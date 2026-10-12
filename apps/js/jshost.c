// jshost.c - AREOS's side of the JavaScript engine (QuickJS).
//
// A web page is run in a fresh JavaScript world: the "prelude" (prelude.js,
// built into the program) builds window, document and the rest of the web
// APIs out of plain JavaScript, then the page's own scripts run on top of it.
// This file is the bridge: it starts and stops that world, provides the few
// things JavaScript can't do for itself (network, clock, console), and lets
// the browser ask for the page as simple HTML.
//
// It is compiled with floating point turned on (the rest of the browser isn't),
// so its interface uses only whole numbers and pointers.

#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <stdio.h>
#include "quickjs.h"
#include "jsplatform.h"
#include "jshost.h"

#ifdef AREOS
#include "jshost_hooks.h"
void js_host_log(const char *text, size_t length) {
    char buf[300];
    if (length > sizeof(buf) - 1) length = sizeof(buf) - 1;
    memcpy(buf, text, length);
    buf[length] = 0;
    jsp_log(3, buf);
}
#endif

extern const char jsh_prelude_start[];   // the prelude source (prelude.S)

static JSRuntime *rt;
static JSContext *ctx;
static JSValue exports = JS_UNDEFINED;
static JSValue rejection_tracker = JS_UNDEFINED;
static bool active;
static bool fatal;
static jmp_buf fatal_jump;
static jmp_buf *fatal_target;
static uint64_t deadline;

static char *render_buf;
static size_t render_cap;
static bool dirty, title_changed, url_changed, reload_wanted, nav_requested;
static char nav_url[2048], nav_method[16], nav_ctype[128], url_buf[2048], title_buf[512];
static char *nav_body;
static bool nav_replace_flag;
static int url_mode;
#define SPEAK_MAX 8
static char *speak_queue[SPEAK_MAX];
static int speak_count;
static char *console_buf;
static size_t console_len, console_cap;
static int history_edge_delta;

#ifdef AREOS
_Noreturn void js_host_fatal(const char *why) {
    (void)why;
    if (fatal_target) longjmp(*fatal_target, 1);
    for (;;) { }
}
#endif

// ---- Small helpers ----

static void console_add(int level, const char *text) {
    static const char *names[] = { "log", "info", "warn", "error" };
    size_t n = strlen(text) + 16;
    if (console_len + n + 1 > console_cap) {
        size_t cap = console_cap ? console_cap * 2 : 4096;
        while (cap < console_len + n + 1) cap *= 2;
        if (cap > (size_t)1 << 20) {                    // keep the last part only
            size_t keep = (size_t)1 << 19;
            if (console_len > keep) { memmove(console_buf, console_buf + console_len - keep, keep); console_len = keep; }
            cap = (size_t)1 << 20;
        }
        char *p = realloc(console_buf, cap);
        if (!p) return;
        console_buf = p;
        console_cap = cap;
    }
    if (console_len + n + 1 > console_cap) return;
    console_len += (size_t)snprintf(console_buf + console_len, console_cap - console_len, "%s: %s\n", names[level & 3], text);
}

static void speak_add(const char *text) {
    if (speak_count >= SPEAK_MAX) return;
    char *copy = strdup(text);
    if (copy) speak_queue[speak_count++] = copy;
}

static int interrupt_check(JSRuntime *r, void *opaque) {
    (void)r; (void)opaque;
    return deadline != 0 && jsp_time_ms() > deadline;
}
static void set_deadline(int ms) { deadline = jsp_time_ms() + (uint64_t)ms; }

static void report_exception(const char *what) {
    JSValue ex = JS_GetException(ctx);
    const char *s = JS_ToCString(ctx, ex);
    char buf[1024];
    JSValue stack = JS_UNDEFINED;
    if (JS_IsObject(ex)) stack = JS_GetPropertyStr(ctx, ex, "stack");
    const char *st = JS_IsString(stack) ? JS_ToCString(ctx, stack) : NULL;
    snprintf(buf, sizeof(buf), "%s: %s%s%s", what, s ? s : "error", st ? "\n" : "", st ? st : "");
    console_add(3, buf);
    if (st) JS_FreeCString(ctx, st);
    JS_FreeValue(ctx, stack);
    if (s) JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, ex);
}

