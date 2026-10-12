// 10_node.js - the page's tree of nodes: Node, Text, Element, and the things
// that hang off an element (attributes, classList, style, dataset).

const HTML_NS = 'http://www.w3.org/1999/xhtml';
const SVG_NS = 'http://www.w3.org/2000/svg';
const MATHML_NS = 'http://www.w3.org/1998/Math/MathML';
const XLINK_NS = 'http://www.w3.org/1999/xlink';
const XMLNS_NS = 'http://www.w3.org/2000/xmlns/';
const XML_NS = 'http://www.w3.org/XML/1998/namespace';

let mutationRev = 0;
K.rev = () => mutationRev;
const observers = new Set();
function changed(target, record) {
  mutationRev++;
  N.dirty();
  if (observers.size && record) queueRecord(target, record);
}
K.changed = changed;

class NodeList extends Array {
  item(i) { return this[i] || null; }
}
publish('NodeList', NodeList);
publish('HTMLCollection', NodeList);
NodeList.prototype.namedItem = function (name) { return this.find(e => e.id === name || e.getAttribute('name') === name) || null; };

const isElement = (n) => n && n.nodeType === 1;

class Node extends EventTarget {
  constructor() {
    super();
    this._p = null; this._prev = null; this._next = null; this._c = null;
    this.ownerDocument = K.doc || null;
  }
  get parentNode() { return this._p; }
  get parentElement() { return this._p && this._p.nodeType === 1 ? this._p : null; }
  get childNodes() { return this._c || (this._c = new NodeList()); }
  get firstChild() { return this._c && this._c.length ? this._c[0] : null; }
  get lastChild() { return this._c && this._c.length ? this._c[this._c.length - 1] : null; }
  get previousSibling() { return this._prev; }
  get nextSibling() { return this._next; }
  get isConnected() { let n = this; while (n._p) n = n._p; return n.nodeType === 9; }
  get nodeValue() { return null; }
  set nodeValue(v) {}
  get baseURI() { return K.doc ? K.doc.baseURI : ''; }
  get textContent() {
    if (!this._c) return '';
    let out = '';
    for (const c of this._c) if (c.nodeType === 1 || c.nodeType === 3 || c.nodeType === 11) out += c.textContent;
    return out;
  }
  set textContent(v) {
    v = v == null ? '' : String(v);
    this._removeAll();
    if (v !== '') this.appendChild(this.ownerDocument.createTextNode(v));
  }
  hasChildNodes() { return !!(this._c && this._c.length); }
  getRootNode(opts) {
    let n = this;
    while (n._p) n = n._p;
    return n;
  }
  _eventParent() {
    if (this._p) return this._p;
    if (this.nodeType === 9) return G;
    if (this.nodeType === 11 && this.host) return this.host;
    return null;
  }
  contains(other) {
    for (let n = other; n; n = n._p) if (n === this) return true;
    return false;
  }
  compareDocumentPosition(other) {
    if (other === this) return 0;
    if (this.contains(other)) return 20;   // contained by, following
    if (other.contains(this)) return 10;   // contains, preceding
    const a = [], b = [];
    for (let n = this; n; n = n._p) a.unshift(n);
    for (let n = other; n; n = n._p) b.unshift(n);
    if (a[0] !== b[0]) return 1;
    let i = 0;
    while (a[i] === b[i]) i++;
    const ia = a[i]._p._c.indexOf(a[i]), ib = b[i]._p._c.indexOf(b[i]);
    return ia < ib ? 4 : 2;
  }
  isSameNode(o) { return this === o; }
  isEqualNode(o) { return !!o && this.nodeType === o.nodeType && this.nodeName === o.nodeName && serializeNode(this) === serializeNode(o); }
  lookupNamespaceURI() { return null; }
  normalize() {}
  _removeAll() {
    if (!this._c) return;
    const old = this._c.slice();
    for (const ch of old) this._detach(ch, false);
    this._c.length = 0;
    changed(this, old.length ? { type: 'childList', target: this, addedNodes: [], removedNodes: old, previousSibling: null, nextSibling: null } : null);
    for (const ch of old) notifyDisconnected(ch);
  }
  _detach(ch) {
    ch._p = null; ch._prev = null; ch._next = null;
  }
  _checkInsert(node, ref) {
    if (node === this || (node.contains && node.contains(this))) throw new DOMException('The new child element contains the parent.', 'HierarchyRequestError');
    if (ref && ref._p !== this) throw new DOMException('The node before which the new node is to be inserted is not a child of this node.', 'NotFoundError');
    const t = this.nodeType;
    if (t === 3 || t === 8 || t === 10) throw new DOMException('This node type does not support children.', 'HierarchyRequestError');
  }
  insertBefore(node, ref) {
    if (ref === undefined) ref = null;
    if (!(node instanceof Node)) throw new TypeError("Failed to execute 'insertBefore' on 'Node': parameter 1 is not of type 'Node'.");
    this._checkInsert(node, ref);
    if (node.nodeType === 11) {
      const kids = node._c ? node._c.slice() : [];
      for (const k of kids) this.insertBefore(k, ref);
      return node;
    }
    if (node._p) node._p.removeChild(node);
    if (!this._c) this._c = new NodeList();
    const list = this._c;
    let idx = ref ? list.indexOf(ref) : list.length;
    const prev = idx > 0 ? list[idx - 1] : null;
    list.splice(idx, 0, node);
    node._p = this; node._prev = prev; node._next = ref;
    if (prev) prev._next = node;
    if (ref) ref._prev = node;
    if (node.ownerDocument !== this.ownerDocument && this.nodeType !== 9) adopt(node, this.ownerDocument);
    changed(this, { type: 'childList', target: this, addedNodes: [node], removedNodes: [], previousSibling: prev, nextSibling: ref });
    if (this.isConnected) notifyConnected(node);
    return node;
  }
  appendChild(node) { return this.insertBefore(node, null); }
  removeChild(child) {
    if (!child || child._p !== this) throw new DOMException('The node to be removed is not a child of this node.', 'NotFoundError');
    const list = this._c;
    const idx = list.indexOf(child);
    const prev = child._prev, next = child._next;
    const wasConnected = this.isConnected;
    list.splice(idx, 1);
    if (prev) prev._next = next;
    if (next) next._prev = prev;
    this._detach(child);
    changed(this, { type: 'childList', target: this, addedNodes: [], removedNodes: [child], previousSibling: prev, nextSibling: next });
    if (wasConnected) notifyDisconnected(child);
    return child;
  }
  replaceChild(node, old) {
    if (!old || old._p !== this) throw new DOMException('The node to be replaced is not a child of this node.', 'NotFoundError');
    const next = old._next === node ? node._next : old._next;
    this.removeChild(old);
    this.insertBefore(node, next);
    return old;
  }
  cloneNode(deep) { return cloneNodeImpl(this, !!deep, this.ownerDocument || K.doc); }
}
Node.ELEMENT_NODE = 1; Node.ATTRIBUTE_NODE = 2; Node.TEXT_NODE = 3; Node.CDATA_SECTION_NODE = 4; Node.PROCESSING_INSTRUCTION_NODE = 7; Node.COMMENT_NODE = 8;
Node.DOCUMENT_NODE = 9; Node.DOCUMENT_TYPE_NODE = 10; Node.DOCUMENT_FRAGMENT_NODE = 11;
Node.DOCUMENT_POSITION_DISCONNECTED = 1; Node.DOCUMENT_POSITION_PRECEDING = 2; Node.DOCUMENT_POSITION_FOLLOWING = 4; Node.DOCUMENT_POSITION_CONTAINS = 8; Node.DOCUMENT_POSITION_CONTAINED_BY = 16;
for (const k of Object.keys(Node)) Node.prototype[k] = Node[k];
publish('Node', Node);

