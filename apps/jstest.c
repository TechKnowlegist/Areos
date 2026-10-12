// jstest.c - tries out the JavaScript engine: runs a few scripts and says
// what they gave. Run it from the AREOS shell with: run jstest
#include "areos.h"
#include "js/jshost.h"

static int failures;

static void check(const char *source, const char *expected) {
    char out[256];
    js_host_eval_text(source, out, sizeof(out));
    bool same = true;
    for (int i = 0;; i++) {
        if (out[i] != expected[i]) { same = false; break; }
        if (!out[i]) break;
    }
    if (!same) failures++;
    areos_print(same ? "ok   " : "FAIL ");
    areos_print(source);
    areos_print("  =>  ");
    areos_print(out);
    if (!same) { areos_print("   (expected "); areos_print(expected); areos_print(")"); }
    areos_print("\n");
}

int main(void) {
    areos_print("JavaScript engine test\n");
    check("1 + 2", "3");
    check("'abc'.toUpperCase() + [1,2,3].map(x => x * 2).join('-')", "ABC2-4-6");
    check("JSON.stringify({a:[1,2,{b:null}],c:'x'})", "{\"a\":[1,2,{\"b\":null}],\"c\":\"x\"}");
    check("Math.sqrt(2)", "1.4142135623730951");
    check("Math.sin(1)", "0.8414709848078965");
    check("Math.cos(1)", "0.5403023058681398");
    check("Math.atan2(1, 2)", "0.4636476090008061");
    check("Math.exp(1)", "2.718281828459045");
    check("Math.log(10)", "2.302585092994046");
    check("Math.pow(2, 0.5)", "1.4142135623730951");
    check("10 ** 2", "100");
    check("0.1 + 0.2", "0.30000000000000004");
    check("(123.456).toFixed(1) + ' ' + (255).toString(16) + ' ' + parseFloat('3.5e2')", "123.5 ff 350");
    check("'a-b-c'.replace(/-/g, '+') + /(\\d+)/.exec('x42y')[1]", "a+b+c42");
    check("class A { constructor(x){this.x=x} get dbl(){return this.x*2} } new A(21).dbl", "42");
    check("let s = 0; for (const v of new Set([1,2,2,3])) s += v; s", "6");
    check("new Date(86400000 * 365).toISOString()", "1971-01-01T00:00:00.000Z");
    check("typeof Date.now()", "number");
    check("var r = ''; Promise.resolve(5).then(v => { r = v }); r", "");
    check("[3,1,2].sort().join()", "1,2,3");
    check("`t${1+1}`", "t2");
    check("function f(n){return n<2?n:f(n-1)+f(n-2)} f(22)", "17711");
    check("(function r(n){return n? 1 + r(n-1) : 0})(1000)", "1000");
    check("try { (function r(){ r() })() } catch (e) { e instanceof InternalError }", "true");
    check("typeof Math.hypot(3,4)", "number");
    areos_print(failures ? "Some tests failed.\n" : "All tests passed.\n");
    return failures;
}