static void run_jobs(void) {
    JSContext *c;
    for (int i = 0; i < 100000; i++) {
        int r = JS_ExecutePendingJob(rt, &c);
        if (r <= 0) {
            if (r < 0) report_exception("Uncaught (job)");
            break;
        }
    }
}

// ---- The native object the prelude talks to ----

static JSValue n_now(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewFloat64(c, (double)jsp_time_ms());
}
static JSValue n_log(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    int level = 0;
    JS_ToInt32(c, &level, argv[0]);
    const char *s = JS_ToCString(c, argv[1]);
    if (s) { console_add(level, s); JS_FreeCString(c, s); }
    return JS_UNDEFINED;
}
static JSValue n_dirty(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)c; (void)this_val; (void)argc; (void)argv;
    dirty = true;
    return JS_UNDEFINED;
}
static JSValue n_microtasks(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)c; (void)this_val; (void)argc; (void)argv;
    run_jobs();
    return JS_UNDEFINED;
}
static JSValue n_title(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *s = JS_ToCString(c, argv[0]);
    if (s) { strncpy(title_buf, s, sizeof(title_buf) - 1); title_buf[sizeof(title_buf) - 1] = 0; JS_FreeCString(c, s); }
    title_changed = true;
    return JS_UNDEFINED;
}
static JSValue n_announce(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *s = JS_ToCString(c, argv[0]);
    if (s) { speak_add(s); JS_FreeCString(c, s); }
    return JS_UNDEFINED;
}
static JSValue n_navigate(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *url = JS_ToCString(c, argv[0]);
    int replace = 0;
    JS_ToInt32(c, &replace, argv[1]);
    const char *method = JS_ToCString(c, argv[2]);
    const char *body = JS_ToCString(c, argv[3]);
    const char *ctype = JS_ToCString(c, argv[4]);
    if (url) {
        strncpy(nav_url, url, sizeof(nav_url) - 1); nav_url[sizeof(nav_url) - 1] = 0;
        strncpy(nav_method, method ? method : "GET", sizeof(nav_method) - 1); nav_method[sizeof(nav_method) - 1] = 0;
        strncpy(nav_ctype, ctype ? ctype : "", sizeof(nav_ctype) - 1); nav_ctype[sizeof(nav_ctype) - 1] = 0;
        free(nav_body);
        nav_body = body ? strdup(body) : NULL;
        nav_replace_flag = replace != 0;
        nav_requested = true;
    }
    if (url) JS_FreeCString(c, url);
    if (method) JS_FreeCString(c, method);
    if (body) JS_FreeCString(c, body);
    if (ctype) JS_FreeCString(c, ctype);
    return JS_UNDEFINED;
}
static JSValue n_url_changed(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *url = JS_ToCString(c, argv[0]);
    int mode = 0;
    JS_ToInt32(c, &mode, argv[1]);
    if (url) { strncpy(url_buf, url, sizeof(url_buf) - 1); url_buf[sizeof(url_buf) - 1] = 0; JS_FreeCString(c, url); }
    url_mode = mode;
    url_changed = true;
    dirty = true;
    return JS_UNDEFINED;
}
static JSValue n_reload(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)c; (void)this_val; (void)argc; (void)argv;
    reload_wanted = true;
    return JS_UNDEFINED;
}
static JSValue n_history_edge(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    JS_ToInt32(c, &history_edge_delta, argv[0]);
    return JS_UNDEFINED;
}
static JSValue n_random(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    size_t size;
    uint8_t *p = JS_GetArrayBuffer(c, &size, argv[0]);
    if (p) jsp_random(p, (int)size);
    return JS_UNDEFINED;
}
static JSValue n_width(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(c, jsp_width());
}
static JSValue n_height(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(c, jsp_height());
}
static JSValue n_theme(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc; (void)argv;
    return JS_NewInt32(c, jsp_theme());
}