function adopt(node, doc) {
  node.ownerDocument = doc;
  if (node._c) for (const c of node._c) adopt(c, doc);
}
function notifyConnected(node) {
  if (node._connected) node._connected();
  if (node._c) for (const c of node._c.slice()) notifyConnected(c);
  if (node._sr) notifyConnected(node._sr);
}
function notifyDisconnected(node) {
  if (node._disconnected) node._disconnected();
  if (node._c) for (const c of node._c.slice()) notifyDisconnected(c);
}

// Things that elements, text and comments share: before/after/remove/replaceWith.
const ChildNodeMixin = {
  remove() { if (this._p) this._p.removeChild(this); },
  before(...nodes) { const p = this._p; if (!p) return; const ref = viablePrev(this, nodes); p.insertBefore(nodesToNode(nodes, this.ownerDocument), ref ? ref._next : p.firstChild); },
  after(...nodes) { const p = this._p; if (!p) return; p.insertBefore(nodesToNode(nodes, this.ownerDocument), this._next); },
  replaceWith(...nodes) { const p = this._p; if (!p) return; const next = viableNext(this, nodes); const frag = nodesToNode(nodes, this.ownerDocument); if (this._p === p) p.replaceChild(frag, this); else p.insertBefore(frag, next); },
};
function viablePrev(n, nodes) { let p = n._prev; while (p && nodes.includes(p)) p = p._prev; return p; }
function viableNext(n, nodes) { let p = n._next; while (p && nodes.includes(p)) p = p._next; return p; }
function nodesToNode(nodes, doc) {
  if (nodes.length === 1 && nodes[0] instanceof Node) return nodes[0];
  const frag = doc.createDocumentFragment();
  for (const n of nodes) frag.appendChild(n instanceof Node ? n : doc.createTextNode(String(n)));
  return frag;
}
const ParentNodeMixin = {
  append(...nodes) { this.appendChild(nodesToNode(nodes, this.ownerDocument)); },
  prepend(...nodes) { this.insertBefore(nodesToNode(nodes, this.ownerDocument), this.firstChild); },
  replaceChildren(...nodes) { this._removeAll(); if (nodes.length) this.appendChild(nodesToNode(nodes, this.ownerDocument)); },
  get children() { const out = new NodeList(); if (this._c) for (const c of this._c) if (c.nodeType === 1) out.push(c); return out; },
  get childElementCount() { let n = 0; if (this._c) for (const c of this._c) if (c.nodeType === 1) n++; return n; },
  get firstElementChild() { if (this._c) for (const c of this._c) if (c.nodeType === 1) return c; return null; },
  get lastElementChild() { if (this._c) for (let i = this._c.length - 1; i >= 0; i--) if (this._c[i].nodeType === 1) return this._c[i]; return null; },
  querySelector(sel) { return queryOne(this, sel); },
  querySelectorAll(sel) { return queryAll(this, sel); },
  getElementById(id) { return findById(this, String(id)); },
};
function mixin(cls, ...mixins) {
  for (const m of mixins) for (const k of Reflect.ownKeys(m)) Object.defineProperty(cls.prototype, k, Object.getOwnPropertyDescriptor(m, k)), Object.defineProperty(cls.prototype, k, { enumerable: false });
}
function findById(root, id) {
  if (!root._c) return null;
  for (const c of root._c) {
    if (c.nodeType !== 1) continue;
    if (c._a && c._a.get('id') === id) return c;
    const f = findById(c, id);
    if (f) return f;
  }
  return null;
}

