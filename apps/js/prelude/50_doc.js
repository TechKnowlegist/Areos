// 50_doc.js - the Document, and the window around it.

class Document extends Node {
  constructor() {
    super();
    this.ownerDocument = null;
    this._html = true;
    this._focus = null;
    this._cookies = new Map();
    this.currentScript = null;
    this._readyState = 'loading';
    this._title = null;
  }
  get nodeType() { return 9; }
  get nodeName() { return '#document'; }
  get documentElement() { return this._c ? this._c.find(c => c.nodeType === 1) || null : null; }
  get doctype() { return this._c ? this._c.find(c => c.nodeType === 10) || null : null; }
  get head() { const h = this.documentElement; return h && h._c ? h._c.find(c => c.localName === 'head') || null : null; }
  get body() { const h = this.documentElement; return h && h._c ? h._c.find(c => c.localName === 'body' || c.localName === 'frameset') || null : null; }
  set body(b) { const old = this.body; if (old) old._p.replaceChild(b, old); else this.documentElement.appendChild(b); }
  get scrollingElement() { return this.documentElement; }
  get defaultView() { return this === K.doc ? G : null; }
  get readyState() { return this._readyState; }
  get characterSet() { return 'UTF-8'; } get charset() { return 'UTF-8'; } get inputEncoding() { return 'UTF-8'; }
  get contentType() { return this._html ? 'text/html' : 'application/xml'; }
  get compatMode() { return 'CSS1Compat'; }
  get designMode() { return 'off'; } set designMode(v) {}
  get dir() { return ''; } set dir(v) {}
  get hidden() { return false; }
  get visibilityState() { return 'visible'; }
  get referrer() { return ''; }
  get URL() { return this === K.doc ? K.url : 'about:blank'; }
  get documentURI() { return this.URL; }
  get domain() { try { return new URL(this.URL).hostname; } catch (e) { return ''; } }
  get lastModified() { return new Date().toString(); }
  get location() { return this === K.doc ? location : null; }
  set location(v) { if (this === K.doc) location.href = v; }
  get baseURI() {
    if (this !== K.doc) return 'about:blank';
    const base = this.head && this.head.querySelector('base[href]') || this.querySelector('base[href]');
    if (base) { try { return new URL(base.getAttribute('href'), K.url).href; } catch (e) { /* ignore */ } }
    return K.url;
  }
  get title() {
    if (this._title !== null) return this._title;
    const t = this.querySelector('title');
    return t ? t.textContent.replace(/[ \t\n\r\f]+/g, ' ').trim() : '';
  }
  set title(v) {
    v = String(v);
    this._title = null;
    let t = this.querySelector('title');
    if (!t) { const h = this.head; if (!h) return; t = this.createElement('title'); h.appendChild(t); }
    t.textContent = v;
    if (this === K.doc) N.title(v);
  }
  get forms() { return collect(this, e => e.localName === 'form'); }
  get images() { return collect(this, e => e.localName === 'img'); }
  get links() { return collect(this, e => (e.localName === 'a' || e.localName === 'area') && e.hasAttribute('href')); }
  get anchors() { return collect(this, e => e.localName === 'a' && e.hasAttribute('name')); }
  get scripts() { return collect(this, e => e.localName === 'script'); }
  get embeds() { return []; } get plugins() { return []; }
  get all() { return collect(this, () => true); }
  get styleSheets() { return Object.assign([], { item(i) { return this[i] || null; } }); }
  get adoptedStyleSheets() { return this._ass || (this._ass = []); } set adoptedStyleSheets(v) { this._ass = v; }
  get fonts() { return fontFaceSet; }
  get activeElement() { return this._focus && this._focus.isConnected ? this._focus : this.body || this.documentElement; }
  get fullscreenElement() { return null; } get pictureInPictureElement() { return null; } get pointerLockElement() { return null; }
  get cookie() { let s = ''; for (const [k, v] of this._cookies) s += (s ? '; ' : '') + k + '=' + v; return s; }
  set cookie(v) {
    // Cookies live only in this page (AREOS doesn't keep or send cookies).
    const first = String(v).split(';')[0];
    const eq = first.indexOf('=');
    const name = (eq < 0 ? '' : first.slice(0, eq)).trim(), val = eq < 0 ? first.trim() : first.slice(eq + 1).trim();
    if (/;\s*max-age=-|;\s*expires=[^;]*19[0-6]\d/i.test(String(v))) this._cookies.delete(name); else this._cookies.set(name, val);
  }
  hasFocus() { return true; }
  getElementById(id) { return findById(this, String(id)); }
  getElementsByName(n) { return collect(this, e => e.getAttribute('name') === n); }
  getElementsByTagName(tag) { return Element.prototype.getElementsByTagName.call(this, tag); }
  getElementsByClassName(c) { return Element.prototype.getElementsByClassName.call(this, c); }
  _create(local, ns, prefix) { return createElementImpl(this, local, ns || HTML_NS, prefix); }
  createElement(name, opts) {
    name = String(name);
    if (!/^[A-Za-z_:\u0080-￿][-A-Za-z0-9_:.\u0080-￿]*$/.test(name)) throw new DOMException("The tag name provided ('" + name + "') is not a valid name.", 'InvalidCharacterError');
    const el = createElementImpl(this, this._html ? name.toLowerCase() : name, this._html ? HTML_NS : null, null);
    if (opts && opts.is) el.setAttribute('is', opts.is);
    return el;
  }
  createElementNS(ns, qname, opts) {
    qname = String(qname);
    const colon = qname.indexOf(':');
    const local = colon < 0 ? qname : qname.slice(colon + 1);
    return createElementImpl(this, local, ns === null || ns === undefined || ns === '' ? null : ns, colon < 0 ? null : qname.slice(0, colon));
  }
  createTextNode(d) { const t = new Text(String(d)); t.ownerDocument = this; return t; }
  createComment(d) { const t = new Comment(String(d)); t.ownerDocument = this; return t; }
  createCDATASection(d) { const t = new CDATASection(String(d)); t.ownerDocument = this; return t; }
  createDocumentFragment() { const f = new DocumentFragment(); f.ownerDocument = this; return f; }
  createProcessingInstruction(target, data) { const p = new ProcessingInstruction(data); p.target = target; p.ownerDocument = this; return p; }
  createAttribute(name) { return new Attr(null, name, ''); }
  createEvent(type) {
    const map = { Event: Event, Events: Event, HTMLEvents: Event, UIEvent: UIEvent, UIEvents: UIEvent, MouseEvent: MouseEvent, MouseEvents: MouseEvent, KeyboardEvent: KeyboardEvent, CustomEvent: CustomEvent, FocusEvent: FocusEvent, MessageEvent: MessageEvent, TouchEvent: TouchEvent };
    const cls = map[type];
    if (!cls) throw new DOMException('The provided event type (\'' + type + '\') is invalid.', 'NotSupportedError');
    const ev = new cls('');
    ev.initUIEvent = ev.initMouseEvent = ev.initKeyboardEvent = function (type, bubbles, cancelable) { this.type = type; this.bubbles = !!bubbles; this.cancelable = !!cancelable; };
    ev.initCustomEvent = function (type, bubbles, cancelable, detail) { this.type = type; this.bubbles = !!bubbles; this.cancelable = !!cancelable; this.detail = detail; };
    return ev;
  }
  createRange() { return new Range(); }
  createTreeWalker(root, what, filter) { return new TreeWalker(root, what === undefined ? 0xffffffff : what, filter); }
  createNodeIterator(root, what, filter) { return new TreeWalker(root, what === undefined ? 0xffffffff : what, filter, true); }
  importNode(node, deep) { return cloneNodeImpl(node, !!deep, this); }
  adoptNode(node) { if (node._p) node._p.removeChild(node); adopt(node, this); return node; }
  elementFromPoint() { return null; } elementsFromPoint() { return []; }
  getSelection() { return selection; }
  execCommand() { return false; } queryCommandSupported() { return false; } queryCommandEnabled() { return false; } queryCommandState() { return false; }
  exitFullscreen() { return Promise.resolve(); }
  startViewTransition(cb) { if (cb) Promise.resolve().then(cb); return { finished: Promise.resolve(), ready: Promise.resolve(), updateCallbackDone: Promise.resolve(), skipTransition() {} }; }
  get implementation() { return implementation; }
  open() { this._written = ''; return this; }
  close() {}
  write(...parts) { this._write(parts.join('')); }
  writeln(...parts) { this._write(parts.join('') + '\n'); }
  _write(html) {
    // document.write while the page is loading: the new HTML goes in just after the running script
    const cs = this.currentScript;
    const parent = (cs && cs._p) || this.body || this.documentElement;
    if (!parent) return;
    const frag = parseFragment(html, parent.nodeType === 1 ? parent : this.body, this);
    for (const s of collect(frag, e => e.localName === 'script')) { s._started = false; s._parserInserted = false; }
    parent.insertBefore(frag, cs ? cs._next : null);
  }
  createExpression() { throw new DOMException('XPath is not supported', 'NotSupportedError'); }
  evaluate() { throw new DOMException('XPath is not supported', 'NotSupportedError'); }
  createNSResolver() { return () => null; }
  hasStorageAccess() { return Promise.resolve(true); }
  requestStorageAccess() { return Promise.resolve(); }
}
mixin(Document, ParentNodeMixin);
installHandlers(Document.prototype);
class HTMLDocument extends Document {
  constructor(xml) { super(); this._html = !xml; }
}
class XMLDocument extends Document {}
publish('Document', Document);
publish('HTMLDocument', HTMLDocument);
publish('XMLDocument', XMLDocument);
const implementation = {
  createHTMLDocument(title) {
    const d = new HTMLDocument();
    K.parseDocumentInto(d, '<!DOCTYPE html><html><head>' + (title !== undefined ? '<title>' + escText(String(title)) + '</title>' : '') + '</head><body></body></html>');
    return d;
  },
  createDocument(ns, qname) { const d = new XMLDocument(); d._html = false; if (qname) d.appendChild(d.createElementNS(ns, qname)); return d; },
  createDocumentType(name, pub, sys) { return new DocumentType(name, pub, sys); },
  hasFeature() { return true; },
};
K.templateDoc = () => K.tplDoc || (K.tplDoc = implementation.createHTMLDocument());
publish('DOMImplementation', function DOMImplementation() {});