// http(method, url, [name, value, ...], arrayBuffer|null, timeoutMs)
static JSValue n_http(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *method = JS_ToCString(c, argv[0]);
    const char *url = JS_ToCString(c, argv[1]);
    int timeout = 30000;
    JS_ToInt32(c, &timeout, argv[4]);
    // headers
    size_t hcap = 1024, hlen = 0;
    char *headers = malloc(hcap);
    int32_t n = 0;
    JSValue len = JS_GetPropertyStr(c, argv[2], "length");
    JS_ToInt32(c, &n, len);
    JS_FreeValue(c, len);
    for (int32_t i = 0; i + 1 < n; i += 2) {
        JSValue k = JS_GetPropertyUint32(c, argv[2], (uint32_t)i), v = JS_GetPropertyUint32(c, argv[2], (uint32_t)i + 1);
        const char *ks = JS_ToCString(c, k), *vs = JS_ToCString(c, v);
        if (ks && vs) {
            size_t need = strlen(ks) + strlen(vs) + 4;
            while (hlen + need + 1 > hcap) { hcap *= 2; headers = realloc(headers, hcap); }
            hlen += (size_t)snprintf(headers + hlen, hcap - hlen, "%s: %s\r\n", ks, vs);
        }
        if (ks) JS_FreeCString(c, ks);
        if (vs) JS_FreeCString(c, vs);
        JS_FreeValue(c, k);
        JS_FreeValue(c, v);
    }
    headers[hlen] = 0;
    size_t body_len = 0;
    uint8_t *body = NULL;
    if (!JS_IsNull(argv[3]) && !JS_IsUndefined(argv[3])) body = JS_GetArrayBuffer(c, &body_len, argv[3]);
    static struct jsp_response resp;
    memset(&resp, 0, sizeof(resp));
    uint64_t saved_deadline = deadline;
    deadline = 0;                                   // waiting for the network isn't the script's fault
    int rc = jsp_http(method ? method : "GET", url ? url : "", headers, (const char *)body, (int)body_len, timeout, &resp);
    deadline = saved_deadline;
    free(headers);
    if (method) JS_FreeCString(c, method);
    if (url) JS_FreeCString(c, url);
    if (rc != 0) {
        JSValue err = JS_ThrowTypeError(c, "Failed to fetch: %s", resp.error[0] ? resp.error : "network error");
        free(resp.headers);
        free(resp.body);
        return err;
    }
    JSValue obj = JS_NewObject(c);
    JS_SetPropertyStr(c, obj, "status", JS_NewInt32(c, resp.status));
    JS_SetPropertyStr(c, obj, "statusText", JS_NewString(c, resp.status_text));
    JS_SetPropertyStr(c, obj, "url", JS_NewString(c, resp.final_url));
    JSValue harr = JS_NewArray(c);
    if (resp.headers) {
        uint32_t idx = 0;
        char *p = resp.headers;
        char *end = resp.headers + resp.headers_len;
        while (p < end) {
            char *eol = memchr(p, '\n', (size_t)(end - p));
            size_t line_len = eol ? (size_t)(eol - p) : (size_t)(end - p);
            size_t l = line_len;
            if (l && p[l - 1] == '\r') l--;
            char *colon = memchr(p, ':', l);
            if (colon) {
                char *v = colon + 1;
                while (v < p + l && *v == ' ') v++;
                JS_SetPropertyUint32(c, harr, idx++, JS_NewStringLen(c, p, (size_t)(colon - p)));
                JS_SetPropertyUint32(c, harr, idx++, JS_NewStringLen(c, v, (size_t)(p + l - v)));
            }
            p += line_len + (eol ? 1 : 0);
        }
    }
    JS_SetPropertyStr(c, obj, "headers", harr);
    JS_SetPropertyStr(c, obj, "body", JS_NewArrayBufferCopy(c, (const uint8_t *)(resp.body ? resp.body : ""), (size_t)resp.body_len));
    free(resp.headers);
    free(resp.body);
    return obj;
}