class CharacterData extends Node {
  constructor(data) { super(); this._d = data === undefined ? '' : String(data); }
  get data() { return this._d; }
  set data(v) { const old = this._d; this._d = String(v); changed(this, { type: 'characterData', target: this, oldValue: old }); }
  get nodeValue() { return this._d; }
  set nodeValue(v) { this.data = v; }
  get textContent() { return this._d; }
  set textContent(v) { this.data = v; }
  get length() { return this._d.length; }
  substringData(o, c) { return this._d.substr(o, c); }
  appendData(s) { this.data = this._d + s; }
  insertData(o, s) { this.data = this._d.slice(0, o) + s + this._d.slice(o); }
  deleteData(o, c) { this.data = this._d.slice(0, o) + this._d.slice(o + c); }
  replaceData(o, c, s) { this.data = this._d.slice(0, o) + s + this._d.slice(o + c); }
  get nextElementSibling() { let n = this._next; while (n && n.nodeType !== 1) n = n._next; return n; }
  get previousElementSibling() { let n = this._prev; while (n && n.nodeType !== 1) n = n._prev; return n; }
}
mixin(CharacterData, ChildNodeMixin);
class Text extends CharacterData {
  constructor(data) { super(data); }
  get nodeType() { return 3; }
  get nodeName() { return '#text'; }
  get wholeText() { return this._d; }
  splitText(offset) {
    const rest = this.ownerDocument.createTextNode(this._d.slice(offset));
    this.data = this._d.slice(0, offset);
    if (this._p) this._p.insertBefore(rest, this._next);
    return rest;
  }
}
class CDATASection extends Text { get nodeType() { return 4; } get nodeName() { return '#cdata-section'; } }
class Comment extends CharacterData {
  constructor(data) { super(data); }
  get nodeType() { return 8; }
  get nodeName() { return '#comment'; }
}
class ProcessingInstruction extends CharacterData { get nodeType() { return 7; } get nodeName() { return this.target; } }
class DocumentType extends Node {
  constructor(name, publicId, systemId) { super(); this.name = name || 'html'; this.publicId = publicId || ''; this.systemId = systemId || ''; }
  get nodeType() { return 10; }
  get nodeName() { return this.name; }
}
mixin(DocumentType, ChildNodeMixin);
class DocumentFragment extends Node {
  get nodeType() { return 11; }
  get nodeName() { return '#document-fragment'; }
  getElementById(id) { return findById(this, String(id)); }
}
mixin(DocumentFragment, ParentNodeMixin);
class ShadowRoot extends DocumentFragment {
  constructor(host, mode) { super(); this.host = host; this.mode = mode || 'open'; this.adoptedStyleSheets = []; this.delegatesFocus = false; this.slotAssignment = 'named'; }
  get innerHTML() { return serializeChildren(this); }
  set innerHTML(h) { setInnerHTML(this, h); }
  get activeElement() { return null; }
  get styleSheets() { return []; }
}
for (const [n, c] of Object.entries({ Text, Comment, CDATASection, ProcessingInstruction, DocumentType, DocumentFragment, ShadowRoot, CharacterData }))
  publish(n, c);