class Range {
  constructor() { this.startContainer = this.endContainer = K.doc; this.startOffset = this.endOffset = 0; this.collapsed = true; this.commonAncestorContainer = K.doc; }
  setStart(n, o) { this.startContainer = n; this.startOffset = o; this._upd(); } setEnd(n, o) { this.endContainer = n; this.endOffset = o; this._upd(); }
  setStartBefore(n) { this.setStart(n._p, n._p._c.indexOf(n)); } setStartAfter(n) { this.setStart(n._p, n._p._c.indexOf(n) + 1); }
  setEndBefore(n) { this.setEnd(n._p, n._p._c.indexOf(n)); } setEndAfter(n) { this.setEnd(n._p, n._p._c.indexOf(n) + 1); }
  _upd() { this.collapsed = this.startContainer === this.endContainer && this.startOffset === this.endOffset; this.commonAncestorContainer = this.startContainer; }
  selectNode(n) { this.setStartBefore(n); this.setEndAfter(n); } selectNodeContents(n) { this.setStart(n, 0); this.setEnd(n, n._c ? n._c.length : (n._d || '').length); }
  collapse(toStart) { if (toStart) { this.endContainer = this.startContainer; this.endOffset = this.startOffset; } else { this.startContainer = this.endContainer; this.startOffset = this.endOffset; } this._upd(); }
  cloneRange() { const r = new Range(); Object.assign(r, this); return r; }
  detach() {} toString() { return ''; }
  getBoundingClientRect() { return { x: 0, y: 0, left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0 }; }
  getClientRects() { return []; }
  createContextualFragment(html) { return parseFragment(String(html), this.startContainer.nodeType === 1 ? this.startContainer : K.doc.body, K.doc); }
  deleteContents() {} insertNode(n) { const c = this.startContainer; if (c.nodeType === 3) { c._p.insertBefore(n, c._next); } else c.insertBefore(n, c._c ? c._c[this.startOffset] || null : null); }
  extractContents() { return K.doc.createDocumentFragment(); } cloneContents() { return K.doc.createDocumentFragment(); } surroundContents() {}
  compareBoundaryPoints() { return 0; } isPointInRange() { return false; } comparePoint() { return 0; } intersectsNode() { return false; }
}
publish('Range', Range);
publish('StaticRange', Range);
const selection = {
  rangeCount: 0, anchorNode: null, focusNode: null, anchorOffset: 0, focusOffset: 0, isCollapsed: true, type: 'None',
  getRangeAt() { return new Range(); }, addRange() {}, removeAllRanges() {}, removeRange() {}, empty() {}, collapse() {}, collapseToStart() {}, collapseToEnd() {}, extend() {}, selectAllChildren() {}, setBaseAndExtent() {}, containsNode() { return false; }, deleteFromDocument() {}, toString() { return ''; }, modify() {},
};
publish('Selection', function Selection() {});
class TreeWalker {
  constructor(root, what, filter, iterator) { this.root = root; this.whatToShow = what; this.filter = filter || null; this.currentNode = root; this._it = !!iterator; this.referenceNode = root; this.pointerBeforeReferenceNode = true; }
  _ok(n) {
    const bit = 1 << (n.nodeType - 1);
    if (!(this.whatToShow & bit)) return 3;     // skip
    if (!this.filter) return 1;
    const r = typeof this.filter === 'function' ? this.filter(n) : this.filter.acceptNode(n);
    return r;
  }
  _next(n, descend) {
    if (descend && n._c && n._c.length) return n._c[0];
    while (n && n !== this.root) { if (n._next) return n._next; n = n._p; }
    return null;
  }
  nextNode() {
    let n = this.currentNode, descend = true;
    for (;;) {
      n = this._next(n, descend);
      if (!n) return null;
      const r = this._ok(n);
      if (r === 1) { this.currentNode = this.referenceNode = n; return n; }
      descend = r !== 2;
    }
  }
  previousNode() {
    let n = this.currentNode;
    while (n && n !== this.root) {
      if (n._prev) { n = n._prev; while (n._c && n._c.length) n = n._c[n._c.length - 1]; } else n = n._p;
      if (n && n !== this.root && this._ok(n) === 1) { this.currentNode = this.referenceNode = n; return n; }
    }
    return null;
  }
  parentNode() { let n = this.currentNode; while (n && n !== this.root) { n = n._p; if (n && this._ok(n) === 1) { this.currentNode = n; return n; } } return null; }
  firstChild() { const c = this.currentNode._c && this.currentNode._c.find(x => this._ok(x) === 1); if (c) this.currentNode = c; return c || null; }
  lastChild() { const l = this.currentNode._c || []; for (let k = l.length - 1; k >= 0; k--) if (this._ok(l[k]) === 1) { this.currentNode = l[k]; return l[k]; } return null; }
  nextSibling() { for (let n = this.currentNode._next; n; n = n._next) if (this._ok(n) === 1) { this.currentNode = n; return n; } return null; }
  previousSibling() { for (let n = this.currentNode._prev; n; n = n._prev) if (this._ok(n) === 1) { this.currentNode = n; return n; } return null; }
  detach() {}
}
publish('TreeWalker', TreeWalker);
publish('NodeIterator', TreeWalker);
publish('NodeFilter', { FILTER_ACCEPT: 1, FILTER_REJECT: 2, FILTER_SKIP: 3, SHOW_ALL: 0xffffffff, SHOW_ELEMENT: 1, SHOW_ATTRIBUTE: 2, SHOW_TEXT: 4, SHOW_CDATA_SECTION: 8, SHOW_COMMENT: 128, SHOW_DOCUMENT: 256, SHOW_DOCUMENT_TYPE: 512, SHOW_DOCUMENT_FRAGMENT: 1024 });
const fontFaceSet = Object.assign(new EventTarget(), { ready: Promise.resolve(), status: 'loaded', size: 0, check: () => true, load: () => Promise.resolve([]), add() {}, delete() { return true; }, clear() {}, forEach() {}, has() { return false; } });
publish('FontFace', class FontFace { constructor(f, s) { this.family = f; this.status = 'loaded'; } load() { return Promise.resolve(this); } });

