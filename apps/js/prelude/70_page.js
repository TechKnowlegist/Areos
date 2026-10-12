// 70_page.js - running a page: its scripts, and the view of it AREOS shows.

// ---- Running scripts, in order ----
const scriptQueue = [];            // things waiting to run: {el, kind}
let phase = 'idle';               // idle -> classic -> deferred -> events -> done
let deferred = [];
K.importMap = { imports: {}, scopes: {} };
K.scriptErrors = 0;

function scriptKind(el) {
  const t = (el.getAttribute('type') || '').trim().toLowerCase();
  if (t === '' || t === 'text/javascript' || t === 'application/javascript' || t === 'application/x-javascript' || t === 'text/ecmascript' || t === 'application/ecmascript' || t === 'text/jscript' || t === 'text/babel-not') return 'classic';
  if (t === 'module') return 'module';
  if (t === 'importmap') return 'importmap';
  return 'data';
}
K.queueScript = function (el) { scriptQueue.push(el); };
function loadScriptText(el) {
  const src = el.getAttribute('src');
  if (src === null || src === '') return { text: el.textContent, url: K.url };
  const url = resolveURL(src);
  const r = netFetch('GET', url, new Headers(), null, 30000);
  if (r.status < 200 || r.status >= 300) throw new Error('HTTP ' + r.status);
  return { text: utf8Decode(new Uint8Array(r.body)), url: r.url };
}
function runScript(el) {
  const kind = scriptKind(el);
  if (kind === 'data') return;
  if (kind === 'importmap') {
    try {
      const m = JSON.parse(el.textContent);
      Object.assign(K.importMap.imports, m.imports || {});
      Object.assign(K.importMap.scopes, m.scopes || {});
    } catch (e) { reportError(e); }
    return;
  }
  if (el.hasAttribute('nomodule')) return;
  let loaded;
  try { loaded = loadScriptText(el); }
  catch (e) {
    logLine(3, 'Could not load script ' + el.getAttribute('src') + ': ' + (e && e.message));
    setTimeout(() => el.dispatchEvent(new Event('error')), 0);
    return;
  }
  document.currentScript = kind === 'classic' ? el : null;
  try {
    if (kind === 'module') N.evalModule(loaded.text, loaded.url);
    else N.evalScript(loaded.text, loaded.url);
  } catch (e) { reportError(e); K.scriptErrors++; }
  document.currentScript = null;
  N.microtasks();
  if (el.hasAttribute('src')) setTimeout(() => el.dispatchEvent(new Event('load')), 0);
}
K.resolveModule = function (base, spec) {
  // import maps first, then plain URL resolution
  const im = K.importMap;
  const tryMap = (map) => {
    if (!map) return null;
    if (spec in map) return map[spec];
    let best = null;
    for (const k of Object.keys(map)) if (k.endsWith('/') && spec.startsWith(k) && (!best || k.length > best.length)) best = k;
    return best ? map[best] + spec.slice(best.length) : null;
  };
  for (const sc of Object.keys(im.scopes)) { if (base.startsWith(new URL(sc, K.url).href)) { const r = tryMap(im.scopes[sc]); if (r) return new URL(r, K.url).href; } }
  const r = tryMap(im.imports);
  if (r) return new URL(r, K.url).href;
  if (/^(\.{0,2}\/|[a-zA-Z][a-zA-Z0-9+.-]*:)/.test(spec)) return new URL(spec, base).href;
  throw new TypeError('Failed to resolve module specifier "' + spec + '". Relative references must start with "/", "./", or "../".');
};

// Start the page: parse the HTML and line up its scripts.
K.start = function (url, html) {
  K.url = url;
  history._e = [{ url, state: null }];
  history._i = 0;
  try { G.isSecureContext = /^https:/.test(url); } catch (e) { /* ignore */ }
  try { G.origin = new URL(url).origin; } catch (e) { /* ignore */ }
  K.parseDocumentInto(document, html);
  document._readyState = 'loading';
  N.title(document.title);
  deferred = [];
  const classic = [];
  for (const s of collect(document, e => e.localName === 'script')) {
    const kind = scriptKind(s);
    if (kind === 'data') continue;
    if (kind === 'importmap') { classic.unshift(s); continue; }
    if (kind === 'module' || s.hasAttribute('defer') || s.hasAttribute('async')) { if (s.hasAttribute('src') || s.textContent) deferred.push(s); }
    else if (s.hasAttribute('src') || s.textContent) classic.push(s);
  }
  for (const s of classic) scriptQueue.push(s);
  phase = 'classic';
};