// ---- Attributes ----
class Attr {
  constructor(el, name, value, ns) { this.ownerElement = el; this.name = name; this.localName = name.includes(':') ? name.split(':')[1] : name; this.namespaceURI = ns || null; this.prefix = name.includes(':') ? name.split(':')[0] : null; this.specified = true; this._v = value; }
  get value() { return this.ownerElement && this.ownerElement._a ? (this.ownerElement._a.get(this.name) ?? this._v) : this._v; }
  set value(v) { if (this.ownerElement) this.ownerElement.setAttribute(this.name, v); else this._v = String(v); }
  get nodeValue() { return this.value; }
  get textContent() { return this.value; }
  get nodeName() { return this.name; }
  get nodeType() { return 2; }
}
publish('Attr', Attr);
class NamedNodeMap {
  constructor(el) { this._el = el; }
  get length() { return this._el._a ? this._el._a.size : 0; }
  item(i) { return this._list()[i] || null; }
  _list() { const out = []; if (this._el._a) for (const [k, v] of this._el._a) out.push(new Attr(this._el, k, v)); return out; }
  getNamedItem(n) { return this._el.hasAttribute(n) ? new Attr(this._el, this._el._norm(n), this._el.getAttribute(n)) : null; }
  setNamedItem(a) { this._el.setAttribute(a.name, a.value); return null; }
  removeNamedItem(n) { const a = this.getNamedItem(n); this._el.removeAttribute(n); return a; }
  [Symbol.iterator]() { return this._list()[Symbol.iterator](); }
}
publish('NamedNodeMap', NamedNodeMap);

// ---- classList ----
class DOMTokenList {
  constructor(el, attr) { this._el = el; this._attr = attr; }
  _tokens() { const v = this._el.getAttribute(this._attr); return v ? v.split(/[ \t\n\f\r]+/).filter(Boolean) : []; }
  _set(list) { this._el.setAttribute(this._attr, list.join(' ')); }
  get length() { return this._tokens().length; }
  get value() { return this._el.getAttribute(this._attr) || ''; }
  set value(v) { this._el.setAttribute(this._attr, v); }
  item(i) { return this._tokens()[i] ?? null; }
  contains(t) { return this._tokens().includes(String(t)); }
  add(...ts) {
    const l = this._tokens();
    for (const t of ts) { if (/\s/.test(t) || t === '') throw new DOMException("The token provided ('" + t + "') contains HTML space characters, which are not valid in tokens.", 'InvalidCharacterError'); if (!l.includes(String(t))) l.push(String(t)); }
    this._set(l);
  }
  remove(...ts) { const set = new Set(ts.map(String)); const l = this._tokens(); const n = l.filter(x => !set.has(x)); if (n.length !== l.length || this._el.hasAttribute(this._attr)) this._set(n); }
  toggle(t, force) {
    t = String(t);
    const has = this.contains(t);
    if (force === undefined ? has : !force) { if (has) this.remove(t); return false; }
    if (!has) this.add(t);
    return true;
  }
  replace(a, b) { const l = this._tokens(); const i = l.indexOf(String(a)); if (i < 0) return false; l[i] = String(b); this._set([...new Set(l)]); return true; }
  supports() { return true; }
  forEach(fn, thisArg) { this._tokens().forEach((t, i) => fn.call(thisArg, t, i, this)); }
  keys() { return this._tokens().keys(); }
  values() { return this._tokens().values(); }
  entries() { return this._tokens().entries(); }
  [Symbol.iterator]() { return this._tokens()[Symbol.iterator](); }
  toString() { return this.value; }
}
publish('DOMTokenList', DOMTokenList);