// the page's document
const document = new HTMLDocument();
K.doc = document;
publish('document', document);

// ---- window ----
for (const m of ['addEventListener', 'removeEventListener', 'dispatchEvent']) hid(G, m, EventTarget.prototype[m]);
G._l = null;
installHandlers(G);
for (const k of ['window', 'self', 'frames', 'globalThis']) publish(k, G);
publish('top', G); publish('parent', G);
publish('opener', null);
publish('frameElement', null);
Object.defineProperty(G, 'location', { get() { return location; }, set(v) { location.href = v; }, configurable: true, enumerable: true });
publish('history', history);
publish('closed', false);
publish('name', '');
publish('length', 0);
publish('status', '');
publish('devicePixelRatio', 1);
publish('innerWidth', N.width());
publish('innerHeight', N.height());
publish('outerWidth', N.width());
publish('outerHeight', N.height());
publish('screenX', 0); publish('screenY', 0); publish('screenLeft', 0); publish('screenTop', 0);
publish('scrollX', 0); publish('scrollY', 0); publish('pageXOffset', 0); publish('pageYOffset', 0);
publish('isSecureContext', false);
publish('crossOriginIsolated', false);
publish('origin', 'null');
publish('scrollTo', () => {}); publish('scrollBy', () => {}); publish('scroll', () => {});
publish('focus', () => {}); publish('blur', () => {}); publish('stop', () => {}); publish('print', () => {});
publish('moveTo', () => {}); publish('resizeTo', () => {}); publish('moveBy', () => {}); publish('resizeBy', () => {});
publish('close', () => {});
publish('open', (url) => { if (url) K.navigate(resolveURL(url), false, true); return null; });
publish('alert', (m) => { N.announce(String(m === undefined ? '' : m)); logLine(1, 'alert: ' + m); });
publish('confirm', (m) => { N.announce(String(m) + ' (AREOS answers no)'); logLine(1, 'confirm: ' + m); return false; });
publish('prompt', (m) => { N.announce(String(m) + ' (AREOS cancels)'); logLine(1, 'prompt: ' + m); return null; });
publish('getSelection', () => selection);
publish('postMessage', (data, origin) => { setTimeout(() => G.dispatchEvent(new MessageEvent('message', { data, origin: new URL(K.url).origin, source: G })), 0); });
publish('getComputedStyle', (el) => computedStyle(el));
publish('requestIdleCallback', G.requestIdleCallback);
publish('visualViewport', Object.assign(new EventTarget(), { width: N.width(), height: N.height(), scale: 1, offsetLeft: 0, offsetTop: 0, pageLeft: 0, pageTop: 0 }));
publish('speechSynthesis', { speak() {}, cancel() {}, getVoices: () => [], pause() {}, resume() {}, speaking: false, pending: false, paused: false, addEventListener() {}, removeEventListener() {} });
publish('SpeechSynthesisUtterance', function SpeechSynthesisUtterance(t) { this.text = t; });
publish('caches', undefined);
publish('indexedDB', undefined);
publish('chrome', undefined);