// ---- Storage that lasts while AREOS is on ----
struct store_entry { char *scope, *key, *value; };
static struct store_entry *store;
static int store_count, store_cap;

static int store_find(const char *scope, const char *key) {
    for (int i = 0; i < store_count; i++) if (!strcmp(store[i].scope, scope) && !strcmp(store[i].key, key)) return i;
    return -1;
}
static JSValue n_store_get(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *scope = JS_ToCString(c, argv[0]), *key = JS_ToCString(c, argv[1]);
    JSValue r = JS_UNDEFINED;
    if (scope && key) { int i = store_find(scope, key); if (i >= 0) r = JS_NewString(c, store[i].value); }
    if (scope) JS_FreeCString(c, scope);
    if (key) JS_FreeCString(c, key);
    return r;
}
static JSValue n_store_set(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *scope = JS_ToCString(c, argv[0]), *key = JS_ToCString(c, argv[1]);
    const char *value = JS_IsUndefined(argv[2]) ? NULL : JS_ToCString(c, argv[2]);
    if (scope && key) {
        int i = store_find(scope, key);
        if (!value) {
            if (i >= 0) {
                free(store[i].scope); free(store[i].key); free(store[i].value);
                store[i] = store[--store_count];
            }
        } else if (strlen(value) < 1000000 && store_count < 2000) {
            if (i >= 0) { free(store[i].value); store[i].value = strdup(value); }
            else {
                if (store_count == store_cap) {
                    store_cap = store_cap ? store_cap * 2 : 32;
                    store = realloc(store, sizeof(*store) * (size_t)store_cap);
                }
                store[store_count].scope = strdup(scope);
                store[store_count].key = strdup(key);
                store[store_count].value = strdup(value);
                store_count++;
            }
        }
    }
    if (scope) JS_FreeCString(c, scope);
    if (key) JS_FreeCString(c, key);
    if (value) JS_FreeCString(c, value);
    return JS_UNDEFINED;
}
static JSValue n_store_keys(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    const char *scope = JS_ToCString(c, argv[0]);
    JSValue arr = JS_NewArray(c);
    uint32_t n = 0;
    if (scope) {
        for (int i = 0; i < store_count; i++) if (!strcmp(store[i].scope, scope)) JS_SetPropertyUint32(c, arr, n++, JS_NewString(c, store[i].key));
        JS_FreeCString(c, scope);
    }
    return arr;
}

// ---- Running code ----
static JSValue n_eval_script(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    size_t len;
    const char *code = JS_ToCStringLen(c, &len, argv[0]);
    const char *name = JS_ToCString(c, argv[1]);
    if (!code) return JS_EXCEPTION;
    set_deadline(20000);
    JSValue v = JS_Eval(c, code, len, name ? name : "script", JS_EVAL_TYPE_GLOBAL);
    JS_FreeCString(c, code);
    if (name) JS_FreeCString(c, name);
    if (JS_IsException(v)) return v;
    JS_FreeValue(c, v);
    return JS_UNDEFINED;
}