// Do the next small piece of work. Returns true if there was some.
K.step = function () {
  if (scriptQueue.length) {
    const el = scriptQueue.shift();
    el._started = true;
    runScript(el);
    return true;
  }
  if (phase === 'classic') {
    phase = 'deferred';
    document._readyState = 'interactive';
    for (const s of deferred) scriptQueue.push(s);
    deferred = [];
    return true;
  }
  if (phase === 'deferred') {
    phase = 'events';
    document._readyState = 'interactive';
    document.dispatchEvent(new Event('readystatechange'));
    document.dispatchEvent(new Event('DOMContentLoaded', { bubbles: true }));
    N.microtasks();
    return true;
  }
  if (phase === 'events') {
    phase = 'done';
    document._readyState = 'complete';
    document.dispatchEvent(new Event('readystatechange'));
    G.dispatchEvent(new Event('load'));
    G.dispatchEvent(new Event('pageshow'));
    document.dispatchEvent(new Event('visibilitychange'));
    N.microtasks();
    return true;
  }
  return false;
};
K.loading = () => phase !== 'done' || scriptQueue.length > 0;

// ---- The view of the page that AREOS shows ----
let aidMap = new Map();
let aidNext = 1;
const skipTags = new Set(['script', 'style', 'noscript', 'template', 'head', 'meta', 'link', 'base', 'svg', 'canvas', 'video', 'audio', 'object', 'embed', 'datalist', 'title', 'param', 'source', 'track', 'map', 'area', 'math', 'dialog-closed']);
const passTags = new Set(['h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'p', 'br', 'hr', 'ul', 'ol', 'li', 'dl', 'dt', 'dd', 'table', 'tr', 'td', 'th', 'thead', 'tbody', 'tfoot', 'blockquote', 'pre', 'b', 'strong', 'i', 'em', 'div', 'section', 'article', 'header', 'footer', 'nav', 'main', 'aside', 'figure', 'figcaption', 'address', 'center', 'caption', 'details', 'summary', 'fieldset', 'legend', 'menu', 'form']);
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const clickAttrs = ['onclick', 'data-action', 'data-toggle', 'data-bs-toggle', 'data-target', 'ng-click', '@click', 'v-on:click', 'x-on:click', 'hx-get', 'hx-post', 'data-href', 'data-onclick', 'wire:click', 'phx-click'];
function hasClickHandler(el) {
  if (typeof el.onclick === 'function') return true;
  const l = el._l && el._l.get('click');
  if (l && l.length) return true;
  const keys = Object.keys(el);
  for (const k of keys) {
    if (k.startsWith('__reactProps')) { const p = el[k]; if (p && (typeof p.onClick === 'function' || typeof p.onMouseDown === 'function' || typeof p.onPointerDown === 'function')) return true; }
    else if (k === '_vei' || k === '__vnode') { const p = el[k]; if (p && (p.onClick || (p.props && p.props.onClick))) return true; }
    else if (k.startsWith('$$click') || k === '__click') return true;
  }
  for (const a of clickAttrs) if (el.hasAttribute(a)) return true;
  const role = el.getAttribute('role');
  if (role === 'button' || role === 'link' || role === 'tab' || role === 'menuitem' || role === 'option' || role === 'switch' || role === 'checkbox' || role === 'radio' || role === 'menuitemcheckbox' || role === 'treeitem') return true;
  return false;
}
function isHiddenEl(el) {
  if (el.hasAttribute('hidden') && el.getAttribute('hidden') !== 'until-found') return true;
  if (el.getAttribute('aria-hidden') === 'true' && !el.querySelector('a[href], button, input, select, textarea')) return true;
  if (el.hasAttribute('inert')) return false;
  const sm = el.hasAttribute('style') ? el.style : null;
  if (sm) {
    const d = sm.display;
    if (d === 'none' || /^none\b/.test(d)) return true;
    if (sm.visibility === 'hidden' || sm.visibility === 'collapse') return true;
  }
  if (K.cssHidden && K.cssHidden(el)) return true;
  if (el.localName === 'details' && !el.hasAttribute('open')) return false;
  return false;
}
// the words a control or element should be called
function accessibleText(el, depth = 0) {
  let t = el.getAttribute('aria-label');
  if (t) return t.trim();
  const lb = el.getAttribute('aria-labelledby');
  if (lb) { const parts = lb.split(/\s+/).map(id => document.getElementById(id)).filter(Boolean).map(e => e.textContent.trim()); if (parts.length) return parts.join(' '); }
  return '';
}
function textOfNode(n, out) {
  if (n.nodeType === 3) { out.push(n._d); return; }
  if (n.nodeType !== 1) return;
  if (skipTags.has(n.localName) && n.localName !== 'svg') return;
  if (n.localName === 'svg') { const l = accessibleText(n); if (l) out.push(' ' + l + ' '); else { const t = n.querySelector('title'); if (t) out.push(' ' + t.textContent + ' '); } return; }
  if (isHiddenEl(n)) return;
  if (n.localName === 'img') { const alt = n.getAttribute('alt'); if (alt) out.push(' ' + alt + ' '); return; }
  if (n.localName === 'input' || n.localName === 'select' || n.localName === 'textarea') return;
  const sh = n._sr;
  const kids = sh ? sh._c : n._c;
  if (kids) for (const c of kids) textOfNode(c, out);
}
function plainText(el) {
  const out = [];
  textOfNode(el, out);
  return out.join('').replace(/\s+/g, ' ').trim();
}
function labelFor(el) {
  let t = accessibleText(el);
  if (t) return t;
  const labs = el.labels || [];
  for (const l of labs) { const x = plainText(l); if (x) return x; }
  t = el.getAttribute('title') || el.getAttribute('placeholder') || el.getAttribute('name') || '';
  return t;
}
function nodeAid(el) {
  if (!el._aid) el._aid = aidNext++;
  aidMap.set(el._aid, el);
  return el._aid;
}
function renderChildren(n, out, ctx) {
  const kids = n._sr ? shadowKids(n) : n._c;
  if (kids) for (const c of kids) renderNode(c, out, ctx);
}
function shadowKids(host) {
  // a shadow root's content, with <slot> replaced by the host's own children
  const out = [];
  const expand = (nodes) => {
    for (const c of nodes || []) {
      if (c.nodeType === 1 && c.localName === 'slot') {
        const name = c.getAttribute('name');
        const assigned = (host._c || []).filter(x => name ? (x.nodeType === 1 && x.getAttribute('slot') === name) : !(x.nodeType === 1 && x.hasAttribute('slot')));
        if (assigned.length) out.push(...assigned); else expand(c._c);
      } else out.push(c);
    }
  };
  expand(host._sr._c);
  return out;
}
function renderNode(n, out, ctx) {
  if (n.nodeType === 3) {
    if (ctx.pre) out.push(esc(n._d)); else out.push(esc(n._d));
    return;
  }
  if (n.nodeType !== 1) return;
  const tag = n.localName;
  if (n.namespaceURI !== HTML_NS) {
    if (tag === 'svg') { const l = accessibleText(n) || (n.querySelector('title') ? n.querySelector('title').textContent : ''); if (l) out.push(' ' + esc(l) + ' '); }
    return;
  }
  if (skipTags.has(tag)) return;
  if (isHiddenEl(n)) return;
  switch (tag) {
    case 'a': {
      const href = n.getAttribute('href');
      if (href !== null && href !== '') {
        out.push('<a data-aid="' + nodeAid(n) + '" href="' + esc(n.href || href) + '">');
        renderChildren(n, out, ctx);
        out.push('</a>');
        return;
      }
      if (hasClickHandler(n)) { renderSyntheticButton(n, out, plainText(n) || accessibleText(n) || n.getAttribute('title') || ''); return; }
      renderChildren(n, out, ctx);
      return;
    }
    case 'button': {
      const label = plainText(n) || accessibleText(n) || n.getAttribute('title') || n.value || '';
      renderSyntheticButton(n, out, label);
      return;
    }
    case 'img': {
      const alt = n.getAttribute('alt') || accessibleText(n);
      if (alt) out.push('<img alt="' + esc(alt) + '">');
      return;
    }
    case 'input': return renderInput(n, out);
    case 'textarea': {
      out.push('<input type="text" data-aid="' + nodeAid(n) + '" data-multi="1" value="' + esc(n.value) + '" aria-label="' + esc(labelFor(n) || 'text area') + '">');
      return;
    }
    case 'select': {
      const sel = n.options.filter(o => o.selected);
      const chosen = sel.length ? sel.map(o => o.text).join(', ') : (n.options[n.selectedIndex] ? n.options[n.selectedIndex].text : '');
      const label = labelFor(n);
      renderSyntheticButton(n, out, (label ? label + ': ' : '') + (chosen || 'choose') + ' (menu)');
      return;
    }
    case 'iframe': { const t = n.getAttribute('title') || n.getAttribute('aria-label'); if (t) out.push('<p>[Embedded content: ' + esc(t) + ']</p>'); return; }
    case 'label': renderChildren(n, out, ctx); return;
    case 'h1': case 'h2': case 'h3': case 'h4': case 'h5': case 'h6': case 'p': case 'ul': case 'ol': case 'li': case 'blockquote': case 'table': case 'tr': case 'td': case 'th': case 'dl': case 'dt': case 'dd': case 'b': case 'strong': case 'i': case 'em': case 'hr': case 'br': case 'thead': case 'tbody': case 'tfoot': case 'caption': {
      if (tag === 'br' || tag === 'hr') { out.push('<' + tag + '>'); return; }
      out.push('<' + tag + '>');
      renderChildren(n, out, ctx);
      out.push('</' + tag + '>');
      return;
    }
    case 'pre': out.push('<pre>'); renderChildren(n, out, { pre: true }); out.push('</pre>'); return;
    default: break;
  }
  const role = n.getAttribute('role');
  if (role === 'heading') {
    const lv = Math.min(6, Math.max(1, parseInt(n.getAttribute('aria-level'), 10) || 2));
    out.push('<h' + lv + '>'); renderChildren(n, out, ctx); out.push('</h' + lv + '>');
    return;
  }
  if (role === 'checkbox' || role === 'switch' || role === 'radio' || role === 'menuitemcheckbox') {
    const on = n.getAttribute('aria-checked') === 'true';
    renderSyntheticButton(n, out, (role === 'radio' ? (on ? '(o) ' : '( ) ') : (on ? '[x] ' : '[ ] ')) + (plainText(n) || accessibleText(n)));
    return;
  }
  if (hasClickHandler(n) && !hasInteractiveDescendant(n)) {
    const label = plainText(n) || accessibleText(n) || n.getAttribute('title') || '';
    renderSyntheticButton(n, out, label);
    return;
  }
  if (tag === 'dialog' && !n.hasAttribute('open')) return;
  const lbl = n.getAttribute('aria-label');
  const isLandmark = tag === 'nav' || tag === 'main' || role === 'navigation' || role === 'main';
  if (passTags.has(tag)) {
    if (isLandmark && lbl) out.push('<p><b>' + esc(lbl) + '</b></p>');
    out.push('<' + (tag === 'div' || tag === 'section' || tag === 'article' || tag === 'header' || tag === 'footer' || tag === 'nav' || tag === 'main' || tag === 'aside' || tag === 'form' || tag === 'fieldset' || tag === 'figure' || tag === 'details' || tag === 'summary' || tag === 'figcaption' || tag === 'address' || tag === 'center' || tag === 'legend' || tag === 'menu' ? 'div' : tag) + '>');
    renderChildren(n, out, ctx);
    out.push('</div>');
    return;
  }
  // spans and unknown tags: just their content
  renderChildren(n, out, ctx);
}
function hasInteractiveDescendant(n) {
  return !!(n.querySelector && n.querySelector('a[href], button, input, select, textarea'));
}
function renderSyntheticButton(n, out, label) {
  label = String(label).replace(/\s+/g, ' ').trim();
  if (!label) label = n.getAttribute('title') || n.getAttribute('aria-label') || 'button';
  const expanded = n.getAttribute('aria-expanded');
  if (expanded === 'true') label += ' (expanded)'; else if (expanded === 'false') label += ' (collapsed)';
  if (n.getAttribute('aria-pressed') === 'true') label += ' (pressed)';
  if (n.getAttribute('aria-selected') === 'true') label += ' (selected)';
  if (n.disabled || n.getAttribute('aria-disabled') === 'true') label += ' (disabled)';
  out.push('<button data-aid="' + nodeAid(n) + '">' + esc(label) + '</button>');
}
function renderInput(n, out) {
  const t = n.type;
  if (t === 'hidden') return;
  const label = labelFor(n);
  if (t === 'submit' || t === 'button' || t === 'reset' || t === 'image') {
    renderSyntheticButton(n, out, n.value || n.getAttribute('alt') || accessibleText(n) || (t === 'reset' ? 'Reset' : t === 'submit' ? 'Submit' : 'button'));
    return;
  }
  if (t === 'checkbox' || t === 'radio') {
    renderSyntheticButton(n, out, (t === 'radio' ? (n.checked ? '(o) ' : '( ) ') : (n.checked ? '[x] ' : '[ ] ')) + (label || n.value));
    return;
  }
  if (t === 'file') { out.push('<p>[File upload: ' + esc(label || 'choose file') + ' - not supported yet]</p>'); return; }
  const type = t === 'password' ? 'password' : 'text';
  out.push('<input type="' + type + '" data-aid="' + nodeAid(n) + '" value="' + esc(n.value) + '" aria-label="' + esc(label || t) + '"' + (n.hasAttribute('placeholder') ? ' placeholder="' + esc(n.getAttribute('placeholder')) + '"' : '') + (n.disabled ? ' disabled' : '') + '>');
}
// The page as simple HTML for AREOS to lay out.
K.render = function () {
  aidMap = new Map();
  const out = [];
  out.push('<title>' + esc(document.title) + '</title>');
  const body = document.body || document.documentElement;
  if (body) renderChildren(body, out, {});
  return out.join('');
};

// ---- The whole page, as HTML, for the styled view ----
// Every element with all its attributes, so the browser's style rules apply as
// they do in any browser. Controls get a data-aid so clicks and typing reach the scripts.
const fullVoidTags = new Set(['area', 'base', 'br', 'col', 'embed', 'hr', 'img', 'input', 'link', 'meta', 'param', 'source', 'track', 'wbr']);
const skipFull = new Set(['script', 'template', 'noscript', 'meta', 'title-removed']);
function wantsAid(n) {
  const tag = n.localName;
  if (tag === 'a') { const h = n.getAttribute('href'); if (h !== null && h !== '') return true; }
  if (tag === 'button' || tag === 'select' || tag === 'textarea' || tag === 'summary') return true;
  if (tag === 'input') return n.type !== 'hidden';
  const ti = n.getAttribute('tabindex');
  if (ti !== null && parseInt(ti, 10) >= 0) return true;
  const ce = n.getAttribute('contenteditable');
  if (ce === '' || ce === 'true') return true;
  if (hasClickHandler(n) && !hasInteractiveDescendant(n)) return true;
  return false;
}
function fullAttrs(n, out) {
  const tag = n.localName;
  const skip = new Set(['data-aid']);
  if (tag === 'input') { skip.add('value'); skip.add('checked'); }
  if (tag === 'option') skip.add('selected');
  if (n._a) {
    for (const [k, v] of n._a) {
      if (skip.has(k)) continue;
      out.push(' ' + k + '="' + esc(v) + '"');
    }
  }
  if (tag === 'input') {
    const t = n.type;
    if (t === 'checkbox' || t === 'radio') { if (n.checked) out.push(' checked'); }
    else if (t !== 'file') out.push(' value="' + esc(n.value) + '"');
  }
  if (tag === 'option' && n.selected) out.push(' selected');
  if (n.namespaceURI === HTML_NS && wantsAid(n) && !n.disabled) out.push(' data-aid="' + nodeAid(n) + '"');
}
function fullNode(n, out) {
  if (n.nodeType === 3) { out.push(esc(n._d)); return; }
  if (n.nodeType !== 1) return;
  const tag = n.localName;
  if (skipFull.has(tag)) return;
  const html = n.namespaceURI === HTML_NS;
  if (tag === 'link') {
    const rel = (n.getAttribute('rel') || '').toLowerCase();
    if (rel.indexOf('stylesheet') < 0) return;
  }
  out.push('<' + tag);
  fullAttrs(n, out);
  out.push('>');
  if (fullVoidTags.has(tag) && html) return;
  if (tag === 'style') {
    const t = [];
    for (const c of n._c || []) if (c.nodeType === 3) t.push(c._d);
    out.push(t.join(''));
  } else if (tag === 'textarea') {
    out.push(esc(n.value));
  } else if (tag === 'iframe') {
    // nothing inside
  } else {
    const kids = n._sr ? shadowKids(n) : n._c;
    if (kids) for (const c of kids) fullNode(c, out);
  }
  out.push('</' + tag + '>');
}
K.renderFull = function () {
  aidMap = new Map();
  const out = ['<!DOCTYPE html>'];
  const root = document.documentElement;
  if (root) fullNode(root, out);
  return out.join('');
};
K.aidMap = () => aidMap;
function target(aid) { return aidMap.get(aid) || null; }

// The user pressed Enter/clicked on something with this id.
K.userClick = function (aid) {
  const el = target(aid);
  if (!el) return 0;
  if (el.disabled || el.getAttribute('aria-disabled') === 'true') { N.announce('This control is disabled.'); return 0; }
  if (el.localName === 'select') { cycleSelect(el); return 1; }
  try {
    if (isFocusable(el)) focusElement(el);
    const init = { clientX: 5, clientY: 5, pointerType: 'mouse' };
    el.dispatchEvent(new PointerEvent('pointerdown', Object.assign({ bubbles: true, cancelable: true, button: 0, buttons: 1 }, init)));
    el.dispatchEvent(new MouseEvent('mousedown', Object.assign({ bubbles: true, cancelable: true, button: 0, buttons: 1 }, init)));
    el.dispatchEvent(new PointerEvent('pointerup', Object.assign({ bubbles: true, cancelable: true, button: 0 }, init)));
    el.dispatchEvent(new MouseEvent('mouseup', Object.assign({ bubbles: true, cancelable: true, button: 0 }, init)));
    dispatchClick(el, init);
  } catch (e) { reportError(e); }
  N.microtasks();
  return 1;
};
function cycleSelect(el) {
  const opts = el.options.filter(o => !o.disabled);
  if (!opts.length) return;
  let i = opts.findIndex(o => o.selected);
  const next = opts[(i + 1) % opts.length];
  if (el.multiple) { next.selected = !next.selected; } else { for (const o of el.options) o._selected = o === next; }
  focusElement(el);
  el.dispatchEvent(new Event('input', { bubbles: true }));
  el.dispatchEvent(new Event('change', { bubbles: true }));
  N.announce('Selected ' + next.text);
  N.microtasks();
}
// The user typed in a text box: its whole text is now `text`.
K.userInput = function (aid, text) {
  const el = target(aid);
  if (!el) return;
  try {
    if (K.doc._focus !== el) focusElement(el);
    // use the real (prototype) setter so frameworks that watch the value notice the change
    const proto = el.localName === 'textarea' ? HTMLTextAreaElement.prototype : HTMLInputElement.prototype;
    const desc = Object.getOwnPropertyDescriptor(proto, 'value');
    desc.set.call(el, text);
    el.dispatchEvent(new InputEvent('input', { bubbles: true, composed: true, data: null, inputType: 'insertText' }));
  } catch (e) { reportError(e); }
  N.microtasks();
};
// A key pressed in a text box (Enter, mostly).
K.userKey = function (aid, key) {
  const el = target(aid);
  if (!el) return 0;
  try {
    const codes = { Enter: 13, Escape: 27, ArrowDown: 40, ArrowUp: 38, Tab: 9, Backspace: 8 };
    const init = { key, code: key === 'Enter' ? 'Enter' : key, keyCode: codes[key] || 0, which: codes[key] || 0, bubbles: true, cancelable: true };
    const down = new KeyboardEvent('keydown', init);
    el.dispatchEvent(down);
    let ok = !down.defaultPrevented;
    if (key === 'Enter' && ok) {
      const press = new KeyboardEvent('keypress', Object.assign({ charCode: 13 }, init));
      el.dispatchEvent(press);
      ok = !press.defaultPrevented;
    }
    el.dispatchEvent(new KeyboardEvent('keyup', init));
    if (key === 'Enter' && ok && el.localName === 'input') {
      const form = el.form;
      if (form) {
        const submit = form.elements.find(c => (c.localName === 'button' && c.type === 'submit') || (c.localName === 'input' && (c.type === 'submit' || c.type === 'image')));
        if (submit) dispatchClick(submit);
        else if (form.elements.filter(c => c.localName === 'input' && /^(text|search|email|url|tel|password|number)$/.test(c.type)).length === 1) submitFlow(form, null);
      }
    }
  } catch (e) { reportError(e); }
  N.microtasks();
  return 1;
};
K.renderStats = () => aidMap.size;