const displayDefaults = { div: 'block', p: 'block', h1: 'block', h2: 'block', h3: 'block', h4: 'block', h5: 'block', h6: 'block', ul: 'block', ol: 'block', li: 'list-item', section: 'block', article: 'block', header: 'block', footer: 'block', nav: 'block', main: 'block', aside: 'block', form: 'block', table: 'table', tr: 'table-row', td: 'table-cell', th: 'table-cell', pre: 'block', blockquote: 'block', hr: 'block', fieldset: 'block', dl: 'block', dd: 'block', dt: 'block', details: 'block', summary: 'block', figure: 'block', address: 'block', img: 'inline', script: 'none', style: 'none', head: 'none', title: 'none', meta: 'none', link: 'none', template: 'none', area: 'none', datalist: 'none', noscript: 'none', input: 'inline-block', button: 'inline-block', select: 'inline-block', textarea: 'inline-block' };
function computedStyle(el) {
  const inline = el.style;
  const defaults = { display: (el.nodeType === 1 && displayDefaults[el.localName]) || 'inline', visibility: 'visible', position: 'static', opacity: '1', overflow: 'visible', 'overflow-x': 'visible', 'overflow-y': 'visible', color: 'rgb(255, 255, 255)', 'background-color': 'rgba(0, 0, 0, 0)', 'font-size': '16px', 'font-family': 'monospace', 'font-weight': '400', 'line-height': 'normal', width: 'auto', height: 'auto', 'z-index': 'auto', transform: 'none', transition: 'none 0s ease 0s', 'transition-duration': '0s', 'animation-duration': '0s', 'animation-name': 'none', 'pointer-events': 'auto', cursor: 'auto', direction: 'ltr', 'box-sizing': 'content-box', 'text-align': 'start', 'white-space': 'normal', margin: '0px', padding: '0px', 'padding-left': '0px', 'padding-right': '0px', 'padding-top': '0px', 'padding-bottom': '0px', 'margin-left': '0px', 'margin-right': '0px', 'margin-top': '0px', 'margin-bottom': '0px', 'border-width': '0px', 'border-left-width': '0px', 'border-right-width': '0px', 'border-top-width': '0px', 'border-bottom-width': '0px', 'scroll-behavior': 'auto' };
  if (el.nodeType === 1 && el.hasAttribute('hidden')) defaults.display = 'none';
  const api = {
    getPropertyValue(n) { n = String(n); const v = inline.getPropertyValue(n); return v || (n in defaults ? defaults[n] : (n.startsWith('--') ? '' : '')); },
    getPropertyPriority() { return ''; }, item(i) { return Object.keys(defaults)[i] || ''; }, get length() { return Object.keys(defaults).length; },
  };
  return new Proxy(api, {
    get(t, p) {
      if (typeof p === 'symbol') return undefined;
      if (p in t) return t[p];
      if (p === 'cssText') return '';
      return t.getPropertyValue(kebab(p));
    },
    set() { return true; },
  });
}
K.computedStyle = computedStyle;
publish('CSS', { supports: () => false, escape: (s) => String(s).replace(/[^\w\u0080-￿-]/g, (c) => '\\' + c), registerProperty() {}, px: (v) => v + 'px', highlights: new Map() });
publish('CSSStyleSheet', function CSSStyleSheet() { return makeSheet(); });
publish('StyleSheet', function StyleSheet() {});
publish('CSSRule', function CSSRule() {});
publish('MediaQueryList', function MediaQueryList() {});
publish('matchMedia', (q) => {
  q = String(q);
  const w = N.width(), h = N.height();
  const eval1 = (s) => {
    s = s.trim().toLowerCase();
    let m;
    if (s === 'all' || s === 'screen' || s === '') return true;
    if (s === 'print' || s === 'speech') return false;
    if ((m = /^\(\s*(min|max)-width\s*:\s*([\d.]+)(px|em|rem)\s*\)$/.exec(s))) { const v = parseFloat(m[2]) * (m[3] === 'px' ? 1 : 16); return m[1] === 'min' ? w >= v : w <= v; }
    if ((m = /^\(\s*(min|max)-height\s*:\s*([\d.]+)(px|em|rem)\s*\)$/.exec(s))) { const v = parseFloat(m[2]) * (m[3] === 'px' ? 1 : 16); return m[1] === 'min' ? h >= v : h <= v; }
    if ((m = /^\(\s*prefers-color-scheme\s*:\s*(\w+)\s*\)$/.exec(s))) return m[1] === (N.theme() ? 'light' : 'dark');
    if ((m = /^\(\s*prefers-reduced-motion\s*:\s*(\w+)\s*\)$/.exec(s))) return m[1] === 'reduce';
    if ((m = /^\(\s*orientation\s*:\s*(\w+)\s*\)$/.exec(s))) return m[1] === (w >= h ? 'landscape' : 'portrait');
    if (/^\(\s*(hover|any-hover)\s*:\s*none\s*\)$/.test(s)) return true;
    if (/^\(\s*(pointer|any-pointer)\s*:\s*(coarse|fine)\s*\)$/.test(s)) return /fine/.test(s);
    if (/^\(\s*(-webkit-)?min-resolution|resolution/.test(s)) return /1dppx|96dpi/.test(s);
    return false;
  };
  const evalQuery = (qq) => qq.split(',').some(part => {
    let neg = false;
    part = part.trim();
    if (/^not\s/.test(part)) { neg = true; part = part.slice(4); }
    const ok = part.replace(/^only\s+/, '').split(/\s+and\s+/).every(eval1);
    return neg ? !ok : ok;
  });
  const mql = new EventTarget();
  Object.assign(mql, { media: q, matches: evalQuery(q), onchange: null, addListener() {}, removeListener() {} });
  return mql;
});