static JSModuleDef *module_loader(JSContext *c, const char *name, void *opaque) {
    (void)opaque;
    static struct jsp_response resp;
    memset(&resp, 0, sizeof(resp));
    uint64_t saved = deadline;
    deadline = 0;
    int rc = jsp_http("GET", name, "Accept: */*\r\n", NULL, 0, 30000, &resp);
    deadline = saved;
    if (rc != 0 || resp.status < 200 || resp.status >= 300) {
        JS_ThrowReferenceError(c, "could not load module '%s' (%s)", name, rc ? resp.error : "HTTP error");
        free(resp.headers); free(resp.body);
        return NULL;
    }
    char *src = malloc((size_t)resp.body_len + 1);
    memcpy(src, resp.body, (size_t)resp.body_len);
    src[resp.body_len] = 0;
    free(resp.headers); free(resp.body);
    JSValue f = JS_Eval(c, src, (size_t)resp.body_len, name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    free(src);
    if (JS_IsException(f)) return NULL;
    JSModuleDef *m = JS_VALUE_GET_PTR(f);
    JSValue meta = JS_GetImportMeta(c, m);
    JS_DefinePropertyValueStr(c, meta, "url", JS_NewString(c, name), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(c, meta, "main", JS_FALSE, JS_PROP_C_W_E);
    JS_FreeValue(c, meta);
    JS_FreeValue(c, f);
    return m;
}
static char *module_normalize(JSContext *c, const char *base, const char *name, void *opaque) {
    (void)opaque;
    JSValue fn = JS_GetPropertyStr(c, exports, "resolveModule");
    JSValue args[2] = { JS_NewString(c, base ? base : ""), JS_NewString(c, name) };
    JSValue r = JS_Call(c, fn, JS_UNDEFINED, 2, args);
    JS_FreeValue(c, args[0]);
    JS_FreeValue(c, args[1]);
    JS_FreeValue(c, fn);
    if (JS_IsException(r)) return NULL;
    const char *s = JS_ToCString(c, r);
    char *out = s ? js_strdup(c, s) : NULL;
    if (s) JS_FreeCString(c, s);
    JS_FreeValue(c, r);
    return out;
}
static JSValue n_eval_module(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    size_t len;
    const char *code = JS_ToCStringLen(c, &len, argv[0]);
    const char *name = JS_ToCString(c, argv[1]);
    if (!code) return JS_EXCEPTION;
    set_deadline(20000);
    JSValue f = JS_Eval(c, code, len, name ? name : "module", JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    JS_FreeCString(c, code);
    if (JS_IsException(f)) { if (name) JS_FreeCString(c, name); return f; }
    JSModuleDef *m = JS_VALUE_GET_PTR(f);
    JSValue meta = JS_GetImportMeta(c, m);
    JS_DefinePropertyValueStr(c, meta, "url", JS_NewString(c, name ? name : ""), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(c, meta, "main", JS_TRUE, JS_PROP_C_W_E);
    JS_FreeValue(c, meta);
    if (name) JS_FreeCString(c, name);
    JSValue promise = JS_EvalFunction(c, f);
    if (JS_IsException(promise)) return promise;
    JS_FreeValue(c, promise);
    return JS_UNDEFINED;
}

static void rejection_hook(JSContext *c, JSValueConst promise, JSValueConst reason, JS_BOOL is_handled, void *opaque) {
    (void)opaque;
    if (JS_IsUndefined(rejection_tracker)) return;
    JSValue args[3] = { JS_DupValue(c, promise), JS_DupValue(c, reason), JS_NewBool(c, is_handled) };
    JSValue r = JS_Call(c, rejection_tracker, JS_UNDEFINED, 3, args);
    JS_FreeValue(c, args[0]);
    JS_FreeValue(c, args[1]);
    JS_FreeValue(c, r);
}
static JSValue n_track_rejections(JSContext *c, JSValueConst this_val, int argc, JSValueConst *argv) {
    (void)this_val; (void)argc;
    JS_FreeValue(c, rejection_tracker);
    rejection_tracker = JS_DupValue(c, argv[0]);
    return JS_UNDEFINED;
}

// ---- Starting and stopping ----

static void add_fn(JSValue obj, const char *name, JSCFunction *fn, int args) {
    JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, fn, name, args));
}

static void free_page_state(void) {
    for (int i = 0; i < speak_count; i++) free(speak_queue[i]);
    speak_count = 0;
    free(nav_body);
    nav_body = NULL;
    dirty = title_changed = url_changed = reload_wanted = nav_requested = false;
    history_edge_delta = 0;
}

void jsh_page_stop(void) {
    if (!active) return;
    JS_FreeValue(ctx, exports);
    JS_FreeValue(ctx, rejection_tracker);
    exports = rejection_tracker = JS_UNDEFINED;
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    ctx = NULL;
    rt = NULL;
    active = false;
    free_page_state();
}

bool jsh_page_active(void) { return active && !fatal; }

static JSValue call_export(const char *name, int argc, JSValueConst *argv) {
    JSValue fn = JS_GetPropertyStr(ctx, exports, name);
    JSValue r = JS_Call(ctx, fn, exports, argc, argv);
    JS_FreeValue(ctx, fn);
    return r;
}

bool jsh_page_start(const char *url, const char *html, int length) {
    jsh_page_stop();
    fatal = false;
    console_len = 0;
    free_page_state();
    jmp_buf here;
    fatal_target = &here;
    if (setjmp(here)) { fatal_target = NULL; fatal = true; active = false; return false; }
    rt = JS_NewRuntime();
    if (!rt) { fatal_target = NULL; return false; }
    JS_SetMemoryLimit(rt, (size_t)256 << 20);
    JS_SetGCThreshold(rt, (size_t)8 << 20);
    JS_SetMaxStackSize(rt, 1536 * 1024);
    JS_SetInterruptHandler(rt, interrupt_check, NULL);
    JS_SetModuleLoaderFunc(rt, module_normalize, module_loader, NULL);
    JS_SetHostPromiseRejectionTracker(rt, rejection_hook, NULL);
    ctx = JS_NewContext(rt);
    if (!ctx) { JS_FreeRuntime(rt); rt = NULL; fatal_target = NULL; return false; }
    active = true;

    JSValue native = JS_NewObject(ctx);
    add_fn(native, "now", n_now, 0);
    add_fn(native, "log", n_log, 2);
    add_fn(native, "dirty", n_dirty, 0);
    add_fn(native, "microtasks", n_microtasks, 0);
    add_fn(native, "title", n_title, 1);
    add_fn(native, "announce", n_announce, 1);
    add_fn(native, "navigate", n_navigate, 5);
    add_fn(native, "urlChanged", n_url_changed, 2);
    add_fn(native, "reload", n_reload, 0);
    add_fn(native, "historyEdge", n_history_edge, 1);
    add_fn(native, "random", n_random, 1);
    add_fn(native, "width", n_width, 0);
    add_fn(native, "height", n_height, 0);
    add_fn(native, "theme", n_theme, 0);
    add_fn(native, "http", n_http, 5);
    add_fn(native, "storeGet", n_store_get, 2);
    add_fn(native, "storeSet", n_store_set, 3);
    add_fn(native, "storeKeys", n_store_keys, 1);
    add_fn(native, "evalScript", n_eval_script, 2);
    add_fn(native, "evalModule", n_eval_module, 2);
    add_fn(native, "trackRejections", n_track_rejections, 1);

    set_deadline(10000);
    JSValue prelude = JS_Eval(ctx, jsh_prelude_start, strlen(jsh_prelude_start), "prelude.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(prelude)) { report_exception("prelude"); fatal_target = NULL; jsh_page_stop(); return false; }
    JSValue result = JS_Call(ctx, prelude, JS_UNDEFINED, 1, &native);
    JS_FreeValue(ctx, prelude);
    JS_FreeValue(ctx, native);
    if (JS_IsException(result)) { report_exception("prelude"); fatal_target = NULL; jsh_page_stop(); return false; }
    exports = result;

    JSValue args[2] = { JS_NewString(ctx, url), JS_NewStringLen(ctx, html, (size_t)length) };
    set_deadline(10000);
    JSValue r = call_export("start", 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    if (JS_IsException(r)) report_exception("start");
    JS_FreeValue(ctx, r);
    run_jobs();
    deadline = 0;
    fatal_target = NULL;
    dirty = true;
    return true;
}

// ---- Pumping ----

int jsh_pump(void) {
    if (!active) return 0;
    if (fatal) return JSH_FATAL;
    jmp_buf here;
    fatal_target = &here;
    if (setjmp(here)) { fatal_target = NULL; fatal = true; return JSH_FATAL; }
    int flags = 0;
    // one step of loading (a script, or the load events), if there is one
    set_deadline(25000);
    JSValue r = call_export("step", 0, NULL);
    bool worked = false;
    if (JS_IsException(r)) report_exception("step");
    else worked = JS_ToBool(ctx, r) > 0;
    JS_FreeValue(ctx, r);
    run_jobs();
    // timers
    if (!worked || true) {
        JSValue arg = JS_NewInt32(ctx, 15);
        set_deadline(15000);
        JSValue t = call_export("timers", 1, &arg);
        if (JS_IsException(t)) report_exception("timers");
        else { int w = -1; JS_ToInt32(ctx, &w, t); if (w >= 0) flags |= JSH_TIMERS; }
        JS_FreeValue(ctx, t);
        run_jobs();
    }
    deadline = 0;
    JSValue ld = call_export("loading", 0, NULL);
    if (JS_ToBool(ctx, ld) > 0) flags |= JSH_LOADING;
    JS_FreeValue(ctx, ld);
    fatal_target = NULL;
    if (dirty) { flags |= JSH_DIRTY; dirty = false; }
    if (nav_requested) flags |= JSH_NAV;
    if (url_changed) { flags |= JSH_URL; }
    if (title_changed) { flags |= JSH_TITLE; }
    if (speak_count) flags |= JSH_SPEAK;
    return flags;
}

int jsh_next_timer_ms(void) {
    if (!active || fatal) return -1;
    JSValue r = call_export("nextTimer", 0, NULL);
    int w = -1;
    JS_ToInt32(ctx, &w, r);
    JS_FreeValue(ctx, r);
    return w;
}

static const char *render_with(const char *export_name, int *length);

const char *jsh_render(int *length) { return render_with("render", length); }
const char *jsh_render_full(int *length) { return render_with("renderFull", length); }

static const char *render_with(const char *export_name, int *length) {
    if (!active || fatal) { *length = 0; return ""; }
    jmp_buf here;
    fatal_target = &here;
    if (setjmp(here)) { fatal_target = NULL; fatal = true; *length = 0; return ""; }
    set_deadline(15000);
    JSValue r = call_export(export_name, 0, NULL);
    deadline = 0;
    fatal_target = NULL;
    if (JS_IsException(r)) { report_exception(export_name); *length = 0; return ""; }
    size_t len;
    const char *s = JS_ToCStringLen(ctx, &len, r);
    if (!s) { JS_FreeValue(ctx, r); *length = 0; return ""; }
    if (len + 1 > render_cap) {
        free(render_buf);
        render_cap = len + 1 + 4096;
        render_buf = malloc(render_cap);
    }
    if (!render_buf) { JS_FreeCString(ctx, s); JS_FreeValue(ctx, r); *length = 0; return ""; }
    memcpy(render_buf, s, len);
    render_buf[len] = 0;
    JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, r);
    *length = (int)len;
    return render_buf;
}

int jsh_click(int aid) {
    if (!active || fatal) return 0;
    jmp_buf here;
    fatal_target = &here;
    if (setjmp(here)) { fatal_target = NULL; fatal = true; return 0; }
    JSValue arg = JS_NewInt32(ctx, aid);
    set_deadline(15000);
    JSValue r = call_export("click", 1, &arg);
    deadline = 0;
    fatal_target = NULL;
    int v = 0;
    if (JS_IsException(r)) report_exception("click"); else JS_ToInt32(ctx, &v, r);
    JS_FreeValue(ctx, r);
    run_jobs();
    return v;
}

void jsh_input(int aid, const char *text) {
    if (!active || fatal) return;
    jmp_buf here;
    fatal_target = &here;
    if (setjmp(here)) { fatal_target = NULL; fatal = true; return; }
    JSValue args[2] = { JS_NewInt32(ctx, aid), JS_NewString(ctx, text) };
    set_deadline(10000);
    JSValue r = call_export("input", 2, args);
    deadline = 0;
    fatal_target = NULL;
    if (JS_IsException(r)) report_exception("input");
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, r);
    run_jobs();
}

int jsh_key(int aid, const char *key) {
    if (!active || fatal) return 0;
    jmp_buf here;
    fatal_target = &here;
    if (setjmp(here)) { fatal_target = NULL; fatal = true; return 0; }
    JSValue args[2] = { JS_NewInt32(ctx, aid), JS_NewString(ctx, key) };
    set_deadline(10000);
    JSValue r = call_export("key", 2, args);
    deadline = 0;
    fatal_target = NULL;
    int v = 0;
    if (JS_IsException(r)) report_exception("key"); else JS_ToInt32(ctx, &v, r);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, r);
    run_jobs();
    return v;
}

const char *jsh_nav_url(void) { nav_requested = false; return nav_url; }
bool jsh_nav_replace(void) { return nav_replace_flag; }
const char *jsh_nav_method(void) { return nav_method; }
const char *jsh_nav_body(void) { return nav_body ? nav_body : ""; }
const char *jsh_nav_content_type(void) { return nav_ctype; }
const char *jsh_url(int *mode) { url_changed = false; if (mode) *mode = url_mode; return url_buf; }
const char *jsh_title(void) { title_changed = false; return title_buf; }
const char *jsh_announcement(void) {
    static char current[512];
    if (!speak_count) return NULL;
    strncpy(current, speak_queue[0], sizeof(current) - 1);
    current[sizeof(current) - 1] = 0;
    free(speak_queue[0]);
    memmove(speak_queue, speak_queue + 1, sizeof(char *) * (size_t)(--speak_count));
    return current;
}
const char *jsh_console(void) {
    if (!console_buf) return "";
    console_buf[console_len < console_cap ? console_len : console_cap - 1] = 0;
    return console_buf;
}
bool jsh_wants_reload(void) { bool r = reload_wanted; reload_wanted = false; return r; }

bool jsh_history_can_go(int delta) {
    if (!active || fatal) return false;
    JSValue arg = JS_NewInt32(ctx, delta);
    JSValue r = call_export("canGo", 1, &arg);
    bool ok = JS_ToBool(ctx, r) > 0;
    JS_FreeValue(ctx, r);
    return ok;
}
bool jsh_history_go(int delta) {
    if (!jsh_history_can_go(delta)) return false;
    JSValue arg = JS_NewInt32(ctx, delta);
    set_deadline(10000);
    JSValue r = call_export("go", 1, &arg);
    deadline = 0;
    if (JS_IsException(r)) report_exception("go");
    JS_FreeValue(ctx, r);
    run_jobs();
    return true;
}

// ---- A quick test: evaluate some JavaScript on its own ----
int js_host_eval_text(const char *source, char *out, int out_max) {
    JSRuntime *r = JS_NewRuntime();
    if (!r) { strncpy(out, "out of memory", (size_t)out_max); return 0; }
    JS_SetMaxStackSize(r, 1536 * 1024);
    JSContext *c = JS_NewContext(r);
    int ok = 1;
    JSValue v = JS_Eval(c, source, strlen(source), "test.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(v)) {
        ok = 0;
        JSValue ex = JS_GetException(c);
        const char *s = JS_ToCString(c, ex);
        strncpy(out, s ? s : "error", (size_t)out_max - 1);
        out[out_max - 1] = 0;
        if (s) JS_FreeCString(c, s);
        JS_FreeValue(c, ex);
    } else {
        const char *s = JS_ToCString(c, v);
        strncpy(out, s ? s : "?", (size_t)out_max - 1);
        out[out_max - 1] = 0;
        if (s) JS_FreeCString(c, s);
    }
    JS_FreeValue(c, v);
    JS_FreeContext(c);
    JS_FreeRuntime(r);
    return ok;
}