// ---- style ----
function parseStyle(text) {
  const map = new Map();
  let i = 0, start = 0, depth = 0, quote = '';
  const parts = [];
  for (; i < text.length; i++) {
    const c = text[i];
    if (quote) { if (c === quote) quote = ''; continue; }
    if (c === '"' || c === "'") quote = c;
    else if (c === '(') depth++;
    else if (c === ')') depth--;
    else if (c === ';' && depth <= 0) { parts.push(text.slice(start, i)); start = i + 1; }
  }
  parts.push(text.slice(start));
  for (const p of parts) {
    const k = p.indexOf(':');
    if (k < 0) continue;
    const name = p.slice(0, k).trim().toLowerCase();
    const value = p.slice(k + 1).trim();
    if (name) map.set(name, value);
  }
  return map;
}
const kebab = (s) => s.replace(/[A-Z]/g, m => '-' + m.toLowerCase()).replace(/^(webkit|moz|ms)-/, '-$1-');
const camel = (s) => s.replace(/-([a-z])/g, (m, c) => c.toUpperCase());
function makeStyle(el) {
  const map = () => {
    if (!el._sm) el._sm = parseStyle(el.getAttribute('style') || '');
    return el._sm;
  };
  const sync = () => {
    let t = '';
    for (const [k, v] of el._sm) t += k + ': ' + v + '; ';
    el._syncing = true;
    el.setAttribute('style', t.trim());
    el._syncing = false;
  };
  const api = {
    getPropertyValue: (n) => map().get(kebab(String(n)).toLowerCase()) || '',
    setProperty: (n, v, pr) => { n = String(n); if (!n.startsWith('--')) n = kebab(n).toLowerCase(); if (v === '' || v == null) map().delete(n); else map().set(n, String(v) + (pr ? ' !important' : '')); sync(); },
    removeProperty: (n) => { n = String(n); if (!n.startsWith('--')) n = kebab(n).toLowerCase(); const old = map().get(n) || ''; map().delete(n); sync(); return old; },
    getPropertyPriority: () => '',
    item: (i) => [...map().keys()][i] || '',
    get length() { return map().size; },
    get cssText() { let t = ''; for (const [k, v] of map()) t += k + ': ' + v + '; '; return t.trim(); },
    set cssText(v) { el.setAttribute('style', String(v)); },
    get parentRule() { return null; },
    get cssFloat() { return map().get('float') || ''; },
    set cssFloat(v) { api.setProperty('float', v); },
  };
  return new Proxy(api, {
    get(t, p) {
      if (typeof p === 'symbol') return p === Symbol.iterator ? function* () { yield* map().keys(); } : undefined;
      if (p in t) return t[p];
      if (/^\d+$/.test(p)) return [...map().keys()][+p];
      if (p === 'toString') return () => api.cssText;
      return map().get(p.startsWith('--') ? p : kebab(p)) || '';
    },
    set(t, p, v) {
      if (typeof p === 'symbol') return true;
      if (p === 'cssText') { api.cssText = v; return true; }
      if (p === 'cssFloat') { api.setProperty('float', v); return true; }
      api.setProperty(p, v);
      return true;
    },
    has(t, p) { return p in t || (typeof p === 'string' && map().has(kebab(p))); },
    ownKeys() { return [...map().keys()].map(camel); },
    getOwnPropertyDescriptor(t, p) { return typeof p === 'string' && map().has(kebab(p)) ? { value: map().get(kebab(p)), enumerable: true, configurable: true, writable: true } : undefined; },
  });
}
publish('CSSStyleDeclaration', function CSSStyleDeclaration() {});
function makeDataset(el) {
  const key = (p) => 'data-' + p.replace(/[A-Z]/g, m => '-' + m.toLowerCase());
  return new Proxy({}, {
    get(t, p) { return typeof p === 'string' && el.hasAttribute(key(p)) ? el.getAttribute(key(p)) : undefined; },
    set(t, p, v) { el.setAttribute(key(String(p)), String(v)); return true; },
    has(t, p) { return typeof p === 'string' && el.hasAttribute(key(p)); },
    deleteProperty(t, p) { el.removeAttribute(key(String(p))); return true; },
    ownKeys() { const out = []; if (el._a) for (const k of el._a.keys()) if (k.startsWith('data-')) out.push(camel(k.slice(5))); return out; },
    getOwnPropertyDescriptor(t, p) { return typeof p === 'string' && el.hasAttribute(key(p)) ? { value: el.getAttribute(key(p)), enumerable: true, configurable: true, writable: true } : undefined; },
  });
}

// ---- Elements ----
const voidTags = new Set('area base br col embed hr img input link meta param source track wbr basefont bgsound frame keygen'.split(' '));
K.voidTags = voidTags;

class Element extends Node {
  constructor() {
    super();
    this._a = null;
    this.localName = '';
    this.namespaceURI = HTML_NS;
    this.prefix = null;
    this._cl = null; this._st = null; this._sm = null; this._ds = null; this._sr = null;
    const it = K.initTag;
    if (it) { K.initTag = null; this.localName = it.localName; this.namespaceURI = it.ns; this.prefix = it.prefix || null; this.ownerDocument = it.doc; }
  }
  get nodeType() { return 1; }
  get tagName() { return this.namespaceURI === HTML_NS && this.ownerDocument && this.ownerDocument._html !== false ? this.localName.toUpperCase() : (this.prefix ? this.prefix + ':' : '') + this.localName; }
  get nodeName() { return this.tagName; }
  get id() { return this.getAttribute('id') || ''; }
  set id(v) { this.setAttribute('id', v); }
  get className() { return this.getAttribute('class') || ''; }
  set className(v) { this.setAttribute('class', v); }
  get classList() { return this._cl || (this._cl = new DOMTokenList(this, 'class')); }
  set classList(v) { this.setAttribute('class', v); }
  get slot() { return this.getAttribute('slot') || ''; }
  set slot(v) { this.setAttribute('slot', v); }
  get part() { return new DOMTokenList(this, 'part'); }
  get attributes() { return new NamedNodeMap(this); }
  _norm(name) { name = String(name); return this.namespaceURI === HTML_NS && (!this.ownerDocument || this.ownerDocument._html !== false) ? name.toLowerCase() : name; }
  getAttribute(name) { if (!this._a) return null; const v = this._a.get(this._norm(name)); return v === undefined ? null : v; }
  getAttributeNS(ns, local) { return this.getAttribute(local); }
  getAttributeNode(name) { return this.hasAttribute(name) ? new Attr(this, this._norm(name), this.getAttribute(name)) : null; }
  hasAttribute(name) { return !!this._a && this._a.has(this._norm(name)); }
  hasAttributeNS(ns, local) { return this.hasAttribute(local); }
  hasAttributes() { return !!this._a && this._a.size > 0; }
  getAttributeNames() { return this._a ? [...this._a.keys()] : []; }
  setAttribute(name, value) {
    name = String(name);
    if (!/^[A-Za-z_:][-A-Za-z0-9_:.]*$/.test(name) && !/^[^\s\0"'>/=]+$/.test(name)) throw new DOMException("String contains an invalid character", 'InvalidCharacterError');
    name = this._norm(name);
    value = String(value);
    if (!this._a) this._a = new Map();
    const old = this._a.has(name) ? this._a.get(name) : null;
    this._a.set(name, value);
    if (name === 'style' && !this._syncing) this._sm = null;
    this._attrChanged(name, old, value);
  }
  setAttributeNS(ns, name, value) { this.setAttribute(name, value); }
  setAttributeNode(a) { this.setAttribute(a.name, a.value); return null; }
  removeAttribute(name) {
    name = this._norm(name);
    if (!this._a || !this._a.has(name)) return;
    const old = this._a.get(name);
    this._a.delete(name);
    if (name === 'style') this._sm = null;
    this._attrChanged(name, old, null);
  }
  removeAttributeNS(ns, local) { this.removeAttribute(local); }
  removeAttributeNode(a) { this.removeAttribute(a.name); return a; }
  toggleAttribute(name, force) {
    const has = this.hasAttribute(name);
    if (force === undefined ? has : !force) { if (has) this.removeAttribute(name); return false; }
    if (!has) this.setAttribute(name, '');
    return true;
  }
  _attrChanged(name, old, value) {
    changed(this, { type: 'attributes', target: this, attributeName: name, oldValue: old });
    if (name.length > 2 && name[0] === 'o' && name[1] === 'n' && value !== null && handlerNames.includes(name.slice(2))) {
      try { const f = new Function('event', value); this[name] = function (e) { return f.call(this, e); }; } catch (e) { reportError(e); }
    }
    if (this._observed && this._observed.includes(name) && this.attributeChangedCallback) {
      try { this.attributeChangedCallback(name, old, value); } catch (e) { reportError(e); }
    }
    if (this._attrHook) this._attrHook(name, old, value);
  }
  get style() { return this._st || (this._st = makeStyle(this)); }
  set style(v) { this.setAttribute('style', v); }
  get dataset() { return this._ds || (this._ds = makeDataset(this)); }
  get innerHTML() { return serializeChildren(this); }
  set innerHTML(h) { setInnerHTML(this, h); }
  get outerHTML() { return serializeNode(this); }
  set outerHTML(h) {
    const p = this._p;
    if (!p) throw new DOMException('This element has no parent node.', 'NoModificationAllowedError');
    const frag = parseFragment(String(h), p.nodeType === 1 ? p : this.ownerDocument.body || this.ownerDocument.documentElement, this.ownerDocument);
    p.replaceChild(frag, this);
  }
  insertAdjacentHTML(where, html) {
    const ctx = (where === 'beforebegin' || where === 'afterend') ? this._p : this;
    if (!ctx) throw new DOMException('No parent', 'NoModificationAllowedError');
    const frag = parseFragment(String(html), ctx.nodeType === 1 ? ctx : this.ownerDocument.body, this.ownerDocument);
    this.insertAdjacentElement(where, frag);
  }
  insertAdjacentElement(where, el) {
    switch (String(where).toLowerCase()) {
      case 'beforebegin': if (!this._p) return null; this._p.insertBefore(el, this); break;
      case 'afterbegin': this.insertBefore(el, this.firstChild); break;
      case 'beforeend': this.appendChild(el); break;
      case 'afterend': if (!this._p) return null; this._p.insertBefore(el, this._next); break;
      default: throw new DOMException("The value provided ('" + where + "') is not one of 'beforeBegin', 'afterBegin', 'beforeEnd', or 'afterEnd'.", 'SyntaxError');
    }
    return el;
  }
  insertAdjacentText(where, text) { this.insertAdjacentElement(where, this.ownerDocument.createTextNode(String(text))); }
  matches(sel) { return matchesSelector(this, sel); }
  webkitMatchesSelector(sel) { return matchesSelector(this, sel); }
  closest(sel) { for (let n = this; n && n.nodeType === 1; n = n._p) if (matchesSelector(n, sel)) return n; return null; }
  getElementsByTagName(tag) {
    tag = String(tag);
    const all = tag === '*', lower = tag.toLowerCase();
    return collect(this, e => all || e.localName === lower || e.localName === tag || e.tagName === tag);
  }
  getElementsByTagNameNS(ns, tag) { return this.getElementsByTagName(tag); }
  getElementsByClassName(names) {
    const want = String(names).split(/\s+/).filter(Boolean);
    return collect(this, e => { const c = (e.getAttribute('class') || '').split(/\s+/); return want.every(w => c.includes(w)); });
  }
  get nextElementSibling() { let n = this._next; while (n && n.nodeType !== 1) n = n._next; return n; }
  get previousElementSibling() { let n = this._prev; while (n && n.nodeType !== 1) n = n._prev; return n; }
  attachShadow(init) {
    if (this._sr) throw new DOMException('Shadow root already attached', 'NotSupportedError');
    const sr = new ShadowRoot(this, init && init.mode);
    sr.ownerDocument = this.ownerDocument;
    this._sr = sr;
    return sr;
  }
  get shadowRoot() { return this._sr && this._sr.mode === 'open' ? this._sr : null; }
  getBoundingClientRect() { return { x: 0, y: 0, left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0, toJSON() { return this; } }; }
  getClientRects() { return []; }
  get clientWidth() { return 0; } get clientHeight() { return 0; } get clientTop() { return 0; } get clientLeft() { return 0; }
  get scrollWidth() { return 0; } get scrollHeight() { return 0; }
  get scrollTop() { return this._st0 || 0; } set scrollTop(v) { this._st0 = Number(v) || 0; }
  get scrollLeft() { return this._sl0 || 0; } set scrollLeft(v) { this._sl0 = Number(v) || 0; }
  scrollIntoView() {} scrollTo() {} scrollBy() {} scroll() {} scrollIntoViewIfNeeded() {}
  getAnimations() { return []; }
  animate() { return new Animation(); }
  checkVisibility() { return true; }
  setPointerCapture() {} releasePointerCapture() {} hasPointerCapture() { return false; }
  requestFullscreen() { return Promise.reject(new TypeError('Fullscreen not supported')); }
  requestPointerLock() {}
  computedStyleMap() { return new Map(); }
  setHTMLUnsafe(h) { setInnerHTML(this, h); }
  toggleDispatch() {}
  get ariaLabel() { return this.getAttribute('aria-label'); }
  set ariaLabel(v) { this.setAttribute('aria-label', v); }
  get role() { return this.getAttribute('role'); }
  set role(v) { this.setAttribute('role', v); }
}
mixin(Element, ChildNodeMixin, ParentNodeMixin);
publish('Element', Element);
class Animation extends EventTarget {
  constructor() { super(); this.playState = 'finished'; this.finished = Promise.resolve(this); this.ready = Promise.resolve(this); this.currentTime = 0; this.effect = null; this.id = ''; }
  play() {} pause() {} cancel() {} finish() {} reverse() {} commitStyles() {}
}
publish('Animation', Animation);

function collect(root, test) {
  const out = new NodeList();
  const walk = (n) => {
    if (!n._c) return;
    for (const c of n._c) {
      if (c.nodeType !== 1) continue;
      if (test(c)) out.push(c);
      walk(c);
    }
  };
  walk(root);
  return out;
}
K.collect = collect;

// Cloning
function cloneNodeImpl(node, deep, doc) {
  let copy;
  switch (node.nodeType) {
    case 1: {
      copy = doc._create(node.localName, node.namespaceURI, node.prefix);
      if (node._a) for (const [k, v] of node._a) { if (!copy._a) copy._a = new Map(); copy._a.set(k, v); }
      if (node._cloneHook) node._cloneHook(copy);
      if (node._a) for (const [k, v] of node._a) copy._attrChanged && copy._attrHook && copy._attrHook(k, null, v);
      break;
    }
    case 3: copy = doc.createTextNode(node._d); break;
    case 4: copy = new CDATASection(node._d); break;
    case 8: copy = doc.createComment(node._d); break;
    case 10: copy = new DocumentType(node.name, node.publicId, node.systemId); break;
    case 11: copy = doc.createDocumentFragment(); break;
    case 9: copy = new HTMLDocument(); break;
    default: throw new DOMException('Cannot clone', 'NotSupportedError');
  }
  if (deep && node._c) {
    for (const c of node._c) copy.appendChild(cloneNodeImpl(c, true, doc));
  }
  return copy;
}

// MutationObserver
class MutationObserver {
  constructor(cb) { this._cb = cb; this._targets = []; this._records = []; this._queued = false; }
  observe(target, opts) {
    opts = opts || {};
    if (!opts.childList && !opts.attributes && !opts.characterData) {
      if (opts.attributeFilter || opts.attributeOldValue) opts.attributes = true;
      if (opts.characterDataOldValue) opts.characterData = true;
    }
    this._targets = this._targets.filter(t => t.target !== target);
    this._targets.push({ target, opts });
    observers.add(this);
  }
  disconnect() { this._targets = []; this._records = []; observers.delete(this); }
  takeRecords() { const r = this._records; this._records = []; return r; }
}
function queueRecord(target, rec) {
  for (const ob of observers) {
    for (const { target: t, opts } of ob._targets) {
      if (target !== t && !(opts.subtree && t.contains(target))) continue;
      if (rec.type === 'childList' && !opts.childList) continue;
      if (rec.type === 'attributes' && (!opts.attributes || (opts.attributeFilter && !opts.attributeFilter.includes(rec.attributeName)))) continue;
      if (rec.type === 'characterData' && !opts.characterData) continue;
      const r = Object.assign({ addedNodes: [], removedNodes: [], previousSibling: null, nextSibling: null, attributeName: null, attributeNamespace: null, oldValue: null }, rec);
      if (rec.type === 'attributes' && !opts.attributeOldValue) r.oldValue = null;
      if (rec.type === 'characterData' && !opts.characterDataOldValue) r.oldValue = null;
      ob._records.push(r);
      if (!ob._queued) {
        ob._queued = true;
        Promise.resolve().then(() => {
          ob._queued = false;
          const recs = ob.takeRecords();
          if (recs.length) { try { ob._cb.call(ob, recs, ob); } catch (e) { reportError(e); } }
        });
      }
      break;
    }
  }
}
publish('MutationObserver', MutationObserver);
