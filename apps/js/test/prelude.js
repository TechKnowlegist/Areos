(function (N) { 'use strict';
// 00_base.js - the start of AREOS's web page environment: helpers, the
// console, errors, timers and events. (All the prelude files are joined into
// one function, so they share these names.)

const G = globalThis;
const K = {};                       // internal things shared between the files
const hid = (obj, name, value) => Object.defineProperty(obj, name, { value, writable: true, configurable: true, enumerable: false });
const publish = (name, value) => hid(G, name, value);
const getter = (proto, name, fn, set) => Object.defineProperty(proto, name, { get: fn, set, configurable: true, enumerable: true });
const startTime = N.now();
const now = () => N.now() - startTime;
K.now = now;

// ---- Showing values as text (for the console) ----
function inspect(v, depth = 2, seen = []) {
  switch (typeof v) {
    case 'string': return depth < 2 || seen.length ? JSON.stringify(v) : v;
    case 'undefined': return 'undefined';
    case 'function': return '[Function' + (v.name ? ': ' + v.name : ' (anonymous)') + ']';
    case 'symbol': return v.toString();
    case 'bigint': return v + 'n';
    case 'object': break;
    default: return String(v);
  }
  if (v === null) return 'null';
  if (seen.includes(v)) return '[Circular]';
  if (v instanceof Error) return v.stack ? String(v.stack) : String(v);
  if (v instanceof Node && v.nodeType === 1) return '<' + v.localName + (v.id ? ' id="' + v.id + '"' : '') + '>';
  if (v instanceof Node) return '#' + v.nodeName;
  if (v instanceof Date) return v.toISOString();
  if (v instanceof RegExp) return String(v);
  if (depth < 0) return Array.isArray(v) ? '[Array]' : '[Object]';
  seen = seen.concat([v]);
  try {
    if (Array.isArray(v)) {
      const items = v.slice(0, 20).map(x => inspect(x, depth - 1, seen));
      if (v.length > 20) items.push('... ' + (v.length - 20) + ' more');
      return '[' + items.join(', ') + ']';
    }
    if (v instanceof Map) return 'Map(' + v.size + ') {' + [...v].slice(0, 10).map(([a, b]) => inspect(a, depth - 1, seen) + ' => ' + inspect(b, depth - 1, seen)).join(', ') + '}';
    if (v instanceof Set) return 'Set(' + v.size + ') {' + [...v].slice(0, 10).map(a => inspect(a, depth - 1, seen)).join(', ') + '}';
    const keys = Object.keys(v).slice(0, 20);
    const name = v.constructor && v.constructor !== Object ? v.constructor.name + ' ' : '';
    return name + '{' + keys.map(k => k + ': ' + inspect(v[k], depth - 1, seen)).join(', ') + '}';
  } catch (e) { return '[Object]'; }
}
function formatLog(args) {
  let out = '';
  let rest = args;
  if (typeof args[0] === 'string' && args[0].indexOf('%') >= 0 && args.length > 1) {
    let i = 1;
    out = args[0].replace(/%([sdifoOjc%])/g, (m, c) => {
      if (c === '%') return '%';
      if (i >= args.length) return m;
      const a = args[i++];
      switch (c) {
        case 's': return typeof a === 'string' ? a : inspect(a, 0);
        case 'd': case 'i': return String(typeof a === 'number' ? Math.trunc(a) : NaN);
        case 'f': return String(Number(a));
        case 'c': return '';
        default: return inspect(a, 2);
      }
    });
    rest = args.slice(i);
    if (rest.length) out += ' ';
  } else if (args.length) {
    out = typeof args[0] === 'string' ? args[0] : inspect(args[0]);
    rest = args.slice(1);
    if (rest.length) out += ' ';
  }
  return out + rest.map(a => typeof a === 'string' ? a : inspect(a)).join(' ');
}
K.inspect = inspect;

// ---- The console ----
const consoleLines = [];
function logLine(level, text) {
  consoleLines.push(['log', 'info', 'warn', 'error'][level] + ': ' + text);
  if (consoleLines.length > 300) consoleLines.shift();
  N.log(level, text);
}
K.logLine = logLine;
K.consoleText = () => consoleLines.join('\n');
const counts = {}, timersLog = {};
let groupIndent = '';
const con = {
  log: (...a) => logLine(0, groupIndent + formatLog(a)),
  info: (...a) => logLine(1, groupIndent + formatLog(a)),
  debug: (...a) => logLine(0, groupIndent + formatLog(a)),
  warn: (...a) => logLine(2, groupIndent + formatLog(a)),
  error: (...a) => logLine(3, groupIndent + formatLog(a)),
  trace: (...a) => logLine(0, groupIndent + 'Trace: ' + formatLog(a)),
  assert: (c, ...a) => { if (!c) logLine(3, 'Assertion failed: ' + formatLog(a)); },
  dir: (a) => logLine(0, groupIndent + inspect(a)),
  dirxml: (a) => logLine(0, groupIndent + inspect(a)),
  table: (a) => logLine(0, groupIndent + inspect(a)),
  group: (...a) => { if (a.length) con.log(...a); groupIndent += '  '; },
  groupCollapsed: (...a) => { if (a.length) con.log(...a); groupIndent += '  '; },
  groupEnd: () => { groupIndent = groupIndent.slice(2); },
  count: (l = 'default') => { counts[l] = (counts[l] || 0) + 1; con.log(l + ': ' + counts[l]); },
  countReset: (l = 'default') => { counts[l] = 0; },
  time: (l = 'default') => { timersLog[l] = now(); },
  timeEnd: (l = 'default') => { con.log(l + ': ' + (now() - (timersLog[l] || 0)).toFixed(1) + 'ms'); },
  timeLog: (l = 'default') => { con.log(l + ': ' + (now() - (timersLog[l] || 0)).toFixed(1) + 'ms'); },
  clear: () => {},
  profile: () => {}, profileEnd: () => {}, timeStamp: () => {},
};
publish('console', con);

// ---- Errors ----
class DOMException extends Error {
  constructor(message = '', name = 'Error') {
    super(message);
    hid(this, 'name', name);
    hid(this, 'code', DOMException.codes[name] || 0);
  }
}
DOMException.codes = { IndexSizeError: 1, HierarchyRequestError: 3, WrongDocumentError: 4, InvalidCharacterError: 5, NoModificationAllowedError: 7, NotFoundError: 8, NotSupportedError: 9, InvalidStateError: 11, SyntaxError: 12, InvalidModificationError: 13, NamespaceError: 14, InvalidAccessError: 15, SecurityError: 18, NetworkError: 19, AbortError: 20, TimeoutError: 23, DataCloneError: 25 };
publish('DOMException', DOMException);
function reportError(e) {
  let text = (e && e.stack) ? String(e.stack) : String(e);
  if (e && e.name && e.message !== undefined && !text.includes(e.name)) text = e.name + ': ' + e.message + '\n' + text;
  logLine(3, 'Uncaught ' + text);
  try {
    const ev = new ErrorEvent('error', { message: e && e.message ? String(e.message) : String(e), error: e, cancelable: true });
    G.dispatchEvent(ev);
  } catch (e2) { /* never loop */ }
}
K.reportError = reportError;
publish('reportError', reportError);

// ---- Events ----
class Event {
  constructor(type, init) {
    init = init || {};
    this.type = String(type);
    this.bubbles = !!init.bubbles;
    this.cancelable = !!init.cancelable;
    this.composed = !!init.composed;
    this.defaultPrevented = false;
    this.target = null;
    this.currentTarget = null;
    this.srcElement = null;
    this.eventPhase = 0;
    this.timeStamp = now();
    this.isTrusted = false;
    this._stop = false;
    this._stopNow = false;
    this._path = null;
    this._passive = false;
  }
  preventDefault() { if (this.cancelable && !this._passive) this.defaultPrevented = true; }
  stopPropagation() { this._stop = true; }
  stopImmediatePropagation() { this._stop = true; this._stopNow = true; }
  composedPath() { return this._path ? this._path.slice() : []; }
  get cancelBubble() { return this._stop; }
  set cancelBubble(v) { if (v) this._stop = true; }
  get returnValue() { return !this.defaultPrevented; }
  set returnValue(v) { if (!v) this.preventDefault(); }
  initEvent(type, bubbles, cancelable) { this.type = type; this.bubbles = !!bubbles; this.cancelable = !!cancelable; }
}
Event.NONE = 0; Event.CAPTURING_PHASE = 1; Event.AT_TARGET = 2; Event.BUBBLING_PHASE = 3;
class CustomEvent extends Event { constructor(t, i) { super(t, i); this.detail = i && i.detail !== undefined ? i.detail : null; } }
class UIEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.view = i.view || null; this.detail = i.detail || 0; } }
class MouseEvent extends UIEvent {
  constructor(t, i) {
    super(t, i); i = i || {};
    this.screenX = i.screenX || 0; this.screenY = i.screenY || 0; this.clientX = i.clientX || 0; this.clientY = i.clientY || 0;
    this.pageX = i.pageX || this.clientX; this.pageY = i.pageY || this.clientY; this.offsetX = 0; this.offsetY = 0; this.x = this.clientX; this.y = this.clientY;
    this.ctrlKey = !!i.ctrlKey; this.shiftKey = !!i.shiftKey; this.altKey = !!i.altKey; this.metaKey = !!i.metaKey;
    this.button = i.button || 0; this.buttons = i.buttons || 0; this.relatedTarget = i.relatedTarget || null;
  }
  getModifierState() { return false; }
}
class PointerEvent extends MouseEvent {
  constructor(t, i) { super(t, i); i = i || {}; this.pointerId = i.pointerId || 1; this.pointerType = i.pointerType || 'mouse'; this.isPrimary = i.isPrimary !== false; this.width = 1; this.height = 1; this.pressure = 0; }
}
class WheelEvent extends MouseEvent { constructor(t, i) { super(t, i); i = i || {}; this.deltaX = i.deltaX || 0; this.deltaY = i.deltaY || 0; this.deltaZ = 0; this.deltaMode = 0; } }
class KeyboardEvent extends UIEvent {
  constructor(t, i) {
    super(t, i); i = i || {};
    this.key = i.key || ''; this.code = i.code || ''; this.location = i.location || 0; this.repeat = !!i.repeat; this.isComposing = false;
    this.ctrlKey = !!i.ctrlKey; this.shiftKey = !!i.shiftKey; this.altKey = !!i.altKey; this.metaKey = !!i.metaKey;
    this.keyCode = i.keyCode || 0; this.which = i.which || this.keyCode; this.charCode = i.charCode || 0;
  }
  getModifierState() { return false; }
}
class InputEvent extends UIEvent { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data === undefined ? null : i.data; this.inputType = i.inputType || ''; this.isComposing = false; } }
class FocusEvent extends UIEvent { constructor(t, i) { super(t, i); this.relatedTarget = (i && i.relatedTarget) || null; } }
class ErrorEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.message = i.message || ''; this.filename = i.filename || ''; this.lineno = i.lineno || 0; this.colno = i.colno || 0; this.error = i.error; } }
class PromiseRejectionEvent extends Event { constructor(t, i) { super(t, i); this.promise = i.promise; this.reason = i.reason; } }
class MessageEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.data = i.data; this.origin = i.origin || ''; this.source = i.source || null; this.ports = i.ports || []; this.lastEventId = ''; } }
class HashChangeEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.oldURL = i.oldURL || ''; this.newURL = i.newURL || ''; } }
class PopStateEvent extends Event { constructor(t, i) { super(t, i); this.state = i && i.state !== undefined ? i.state : null; } }
class ProgressEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.lengthComputable = !!i.lengthComputable; this.loaded = i.loaded || 0; this.total = i.total || 0; } }
class StorageEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.key = i.key || null; this.oldValue = i.oldValue || null; this.newValue = i.newValue || null; this.url = i.url || ''; this.storageArea = i.storageArea || null; } }
class TransitionEvent extends Event { constructor(t, i) { super(t, i); this.propertyName = (i && i.propertyName) || ''; this.elapsedTime = 0; } }
class AnimationEvent extends Event { constructor(t, i) { super(t, i); this.animationName = (i && i.animationName) || ''; this.elapsedTime = 0; } }
class ClipboardEvent extends Event { constructor(t, i) { super(t, i); this.clipboardData = (i && i.clipboardData) || null; } }
class DragEvent extends MouseEvent { constructor(t, i) { super(t, i); this.dataTransfer = (i && i.dataTransfer) || null; } }
class TouchEvent extends UIEvent { constructor(t, i) { super(t, i); this.touches = []; this.targetTouches = []; this.changedTouches = []; } }
class CompositionEvent extends UIEvent { constructor(t, i) { super(t, i); this.data = (i && i.data) || ''; } }
class SubmitEvent extends Event { constructor(t, i) { super(t, i); this.submitter = (i && i.submitter) || null; } }
class FormDataEvent extends Event { constructor(t, i) { super(t, i); this.formData = i && i.formData; } }
class BeforeUnloadEvent extends Event {}
class CloseEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.code = i.code || 0; this.reason = i.reason || ''; this.wasClean = !!i.wasClean; } }
class ToggleEvent extends Event { constructor(t, i) { super(t, i); i = i || {}; this.newState = i.newState || ''; this.oldState = i.oldState || ''; } }

const eventClasses = { Event, CustomEvent, UIEvent, MouseEvent, PointerEvent, WheelEvent, KeyboardEvent, InputEvent, FocusEvent, ErrorEvent,
  PromiseRejectionEvent, MessageEvent, HashChangeEvent, PopStateEvent, ProgressEvent, StorageEvent, TransitionEvent, AnimationEvent,
  ClipboardEvent, DragEvent, TouchEvent, CompositionEvent, SubmitEvent, FormDataEvent, BeforeUnloadEvent, CloseEvent, ToggleEvent };
for (const k of Object.keys(eventClasses)) publish(k, eventClasses[k]);

class EventTarget {
  constructor() { this._l = null; }
  addEventListener(type, fn, opts) {
    if (fn == null) return;
    const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
    const once = !!(opts && typeof opts === 'object' && opts.once);
    const passive = !!(opts && typeof opts === 'object' && opts.passive);
    const signal = opts && typeof opts === 'object' ? opts.signal : null;
    if (signal && signal.aborted) return;
    if (!this._l) this._l = new Map();
    let list = this._l.get(type);
    if (!list) this._l.set(type, list = []);
    for (const l of list) if (l.fn === fn && l.capture === capture) return;
    const rec = { fn, capture, once, passive };
    list.push(rec);
    if (signal) signal.addEventListener('abort', () => this.removeEventListener(type, fn, { capture }));
  }
  removeEventListener(type, fn, opts) {
    const capture = typeof opts === 'boolean' ? opts : !!(opts && opts.capture);
    const list = this._l && this._l.get(type);
    if (!list) return;
    for (let i = 0; i < list.length; i++) {
      if (list[i].fn === fn && list[i].capture === capture) { list[i].removed = true; list.splice(i, 1); return; }
    }
  }
  dispatchEvent(ev) {
    if (!(ev instanceof Event)) throw new TypeError('Argument is not an Event');
    const path = [];
    for (let t = this; t; t = t._eventParent ? t._eventParent() : null) path.push(t);
    ev.target = ev.srcElement = this;
    ev._path = path;
    ev._stop = ev._stopNow = false;
    // capture: from the outside in
    for (let i = path.length - 1; i > 0 && !ev._stop; i--) { ev.eventPhase = 1; fireListeners(path[i], ev, true); }
    if (!ev._stop) { ev.eventPhase = 2; fireListeners(path[0], ev, true); if (!ev._stopNow) fireListeners(path[0], ev, false); }
    if (ev.bubbles) for (let i = 1; i < path.length && !ev._stop; i++) { ev.eventPhase = 3; fireListeners(path[i], ev, false); }
    ev.eventPhase = 0;
    ev.currentTarget = null;
    return !ev.defaultPrevented;
  }
}
function fireListeners(target, ev, capturing) {
  const list = target._l && target._l.get(ev.type);
  if (!list || !list.length) return;
  ev.currentTarget = target;
  for (const rec of list.slice()) {
    if (rec.removed) continue;
    if (ev.eventPhase !== 2 && rec.capture !== capturing) continue;
    if (ev.eventPhase === 2 && rec.capture !== capturing) continue;
    if (rec.once) target.removeEventListener(ev.type, rec.fn, { capture: rec.capture });
    ev._passive = rec.passive;
    try {
      if (typeof rec.fn === 'function') rec.fn.call(target, ev);
      else if (rec.fn && typeof rec.fn.handleEvent === 'function') rec.fn.handleEvent(ev);
    } catch (e) { reportError(e); }
    ev._passive = false;
    if (ev._stopNow) break;
  }
}
// onclick = function... style handlers
const handlerNames = ('abort animationend animationiteration animationstart auxclick beforeinput blur cancel canplay change click close contextmenu copy cut dblclick drag dragend dragenter dragleave dragover dragstart drop error focus focusin focusout gotpointercapture input invalid keydown keypress keyup load loadeddata loadedmetadata loadstart lostpointercapture mousedown mouseenter mouseleave mousemove mouseout mouseover mouseup paste pause play playing pointercancel pointerdown pointerenter pointerleave pointermove pointerout pointerover pointerup progress ratechange reset resize scroll scrollend seeked seeking select selectstart stalled submit suspend timeupdate toggle touchcancel touchend touchmove touchstart transitionend transitionrun transitionstart volumechange waiting wheel ' +
  'afterprint beforeprint beforeunload hashchange languagechange message messageerror offline online pagehide pageshow popstate rejectionhandled storage unhandledrejection unload readystatechange visibilitychange fullscreenchange').split(' ');
function installHandlers(proto) {
  for (const name of handlerNames) {
    Object.defineProperty(proto, 'on' + name, {
      configurable: true, enumerable: true,
      get() { return (this._on && this._on[name]) || null; },
      set(fn) {
        if (!this._on) this._on = {};
        const had = typeof this._on[name] === 'function';
        this._on[name] = typeof fn === 'function' ? fn : null;
        if (!had && typeof fn === 'function' && !(this._onw && this._onw[name])) {
          if (!this._onw) this._onw = {};
          const self = this;
          this._onw[name] = true;
          this.addEventListener(name, function (ev) {
            const h = self._on && self._on[name];
            if (typeof h !== 'function') return;
            const r = h.call(self, ev);
            if (r === false) ev.preventDefault();
          });
        }
      },
    });
  }
}
K.installHandlers = installHandlers;
publish('EventTarget', EventTarget);

// ---- Timers ----
let timerSeq = 0, timerOrder = 0;
const timerList = [];
const rafList = [];
let nextFrame = 0;
function addTimer(fn, ms, args, repeat) {
  if (typeof fn !== 'function') { const code = String(fn); fn = () => (0, eval)(code); }
  ms = Math.max(0, Number(ms) || 0);
  const t = { id: ++timerSeq, due: now() + ms, ms: Math.max(ms, repeat ? 4 : 0), fn, args, repeat, order: ++timerOrder };
  timerList.push(t);
  return t.id;
}
publish('setTimeout', (fn, ms, ...args) => addTimer(fn, ms, args, false));
publish('setInterval', (fn, ms, ...args) => addTimer(fn, ms, args, true));
publish('setImmediate', (fn, ...args) => addTimer(fn, 0, args, false));
const clearTimer = (id) => { for (let i = 0; i < timerList.length; i++) if (timerList[i].id === id) { timerList.splice(i, 1); return; } };
publish('clearTimeout', clearTimer);
publish('clearInterval', clearTimer);
publish('clearImmediate', clearTimer);
publish('requestAnimationFrame', (fn) => { const id = ++timerSeq; rafList.push({ id, fn }); return id; });
publish('cancelAnimationFrame', (id) => { for (let i = 0; i < rafList.length; i++) if (rafList[i].id === id) { rafList.splice(i, 1); return; } });
publish('requestIdleCallback', (fn) => addTimer(() => fn({ didTimeout: false, timeRemaining: () => 40 }), 1, [], false));
publish('cancelIdleCallback', clearTimer);
publish('queueMicrotask', (fn) => { Promise.resolve().then(fn).catch(reportError); });
// Run what is due. Returns the wait (ms) until the next timer, or -1 for none.
K.runTimers = function (budget) {
  const t0 = now();
  let ran = 0;
  for (;;) {
    const t = now();
    if (rafList.length && t >= nextFrame) {
      nextFrame = t + 16;
      const list = rafList.splice(0, rafList.length);
      for (const r of list) { try { r.fn(t); } catch (e) { reportError(e); } N.microtasks(); }
      ran++;
    }
    let best = null;
    for (const x of timerList) if (x.due <= t && (!best || x.due < best.due || (x.due === best.due && x.order < best.order))) best = x;
    if (!best) break;
    if (best.repeat) { best.due = t + best.ms; best.order = ++timerOrder; } else timerList.splice(timerList.indexOf(best), 1);
    try { best.fn(...best.args); } catch (e) { reportError(e); }
    N.microtasks();
    if (++ran > 400 || now() - t0 > budget) break;
  }
  let wait = -1;
  const t = now();
  for (const x of timerList) { const w = Math.max(0, x.due - t); if (wait < 0 || w < wait) wait = w; }
  if (rafList.length) { const w = Math.max(0, nextFrame - t); if (wait < 0 || w < wait) wait = w; }
  return Math.ceil(wait);
};
K.hasTimers = () => timerList.length > 0 || rafList.length > 0;

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

// 12_html.js - the HTML element types: HTMLElement and the elements that do
// something (inputs, forms, links, images, scripts...).

const tagClasses = Object.create(null);
function defineTags(cls, tags) { for (const t of tags.split(' ')) tagClasses[t] = cls; }
const attrString = (proto, map) => { for (const p of Object.keys(map)) getter(proto, p, function () { return this.getAttribute(map[p]) || ''; }, function (v) { this.setAttribute(map[p], v); }); };
const attrBool = (proto, map) => { for (const p of Object.keys(map)) getter(proto, p, function () { return this.hasAttribute(map[p]); }, function (v) { if (v) this.setAttribute(map[p], ''); else this.removeAttribute(map[p]); }); };
const attrInt = (proto, map, dflt = 0) => { for (const p of Object.keys(map)) getter(proto, p, function () { const v = parseInt(this.getAttribute(map[p]), 10); return isNaN(v) ? dflt : v; }, function (v) { this.setAttribute(map[p], String(Math.trunc(Number(v)) || 0)); }); };
K.customByCtor = new Map();
K.customByName = new Map();

class HTMLElement extends Element {
  constructor() {
    if (!K.initTag) {
      const def = K.customByCtor.get(new.target);
      if (!def) throw new TypeError('Illegal constructor');
      K.initTag = { localName: def.name, ns: HTML_NS, doc: K.doc };
    }
    super();
  }
  get innerText() { return textOf(this); }
  set innerText(v) { this.textContent = v; }
  get outerText() { return textOf(this); }
  set outerText(v) { this.textContent = v; }
  get tabIndex() { const v = parseInt(this.getAttribute('tabindex'), 10); return isNaN(v) ? (isFocusable(this, true) ? 0 : -1) : v; }
  set tabIndex(v) { this.setAttribute('tabindex', String(v)); }
  get contentEditable() { return this.getAttribute('contenteditable') || 'inherit'; }
  set contentEditable(v) { this.setAttribute('contenteditable', v); }
  get isContentEditable() { const v = this.getAttribute('contenteditable'); return v === '' || v === 'true'; }
  get draggable() { return this.getAttribute('draggable') === 'true'; }
  set draggable(v) { this.setAttribute('draggable', v ? 'true' : 'false'); }
  get offsetWidth() { return 0; } get offsetHeight() { return 0; } get offsetTop() { return 0; } get offsetLeft() { return 0; }
  get offsetParent() { return null; }
  click() { if (this.disabled) return; dispatchClick(this); }
  focus() { focusElement(this); }
  blur() { if (K.doc._focus === this) blurCurrent(); }
  attachInternals() { return { setFormValue() {}, setValidity() {}, checkValidity: () => true, reportValidity: () => true, form: null, labels: [], states: new Set(), shadowRoot: this._sr }; }
  showPopover() {} hidePopover() {} togglePopover() { return false; }
  get isConnectedAndVisible() { return true; }
}
attrString(HTMLElement.prototype, { title: 'title', lang: 'lang', dir: 'dir', accessKey: 'accesskey', nonce: 'nonce', autocapitalize: 'autocapitalize', enterKeyHint: 'enterkeyhint', inputMode: 'inputmode', popover: 'popover' });
attrBool(HTMLElement.prototype, { hidden: 'hidden', inert: 'inert', autofocus: 'autofocus' });
getter(HTMLElement.prototype, 'spellcheck', function () { return this.getAttribute('spellcheck') !== 'false'; }, function (v) { this.setAttribute('spellcheck', v ? 'true' : 'false'); });
getter(HTMLElement.prototype, 'translate', function () { return this.getAttribute('translate') !== 'no'; }, function (v) { this.setAttribute('translate', v ? 'yes' : 'no'); });
installHandlers(Element.prototype);
installHandlers(HTMLElement.prototype);
publish('HTMLElement', HTMLElement);
class SVGElement extends Element {
  get className() { const v = this.getAttribute('class') || ''; return { baseVal: v, animVal: v }; }
  set className(v) { this.setAttribute('class', v); }
  get ownerSVGElement() { for (let n = this._p; n; n = n._p) if (n.localName === 'svg') return n; return null; }
  get viewportElement() { return this.ownerSVGElement; }
  focus() {} blur() {}
  getBBox() { return { x: 0, y: 0, width: 0, height: 0 }; }
  getCTM() { return null; } getScreenCTM() { return null; }
  getTotalLength() { return 0; }
  get tabIndex() { const v = parseInt(this.getAttribute('tabindex'), 10); return isNaN(v) ? -1 : v; }
  set tabIndex(v) { this.setAttribute('tabindex', v); }
  get style() { return Object.getOwnPropertyDescriptor(Element.prototype, 'style').get.call(this); }
}
installHandlers(SVGElement.prototype);
publish('SVGElement', SVGElement);
publish('SVGSVGElement', class SVGSVGElement extends SVGElement {});
publish('MathMLElement', class MathMLElement extends Element {});

function textOf(el) {
  // roughly what innerText gives: block elements start new lines
  let out = '';
  const blockish = /^(address|article|aside|blockquote|dd|details|div|dl|dt|fieldset|figcaption|figure|footer|form|h[1-6]|header|hr|li|main|nav|ol|p|pre|section|table|tr|ul|br)$/;
  const walk = (n) => {
    if (n.nodeType === 3) { out += n._d; return; }
    if (n.nodeType !== 1) return;
    if (n.localName === 'script' || n.localName === 'style' || n.localName === 'template') return;
    const isBlock = blockish.test(n.localName);
    if (isBlock && out && !out.endsWith('\n')) out += '\n';
    if (n.localName === 'br') { out += '\n'; return; }
    if (n._c) for (const c of n._c) walk(c);
    if (isBlock && out && !out.endsWith('\n')) out += '\n';
  };
  walk(el);
  return out.replace(/\n+$/, '');
}

// Which elements can have the keyboard focus.
function isFocusable(el, ignoreTabindex) {
  if (el.nodeType !== 1) return false;
  if (!ignoreTabindex && el.hasAttribute('tabindex')) return true;
  const n = el.localName;
  if (n === 'a' || n === 'area') return el.hasAttribute('href');
  if (n === 'input') return el.getAttribute('type') !== 'hidden' && !el.hasAttribute('disabled');
  if (n === 'button' || n === 'select' || n === 'textarea') return !el.hasAttribute('disabled');
  if (n === 'summary' || n === 'iframe') return true;
  if (el.isContentEditable) return true;
  return false;
}
function focusElement(el) {
  const doc = K.doc;
  if (!el.isConnected || !isFocusable(el)) return;
  const prev = doc._focus;
  if (prev === el) return;
  if (prev) blurCurrent(el);
  doc._focus = el;
  el.dispatchEvent(new FocusEvent('focus', { relatedTarget: prev }));
  el.dispatchEvent(new FocusEvent('focusin', { bubbles: true, relatedTarget: prev }));
}
function blurCurrent(next) {
  const doc = K.doc;
  const el = doc._focus;
  if (!el) return;
  doc._focus = null;
  el.dispatchEvent(new FocusEvent('blur', { relatedTarget: next || null }));
  el.dispatchEvent(new FocusEvent('focusout', { bubbles: true, relatedTarget: next || null }));
}
K.focusElement = focusElement;

// ---- Plain elements ----
class HTMLUnknownElement extends HTMLElement {}
class HTMLDivElement extends HTMLElement {}
class HTMLSpanElement extends HTMLElement {}
class HTMLParagraphElement extends HTMLElement {}
class HTMLHeadingElement extends HTMLElement {}
class HTMLBodyElement extends HTMLElement {}
class HTMLHeadElement extends HTMLElement {}
class HTMLHtmlElement extends HTMLElement {}
class HTMLBRElement extends HTMLElement {}
class HTMLHRElement extends HTMLElement {}
class HTMLPreElement extends HTMLElement {}
class HTMLQuoteElement extends HTMLElement {}
class HTMLUListElement extends HTMLElement {}
class HTMLOListElement extends HTMLElement {}
class HTMLLIElement extends HTMLElement {}
class HTMLDListElement extends HTMLElement {}
class HTMLTableElement extends HTMLElement {
  get rows() { return collect(this, e => e.localName === 'tr'); }
  get tBodies() { return collect(this, e => e.localName === 'tbody'); }
  get tHead() { return this.querySelector('thead'); }
  get tFoot() { return this.querySelector('tfoot'); }
  get caption() { return this.querySelector('caption'); }
  insertRow(i) { const tr = this.ownerDocument.createElement('tr'); (this.querySelector('tbody') || this).appendChild(tr); return tr; }
}
class HTMLTableSectionElement extends HTMLElement { get rows() { return collect(this, e => e.localName === 'tr'); } insertRow() { const tr = this.ownerDocument.createElement('tr'); this.appendChild(tr); return tr; } }
class HTMLTableRowElement extends HTMLElement { get cells() { return collect(this, e => e.localName === 'td' || e.localName === 'th'); } insertCell() { const td = this.ownerDocument.createElement('td'); this.appendChild(td); return td; } get rowIndex() { return 0; } }
class HTMLTableCellElement extends HTMLElement {}
attrInt(HTMLTableCellElement.prototype, { colSpan: 'colspan', rowSpan: 'rowspan' }, 1);
class HTMLTableColElement extends HTMLElement {}
class HTMLTableCaptionElement extends HTMLElement {}
class HTMLFieldSetElement extends HTMLElement { get elements() { return formControls(this); } get type() { return 'fieldset'; } }
attrBool(HTMLFieldSetElement.prototype, { disabled: 'disabled' });
attrString(HTMLFieldSetElement.prototype, { name: 'name' });
class HTMLLegendElement extends HTMLElement {}
class HTMLDetailsElement extends HTMLElement {}
attrBool(HTMLDetailsElement.prototype, { open: 'open' });
class HTMLDialogElement extends HTMLElement {
  show() { this.setAttribute('open', ''); }
  showModal() { this.setAttribute('open', ''); }
  close(v) { if (v !== undefined) this.returnValue = v; if (this.hasAttribute('open')) { this.removeAttribute('open'); this.dispatchEvent(new Event('close')); } }
  get returnValue() { return this._rv || ''; } set returnValue(v) { this._rv = String(v); }
}
attrBool(HTMLDialogElement.prototype, { open: 'open' });
class HTMLMenuElement extends HTMLElement {}
class HTMLModElement extends HTMLElement {}
class HTMLTimeElement extends HTMLElement {}
class HTMLDataElement extends HTMLElement {}
class HTMLOutputElement extends HTMLElement { get value() { return this.textContent; } set value(v) { this.textContent = v; } get form() { return closestForm(this); } get type() { return 'output'; } }
class HTMLProgressElement extends HTMLElement {}
getter(HTMLProgressElement.prototype, 'value', function () { return Number(this.getAttribute('value')) || 0; }, function (v) { this.setAttribute('value', v); });
getter(HTMLProgressElement.prototype, 'max', function () { return Number(this.getAttribute('max')) || 1; }, function (v) { this.setAttribute('max', v); });
class HTMLMeterElement extends HTMLElement {}
class HTMLSlotElement extends HTMLElement {
  assignedNodes() { return []; } assignedElements() { return []; }
}
attrString(HTMLSlotElement.prototype, { name: 'name' });
class HTMLTemplateElement extends HTMLElement {
  constructor() { super(); this._content = null; }
  get content() { if (!this._content) { this._content = new DocumentFragment(); this._content.ownerDocument = K.templateDoc(); } return this._content; }
  get innerHTML() { return serializeChildren(this.content); }
  set innerHTML(h) { setInnerHTML(this.content, h); }
  _cloneHook(copy) { if (this._content) for (const c of this._content._c || []) copy.content.appendChild(cloneNodeImpl(c, true, copy.content.ownerDocument)); }
}
class HTMLParamElement extends HTMLElement {}
class HTMLEmbedElement extends HTMLElement {}
class HTMLObjectElement extends HTMLElement {}
class HTMLPictureElement extends HTMLElement {}
class HTMLSourceElement extends HTMLElement {}
attrString(HTMLSourceElement.prototype, { src: 'src', type: 'type', srcset: 'srcset', sizes: 'sizes', media: 'media' });
class HTMLTrackElement extends HTMLElement {}
class HTMLMapElement extends HTMLElement {}
class HTMLLabelElement extends HTMLElement {
  get htmlFor() { return this.getAttribute('for') || ''; }
  set htmlFor(v) { this.setAttribute('for', v); }
  get control() {
    const id = this.getAttribute('for');
    if (id) return this.ownerDocument.getElementById(id);
    return this.querySelector('input, select, textarea, button, meter, output, progress');
  }
  get form() { return closestForm(this); }
}
class HTMLIFrameElement extends HTMLElement {
  get contentWindow() { return null; }
  get contentDocument() { return null; }
}
attrString(HTMLIFrameElement.prototype, { src: 'src', name: 'name', srcdoc: 'srcdoc', allow: 'allow', loading: 'loading', referrerPolicy: 'referrerpolicy', sandbox: 'sandbox' });
class HTMLCanvasElement extends HTMLElement {
  getContext() { return null; }
  toDataURL() { return 'data:,'; }
  toBlob(cb) { setTimeout(() => cb(null), 0); }
  captureStream() { return null; }
  get width() { return parseInt(this.getAttribute('width'), 10) || 300; } set width(v) { this.setAttribute('width', v); }
  get height() { return parseInt(this.getAttribute('height'), 10) || 150; } set height(v) { this.setAttribute('height', v); }
}
class HTMLMediaElement extends HTMLElement {
  constructor() { super(); this.currentTime = 0; this.volume = 1; this.playbackRate = 1; }
  play() { return Promise.resolve(); }
  pause() {} load() {}
  canPlayType() { return ''; }
  get paused() { return true; } get ended() { return false; } get duration() { return NaN; } get readyState() { return 0; } get networkState() { return 0; } get buffered() { return { length: 0 }; } get seekable() { return { length: 0 }; } get error() { return null; } get currentSrc() { return this.src; } get textTracks() { return []; }
  addTextTrack() { return {}; }
}
attrString(HTMLMediaElement.prototype, { src: 'src', preload: 'preload', crossOrigin: 'crossorigin' });
attrBool(HTMLMediaElement.prototype, { autoplay: 'autoplay', loop: 'loop', controls: 'controls', muted: 'muted', defaultMuted: 'muted', playsInline: 'playsinline' });
class HTMLVideoElement extends HTMLMediaElement { get videoWidth() { return 0; } get videoHeight() { return 0; } }
attrString(HTMLVideoElement.prototype, { poster: 'poster' });
class HTMLAudioElement extends HTMLMediaElement {}

// ---- Links ----
class HTMLAnchorElement extends HTMLElement {
  get href() { const a = this.getAttribute('href'); if (a === null) return ''; try { return new URL(a, this.ownerDocument.baseURI).href; } catch (e) { return a; } }
  set href(v) { this.setAttribute('href', v); }
  _url() { try { return new URL(this.href); } catch (e) { return null; } }
  get origin() { const u = this._url(); return u ? u.origin : ''; }
  get protocol() { const u = this._url(); return u ? u.protocol : ':'; }
  set protocol(v) { const u = this._url(); if (u) { u.protocol = v; this.href = u.href; } }
  get host() { const u = this._url(); return u ? u.host : ''; }
  set host(v) { const u = this._url(); if (u) { u.host = v; this.href = u.href; } }
  get hostname() { const u = this._url(); return u ? u.hostname : ''; }
  set hostname(v) { const u = this._url(); if (u) { u.hostname = v; this.href = u.href; } }
  get port() { const u = this._url(); return u ? u.port : ''; }
  set port(v) { const u = this._url(); if (u) { u.port = v; this.href = u.href; } }
  get pathname() { const u = this._url(); return u ? u.pathname : ''; }
  set pathname(v) { const u = this._url(); if (u) { u.pathname = v; this.href = u.href; } }
  get search() { const u = this._url(); return u ? u.search : ''; }
  set search(v) { const u = this._url(); if (u) { u.search = v; this.href = u.href; } }
  get hash() { const u = this._url(); return u ? u.hash : ''; }
  set hash(v) { const u = this._url(); if (u) { u.hash = v; this.href = u.href; } }
  get username() { return ''; } get password() { return ''; }
  get text() { return this.textContent; } set text(v) { this.textContent = v; }
  get relList() { return new DOMTokenList(this, 'rel'); }
  toString() { return this.href; }
}
attrString(HTMLAnchorElement.prototype, { target: 'target', rel: 'rel', download: 'download', hreflang: 'hreflang', type: 'type', referrerPolicy: 'referrerpolicy', ping: 'ping' });
class HTMLAreaElement extends HTMLAnchorElement {}
class HTMLBaseElement extends HTMLElement { get href() { return this.getAttribute('href') || ''; } set href(v) { this.setAttribute('href', v); } }
attrString(HTMLBaseElement.prototype, { target: 'target' });
class HTMLLinkElement extends HTMLElement {
  get href() { const a = this.getAttribute('href'); if (a === null) return ''; try { return new URL(a, this.ownerDocument.baseURI).href; } catch (e) { return a; } }
  set href(v) { this.setAttribute('href', v); }
  get sheet() { return this.getAttribute('rel') === 'stylesheet' ? makeSheet() : null; }
  get relList() { return new DOMTokenList(this, 'rel'); }
  _connected() {
    const rel = (this.getAttribute('rel') || '').toLowerCase();
    if (this._fired || !this.getAttribute('href')) return;
    if (rel === 'stylesheet' || rel === 'preload' || rel === 'modulepreload' || rel === 'prefetch') {
      this._fired = true;
      setTimeout(() => this.dispatchEvent(new Event('load')), 0);
    }
  }
}
attrString(HTMLLinkElement.prototype, { rel: 'rel', as: 'as', type: 'type', media: 'media', crossOrigin: 'crossorigin', hreflang: 'hreflang', integrity: 'integrity', sizes: 'sizes' });
attrBool(HTMLLinkElement.prototype, { disabled: 'disabled' });
class HTMLMetaElement extends HTMLElement {}
attrString(HTMLMetaElement.prototype, { name: 'name', content: 'content', httpEquiv: 'http-equiv', media: 'media' });
function makeSheet() {
  const rules = [];
  return { cssRules: rules, rules, disabled: false, media: { mediaText: '' }, ownerNode: null, href: null,
    insertRule(text, i = 0) { rules.splice(i, 0, { cssText: text, selectorText: '', style: {} }); return i; },
    deleteRule(i) { rules.splice(i, 1); }, replace() { return Promise.resolve(this); }, replaceSync() {}, addRule() { return 0; }, removeRule() {} };
}
class HTMLStyleElement extends HTMLElement {
  get sheet() { return this._sheet || (this._sheet = makeSheet()); }
  _connected() { if (!this._fired && this.getAttribute('media') === null) { /* inline styles load at once */ } }
}
attrBool(HTMLStyleElement.prototype, { disabled: 'disabled' });
attrString(HTMLStyleElement.prototype, { media: 'media', type: 'type' });
class HTMLTitleElement extends HTMLElement {
  get text() { return this.textContent; } set text(v) { this.textContent = v; }
}

// ---- Images ----
class HTMLImageElement extends HTMLElement {
  get src() { const a = this.getAttribute('src'); if (a === null) return ''; try { return new URL(a, this.ownerDocument.baseURI).href; } catch (e) { return a; } }
  set src(v) { this.setAttribute('src', v); }
  get currentSrc() { return this.src; }
  get complete() { return true; }
  get naturalWidth() { return 0; } get naturalHeight() { return 0; }
  get x() { return 0; } get y() { return 0; }
  decode() { return Promise.resolve(); }
  _attrHook(name, old, value) {
    if (name === 'src' && value) setTimeout(() => this.dispatchEvent(new Event('load')), 0);
  }
}
attrString(HTMLImageElement.prototype, { alt: 'alt', srcset: 'srcset', sizes: 'sizes', crossOrigin: 'crossorigin', useMap: 'usemap', loading: 'loading', decoding: 'decoding', referrerPolicy: 'referrerpolicy', fetchPriority: 'fetchpriority' });
attrBool(HTMLImageElement.prototype, { isMap: 'ismap' });
attrInt(HTMLImageElement.prototype, { width: 'width', height: 'height' });
publish('Image', function Image(w, h) { const i = K.doc.createElement('img'); if (w !== undefined) i.setAttribute('width', w); if (h !== undefined) i.setAttribute('height', h); return i; });
publish('Audio', function Audio(src) { const a = K.doc.createElement('audio'); if (src) a.src = src; return a; });
publish('Option', function Option(text, value, defaultSelected, selected) {
  const o = K.doc.createElement('option');
  if (text !== undefined) o.text = text;
  if (value !== undefined) o.setAttribute('value', value);
  if (defaultSelected) o.setAttribute('selected', '');
  if (selected) o.selected = true;
  return o;
});

// ---- Scripts ----
class HTMLScriptElement extends HTMLElement {
  constructor() { super(); this._started = false; this._parserInserted = false; }
  get src() { const a = this.getAttribute('src'); if (a === null) return ''; try { return new URL(a, this.ownerDocument.baseURI).href; } catch (e) { return a; } }
  set src(v) { this.setAttribute('src', v); }
  get text() { return this.textContent; } set text(v) { this.textContent = v; }
  get async() { return this.hasAttribute('async'); } set async(v) { if (v) this.setAttribute('async', ''); else this.removeAttribute('async'); }
  _connected() {
    if (this._started || this._parserInserted === true) return;
    if (!this.hasAttribute('src') && !this.textContent) return;
    this._started = true;
    K.queueScript(this);
  }
  static supports(t) { return ['classic', 'module', 'importmap'].includes(t); }
}
attrString(HTMLScriptElement.prototype, { type: 'type', crossOrigin: 'crossorigin', integrity: 'integrity', referrerPolicy: 'referrerpolicy', charset: 'charset' });
attrBool(HTMLScriptElement.prototype, { defer: 'defer', noModule: 'nomodule' });

// ---- Forms and controls ----
function closestForm(el) {
  const id = el.getAttribute && el.getAttribute('form');
  if (id) { const f = el.ownerDocument.getElementById(id); return f && f.localName === 'form' ? f : null; }
  for (let n = el._p; n; n = n._p) if (n.localName === 'form') return n;
  return null;
}
const controlTags = new Set(['input', 'button', 'select', 'textarea', 'fieldset', 'output', 'object']);
function formControls(root) {
  const out = new NodeList();
  const doc = root.ownerDocument;
  const walk = (n) => {
    if (!n._c) return;
    for (const c of n._c) {
      if (c.nodeType !== 1) continue;
      if (controlTags.has(c.localName) && (root.localName !== 'form' || closestForm(c) === root)) out.push(c);
      walk(c);
    }
  };
  walk(root);
  if (root.localName === 'form') {
    // controls elsewhere that point here with form="id"
    const id = root.getAttribute('id');
    if (id) for (const c of collect(doc, e => controlTags.has(e.localName) && e.getAttribute('form') === id)) if (!out.includes(c)) out.push(c);
  }
  return out;
}
class HTMLFormElement extends HTMLElement {
  get elements() { return formControls(this); }
  get length() { return this.elements.length; }
  get action() { const a = this.getAttribute('action'); try { return new URL(a == null || a === '' ? this.ownerDocument.URL : a, this.ownerDocument.baseURI).href; } catch (e) { return a || ''; } }
  set action(v) { this.setAttribute('action', v); }
  get method() { const m = (this.getAttribute('method') || 'get').toLowerCase(); return m === 'post' || m === 'dialog' ? m : 'get'; }
  set method(v) { this.setAttribute('method', v); }
  get encoding() { return this.enctype; } set encoding(v) { this.enctype = v; }
  submit() { formSubmitNative(this, null); }
  requestSubmit(submitter) { submitFlow(this, submitter || null); }
  reset() {
    if (!this.dispatchEvent(new Event('reset', { bubbles: true, cancelable: true }))) return;
    for (const c of this.elements) if (c._reset) c._reset();
  }
  checkValidity() { return true; }
  reportValidity() { return true; }
}
attrString(HTMLFormElement.prototype, { target: 'target', name: 'name', enctype: 'enctype', acceptCharset: 'accept-charset', autocomplete: 'autocomplete', rel: 'rel' });
attrBool(HTMLFormElement.prototype, { noValidate: 'novalidate' });

const validity = { valid: true, badInput: false, customError: false, patternMismatch: false, rangeOverflow: false, rangeUnderflow: false, stepMismatch: false, tooLong: false, tooShort: false, typeMismatch: false, valueMissing: false };
class FormControl extends HTMLElement {
  get form() { return closestForm(this); }
  get labels() {
    const out = new NodeList();
    const id = this.getAttribute('id');
    for (const l of collect(this.ownerDocument, e => e.localName === 'label')) {
      if ((id && l.getAttribute('for') === id) || (!l.hasAttribute('for') && l.contains(this))) out.push(l);
    }
    return out;
  }
  get validity() { return validity; }
  get validationMessage() { return ''; }
  get willValidate() { return !this.disabled; }
  checkValidity() { return true; }
  reportValidity() { return true; }
  setCustomValidity() {}
}
attrBool(FormControl.prototype, { disabled: 'disabled', required: 'required' });
attrString(FormControl.prototype, { name: 'name', autocomplete: 'autocomplete' });

const inputTypes = new Set(['hidden', 'text', 'search', 'tel', 'url', 'email', 'password', 'date', 'month', 'week', 'time', 'datetime-local', 'number', 'range', 'color', 'checkbox', 'radio', 'file', 'submit', 'image', 'reset', 'button']);
class HTMLInputElement extends FormControl {
  constructor() { super(); this._value = null; this._checked = null; this._dirtyChecked = false; this._sel = [0, 0]; }
  get type() { const t = (this.getAttribute('type') || '').toLowerCase(); return inputTypes.has(t) ? t : 'text'; }
  set type(v) { this.setAttribute('type', v); }
  get value() {
    if (this._value !== null) return this._value;
    const a = this.getAttribute('value');
    const t = this.type;
    if (t === 'checkbox' || t === 'radio') return a === null ? 'on' : a;
    return a === null ? '' : sanitizeValue(t, a);
  }
  set value(v) {
    if (this.type === 'file') { if (v === '' || v === null) this._value = ''; else throw new DOMException('This input element accepts a filename, which may only be programmatically set to the empty string.', 'InvalidStateError'); return; }
    if (this.type === 'checkbox' || this.type === 'radio') { this.setAttribute('value', v); return; }
    this._value = sanitizeValue(this.type, v === null ? '' : String(v));
    this._sel = [this._value.length, this._value.length];
    changed(this, null);
  }
  get defaultValue() { return this.getAttribute('value') || ''; }
  set defaultValue(v) { this.setAttribute('value', v); }
  get checked() { return this._checked !== null ? this._checked : this.hasAttribute('checked'); }
  set checked(v) {
    v = !!v;
    this._checked = v;
    if (v && this.type === 'radio') uncheckGroup(this);
    changed(this, null);
  }
  get defaultChecked() { return this.hasAttribute('checked'); }
  set defaultChecked(v) { if (v) this.setAttribute('checked', ''); else this.removeAttribute('checked'); }
  get indeterminate() { return !!this._indet; } set indeterminate(v) { this._indet = !!v; }
  get files() { return this._files || (this._files = Object.assign([], { item(i) { return this[i] || null; } })); }
  set files(v) { this._files = v; }
  get valueAsNumber() { const n = parseFloat(this.value); return isNaN(n) ? NaN : n; }
  set valueAsNumber(v) { this.value = String(v); }
  get valueAsDate() { return null; } set valueAsDate(v) {}
  get selectionStart() { return this._sel[0]; } set selectionStart(v) { this._sel[0] = v; }
  get selectionEnd() { return this._sel[1]; } set selectionEnd(v) { this._sel[1] = v; }
  get selectionDirection() { return 'none'; }
  setSelectionRange(a, b) { this._sel = [a, b]; }
  select() { this._sel = [0, this.value.length]; }
  setRangeText(t, s, e) { const v = this.value; s = s ?? this._sel[0]; e = e ?? this._sel[1]; this.value = v.slice(0, s) + t + v.slice(e); }
  stepUp() {} stepDown() {} showPicker() {}
  get list() { const id = this.getAttribute('list'); return id ? this.ownerDocument.getElementById(id) : null; }
  get formAction() { return this.getAttribute('formaction') || this.form?.action || ''; }
  _reset() { this._value = null; this._checked = null; changed(this, null); }
  _cloneHook(copy) { copy._value = this._value; copy._checked = this._checked; }
}
attrString(HTMLInputElement.prototype, { placeholder: 'placeholder', accept: 'accept', alt: 'alt', autocomplete: 'autocomplete', dirName: 'dirname', min: 'min', max: 'max', step: 'step', pattern: 'pattern', src: 'src', formEnctype: 'formenctype', formMethod: 'formmethod', formTarget: 'formtarget', capture: 'capture' });
attrBool(HTMLInputElement.prototype, { readOnly: 'readonly', multiple: 'multiple', formNoValidate: 'formnovalidate' });
attrInt(HTMLInputElement.prototype, { size: 'size' }, 20);
getter(HTMLInputElement.prototype, 'maxLength', function () { const v = parseInt(this.getAttribute('maxlength'), 10); return isNaN(v) ? -1 : v; }, function (v) { this.setAttribute('maxlength', v); });
getter(HTMLInputElement.prototype, 'minLength', function () { const v = parseInt(this.getAttribute('minlength'), 10); return isNaN(v) ? -1 : v; }, function (v) { this.setAttribute('minlength', v); });
function sanitizeValue(type, v) {
  if (type === 'text' || type === 'search' || type === 'tel' || type === 'password') return v.replace(/[\r\n]/g, '');
  if (type === 'url' || type === 'email') return v.replace(/[\r\n]/g, '').trim();
  if (type === 'number') return isNaN(parseFloat(v)) ? '' : v;
  return v;
}
function uncheckGroup(el) {
  const name = el.getAttribute('name');
  if (!name) return;
  const form = closestForm(el);
  for (const o of collect(form || el.ownerDocument, e => e.localName === 'input' && e.getAttribute('type') === 'radio' && e.getAttribute('name') === name)) {
    if (o !== el && closestForm(o) === form) o._checked = false;
  }
}
class HTMLButtonElement extends FormControl {
  get type() { const t = (this.getAttribute('type') || '').toLowerCase(); return t === 'button' || t === 'reset' ? t : 'submit'; }
  set type(v) { this.setAttribute('type', v); }
  get value() { return this.getAttribute('value') || ''; } set value(v) { this.setAttribute('value', v); }
  get formAction() { return this.getAttribute('formaction') || this.form?.action || ''; }
}
attrString(HTMLButtonElement.prototype, { formEnctype: 'formenctype', formMethod: 'formmethod', formTarget: 'formtarget', popoverTarget: 'popovertarget' });
attrBool(HTMLButtonElement.prototype, { formNoValidate: 'formnovalidate' });
class HTMLTextAreaElement extends FormControl {
  constructor() { super(); this._value = null; this._sel = [0, 0]; }
  get type() { return 'textarea'; }
  get value() { return this._value !== null ? this._value : this.textContent.replace(/^\n/, ''); }
  set value(v) { this._value = String(v == null ? '' : v).replace(/\r\n?/g, '\n'); this._sel = [this._value.length, this._value.length]; changed(this, null); }
  get defaultValue() { return this.textContent; } set defaultValue(v) { this.textContent = v; }
  get textLength() { return this.value.length; }
  get selectionStart() { return this._sel[0]; } set selectionStart(v) { this._sel[0] = v; }
  get selectionEnd() { return this._sel[1]; } set selectionEnd(v) { this._sel[1] = v; }
  setSelectionRange(a, b) { this._sel = [a, b]; }
  select() { this._sel = [0, this.value.length]; }
  setRangeText(t, s, e) { const v = this.value; s = s ?? this._sel[0]; e = e ?? this._sel[1]; this.value = v.slice(0, s) + t + v.slice(e); }
  _reset() { this._value = null; changed(this, null); }
}
attrString(HTMLTextAreaElement.prototype, { placeholder: 'placeholder', wrap: 'wrap', dirName: 'dirname' });
attrBool(HTMLTextAreaElement.prototype, { readOnly: 'readonly' });
attrInt(HTMLTextAreaElement.prototype, { rows: 'rows', cols: 'cols' }, 2);
getter(HTMLTextAreaElement.prototype, 'maxLength', function () { const v = parseInt(this.getAttribute('maxlength'), 10); return isNaN(v) ? -1 : v; }, function (v) { this.setAttribute('maxlength', v); });

class HTMLOptionElement extends HTMLElement {
  constructor() { super(); this._selected = null; }
  get value() { const a = this.getAttribute('value'); return a !== null ? a : this.text; }
  set value(v) { this.setAttribute('value', v); }
  get text() { return this.textContent.replace(/\s+/g, ' ').trim(); }
  set text(v) { this.textContent = v; }
  get label() { return this.getAttribute('label') || this.text; } set label(v) { this.setAttribute('label', v); }
  get selected() { return this._selected !== null ? this._selected : this.hasAttribute('selected'); }
  set selected(v) { this._selected = !!v; const s = this.parentSelect; if (s && v && !s.multiple) for (const o of s.options) if (o !== this) o._selected = false; changed(this, null); }
  get defaultSelected() { return this.hasAttribute('selected'); } set defaultSelected(v) { if (v) this.setAttribute('selected', ''); else this.removeAttribute('selected'); }
  get parentSelect() { for (let n = this._p; n; n = n._p) if (n.localName === 'select') return n; return null; }
  get index() { const s = this.parentSelect; return s ? s.options.indexOf(this) : 0; }
  get form() { const s = this.parentSelect; return s ? s.form : null; }
}
attrBool(HTMLOptionElement.prototype, { disabled: 'disabled' });
class HTMLOptGroupElement extends HTMLElement {}
attrBool(HTMLOptGroupElement.prototype, { disabled: 'disabled' });
attrString(HTMLOptGroupElement.prototype, { label: 'label' });
class HTMLSelectElement extends FormControl {
  get type() { return this.multiple ? 'select-multiple' : 'select-one'; }
  get options() { const o = collect(this, e => e.localName === 'option'); return o; }
  get selectedOptions() { return this.options.filter(o => o.selected); }
  get length() { return this.options.length; }
  get selectedIndex() {
    const o = this.options;
    for (let i = 0; i < o.length; i++) if (o[i].selected) return i;
    return this.multiple || this.size > 1 ? -1 : (o.length ? this._firstEnabled(o) : -1);
  }
  _firstEnabled(o) { for (let i = 0; i < o.length; i++) if (!o[i].disabled) return i; return 0; }
  set selectedIndex(i) { this.options.forEach((o, k) => { o._selected = k === i; }); changed(this, null); }
  get value() { const i = this.selectedIndex; const o = this.options[i]; return o ? o.value : ''; }
  set value(v) { let found = false; for (const o of this.options) { o._selected = !found && o.value === String(v); if (o._selected) found = true; } changed(this, null); }
  get size() { return parseInt(this.getAttribute('size'), 10) || 0; } set size(v) { this.setAttribute('size', v); }
  add(o, before) { this.insertBefore(o, before instanceof Node ? before : (typeof before === 'number' ? this.options[before] || null : null)); }
  remove(i) { if (i === undefined) { Element.prototype.remove.call(this); return; } const o = this.options[i]; if (o && o._p) o._p.removeChild(o); }
  item(i) { return this.options[i] || null; }
  namedItem(n) { return this.options.find(o => o.id === n || o.getAttribute('name') === n) || null; }
  _reset() { for (const o of this.options) o._selected = null; changed(this, null); }
}
attrBool(HTMLSelectElement.prototype, { multiple: 'multiple' });

defineTags(HTMLDivElement, 'div');
defineTags(HTMLSpanElement, 'span');
defineTags(HTMLParagraphElement, 'p');
defineTags(HTMLHeadingElement, 'h1 h2 h3 h4 h5 h6');
defineTags(HTMLBodyElement, 'body'); defineTags(HTMLHeadElement, 'head'); defineTags(HTMLHtmlElement, 'html');
defineTags(HTMLBRElement, 'br'); defineTags(HTMLHRElement, 'hr'); defineTags(HTMLPreElement, 'pre listing xmp');
defineTags(HTMLQuoteElement, 'blockquote q'); defineTags(HTMLUListElement, 'ul'); defineTags(HTMLOListElement, 'ol');
defineTags(HTMLLIElement, 'li'); defineTags(HTMLDListElement, 'dl');
defineTags(HTMLTableElement, 'table'); defineTags(HTMLTableSectionElement, 'thead tbody tfoot'); defineTags(HTMLTableRowElement, 'tr');
defineTags(HTMLTableCellElement, 'td th'); defineTags(HTMLTableColElement, 'col colgroup'); defineTags(HTMLTableCaptionElement, 'caption');
defineTags(HTMLFieldSetElement, 'fieldset'); defineTags(HTMLLegendElement, 'legend'); defineTags(HTMLDetailsElement, 'details');
defineTags(HTMLDialogElement, 'dialog'); defineTags(HTMLMenuElement, 'menu'); defineTags(HTMLModElement, 'ins del');
defineTags(HTMLTimeElement, 'time'); defineTags(HTMLDataElement, 'data'); defineTags(HTMLOutputElement, 'output');
defineTags(HTMLProgressElement, 'progress'); defineTags(HTMLMeterElement, 'meter'); defineTags(HTMLSlotElement, 'slot');
defineTags(HTMLTemplateElement, 'template'); defineTags(HTMLParamElement, 'param'); defineTags(HTMLEmbedElement, 'embed');
defineTags(HTMLObjectElement, 'object'); defineTags(HTMLPictureElement, 'picture'); defineTags(HTMLSourceElement, 'source');
defineTags(HTMLTrackElement, 'track'); defineTags(HTMLMapElement, 'map'); defineTags(HTMLLabelElement, 'label');
defineTags(HTMLIFrameElement, 'iframe frame'); defineTags(HTMLCanvasElement, 'canvas'); defineTags(HTMLVideoElement, 'video');
defineTags(HTMLAudioElement, 'audio'); defineTags(HTMLAnchorElement, 'a'); defineTags(HTMLAreaElement, 'area');
defineTags(HTMLBaseElement, 'base'); defineTags(HTMLLinkElement, 'link'); defineTags(HTMLMetaElement, 'meta');
defineTags(HTMLStyleElement, 'style'); defineTags(HTMLTitleElement, 'title'); defineTags(HTMLImageElement, 'img');
defineTags(HTMLScriptElement, 'script'); defineTags(HTMLFormElement, 'form'); defineTags(HTMLInputElement, 'input');
defineTags(HTMLButtonElement, 'button'); defineTags(HTMLTextAreaElement, 'textarea'); defineTags(HTMLOptionElement, 'option');
defineTags(HTMLOptGroupElement, 'optgroup'); defineTags(HTMLSelectElement, 'select');
for (const [n, c] of Object.entries({ HTMLUnknownElement, HTMLDivElement, HTMLSpanElement, HTMLParagraphElement, HTMLHeadingElement, HTMLBodyElement, HTMLHeadElement, HTMLHtmlElement, HTMLBRElement, HTMLHRElement, HTMLPreElement, HTMLQuoteElement, HTMLUListElement, HTMLOListElement, HTMLLIElement, HTMLDListElement, HTMLTableElement, HTMLTableSectionElement, HTMLTableRowElement, HTMLTableCellElement, HTMLTableColElement, HTMLTableCaptionElement, HTMLFieldSetElement, HTMLLegendElement, HTMLDetailsElement, HTMLDialogElement, HTMLMenuElement, HTMLModElement, HTMLTimeElement, HTMLDataElement, HTMLOutputElement, HTMLProgressElement, HTMLMeterElement, HTMLSlotElement, HTMLTemplateElement, HTMLParamElement, HTMLEmbedElement, HTMLObjectElement, HTMLPictureElement, HTMLSourceElement, HTMLTrackElement, HTMLMapElement, HTMLLabelElement, HTMLIFrameElement, HTMLCanvasElement, HTMLMediaElement, HTMLVideoElement, HTMLAudioElement, HTMLAnchorElement, HTMLAreaElement, HTMLBaseElement, HTMLLinkElement, HTMLMetaElement, HTMLStyleElement, HTMLTitleElement, HTMLImageElement, HTMLScriptElement, HTMLFormElement, HTMLInputElement, HTMLButtonElement, HTMLTextAreaElement, HTMLOptionElement, HTMLOptGroupElement, HTMLSelectElement }))
  publish(n, c);

// Making elements
function createElementImpl(doc, localName, ns, prefix) {
  if (ns === HTML_NS) {
    let cls = tagClasses[localName];
    if (!cls && localName.includes('-')) {
      const def = K.customByName.get(localName);
      if (def) {
        K.initTag = { localName, ns, prefix, doc };
        let el;
        try { el = new def.ctor(); } catch (e) { K.initTag = null; reportError(e); el = null; }
        if (el) { el._observed = def.observed; return el; }
      }
      cls = HTMLElement;
    }
    K.initTag = { localName, ns, prefix, doc };
    const el = new (cls || HTMLUnknownElement)();
    K.initTag = null;
    return el;
  }
  K.initTag = { localName, ns, prefix, doc };
  const el = new (ns === SVG_NS ? SVGElement : ns === MATHML_NS ? MathMLElement : Element)();
  K.initTag = null;
  return el;
}
K.createElementImpl = createElementImpl;

// Custom elements
class CustomElementRegistry {
  define(name, ctor, opts) {
    if (!/^[a-z][-._0-9a-z]*-[-._0-9a-z]*$/.test(name)) throw new DOMException("The name '" + name + "' is not a valid custom element name.", 'SyntaxError');
    if (K.customByName.has(name)) throw new DOMException("The name '" + name + "' has already been used with this registry", 'NotSupportedError');
    const def = { name, ctor, observed: ctor.observedAttributes || [] };
    K.customByName.set(name, def);
    K.customByCtor.set(ctor, def);
    // upgrade what's already there
    for (const el of collect(K.doc, e => e.localName === name && e.constructor === HTMLElement)) upgradeElement(el, def);
    const w = this._waiting && this._waiting[name];
    if (w) w.forEach(r => r(ctor));
  }
  get(name) { const d = K.customByName.get(name); return d ? d.ctor : undefined; }
  whenDefined(name) { if (K.customByName.has(name)) return Promise.resolve(K.customByName.get(name).ctor); this._waiting = this._waiting || {}; return new Promise(r => (this._waiting[name] = this._waiting[name] || []).push(r)); }
  upgrade(root) { for (const el of collect(root, e => K.customByName.has(e.localName))) { if (el.constructor === HTMLElement) upgradeElement(el, K.customByName.get(el.localName)); } }
}
function upgradeElement(el, def) {
  Object.setPrototypeOf(el, def.ctor.prototype);
  el._observed = def.observed;
  try {
    K.initTag = { localName: el.localName, ns: HTML_NS, doc: el.ownerDocument };
    // run the constructor body against the existing element
    const saved = Object.assign({}, { a: el._a, c: el._c, p: el._p, prev: el._prev, next: el._next });
    const fresh = Reflect.construct(def.ctor, [], def.ctor);
    K.initTag = null;
    // copy what the constructor set up onto the existing element
    for (const k of Object.keys(fresh)) if (!(k in el) || ['_a', '_c', '_p', '_prev', '_next'].indexOf(k) < 0) { if (!['_a', '_c', '_p', '_prev', '_next', 'ownerDocument', '_l'].includes(k)) el[k] = fresh[k]; }
    if (fresh._sr && !el._sr) { el._sr = fresh._sr; fresh._sr.host = el; }
  } catch (e) { K.initTag = null; reportError(e); }
  if (el._a && el.attributeChangedCallback) for (const [k, v] of el._a) if (def.observed.includes(k)) { try { el.attributeChangedCallback(k, null, v); } catch (e) { reportError(e); } }
  if (el.isConnected && el.connectedCallback) try { el.connectedCallback(); } catch (e) { reportError(e); }
}
HTMLElement.prototype._connected = function () { if (this.connectedCallback) { try { this.connectedCallback(); } catch (e) { reportError(e); } } };
HTMLElement.prototype._disconnected = function () { if (this.disconnectedCallback) { try { this.disconnectedCallback(); } catch (e) { reportError(e); } } };
publish('CustomElementRegistry', CustomElementRegistry);
publish('customElements', new CustomElementRegistry());

// ---- Clicking, form submission ----
function dispatchClick(el, init) {
  const type = el.type;
  const tag = el.localName;
  let undo = null;
  if (tag === 'input' && type === 'checkbox') { const old = el.checked; el._checked = !old; undo = () => { el._checked = old; }; }
  else if (tag === 'input' && type === 'radio') { const old = el.checked; el._checked = true; uncheckGroup(el); undo = () => { el._checked = old; }; }
  const ev = new MouseEvent('click', Object.assign({ bubbles: true, cancelable: true, composed: true, detail: 1, view: G }, init));
  const ok = el.dispatchEvent(ev);
  if (!ok) { if (undo) undo(); return ev; }
  activate(el, ev);
  return ev;
}
function activate(el, ev) {
  const tag = el.localName, type = el.type;
  if (tag === 'input' && (type === 'checkbox' || type === 'radio')) {
    el.dispatchEvent(new Event('input', { bubbles: true, composed: true }));
    el.dispatchEvent(new Event('change', { bubbles: true }));
  } else if ((tag === 'input' && (type === 'submit' || type === 'image')) || (tag === 'button' && type === 'submit')) {
    const f = el.form;
    if (f && !el.disabled) submitFlow(f, el);
  } else if ((tag === 'input' || tag === 'button') && type === 'reset') {
    const f = el.form;
    if (f) f.reset();
  } else if ((tag === 'a' || tag === 'area') && el.hasAttribute('href')) {
    followLink(el, ev);
  } else if (tag === 'summary' && el._p && el._p.localName === 'details') {
    el._p.toggleAttribute('open');
    el._p.dispatchEvent(new Event('toggle'));
  }
  // a click inside a label activates the label's control
  const lab = el.closest && el.closest('label');
  if (lab) {
    const c = lab.control;
    const interactive = tag === 'a' || tag === 'button' || tag === 'input' || tag === 'select' || tag === 'textarea';
    if (c && c !== el && !c.contains(el) && !interactive) dispatchClick(c);
  }
}
function followLink(a, ev) {
  const href = a.getAttribute('href');
  if (href === null) return;
  if (/^\s*javascript:/i.test(href)) { try { (0, eval)(href.replace(/^\s*javascript:/i, '')); } catch (e) { reportError(e); } return; }
  const target = (a.getAttribute('target') || '').toLowerCase();
  if (a.hasAttribute('download')) { N.announce('Downloads are not supported yet.'); return; }
  K.navigate(a.href, false, target && target !== '_self' && target !== '_top' && target !== '_parent' ? true : false);
}
K.dispatchClick = dispatchClick;

function formEntries(form, submitter) {
  const out = [];
  for (const c of form.elements) {
    if (c.disabled || !c.getAttribute) continue;
    if (c.closest && c.closest('fieldset[disabled]')) continue;
    const name = c.getAttribute('name');
    if (c.localName === 'object' || c.localName === 'fieldset' || c.localName === 'output') continue;
    if (!name) continue;
    if (c.localName === 'input') {
      const t = c.type;
      if (t === 'checkbox' || t === 'radio') { if (c.checked) out.push([name, c.value]); }
      else if (t === 'submit' || t === 'image' || t === 'button' || t === 'reset') { if (c === submitter) out.push([name, c.value]); }
      else if (t === 'file') { /* files are not supported */ }
      else out.push([name, c.value]);
    } else if (c.localName === 'button') {
      if (c === submitter) out.push([name, c.value]);
    } else if (c.localName === 'select') {
      for (const o of c.options) if (o.selected && !o.disabled) out.push([name, o.value]);
      if (!c.multiple && c.selectedIndex >= 0 && !c.options.some(o => o.selected)) out.push([name, c.value]);
    } else if (c.localName === 'textarea') {
      out.push([name, c.value]);
    }
  }
  return out;
}
K.formEntries = formEntries;
function submitFlow(form, submitter) {
  const ev = new SubmitEvent('submit', { bubbles: true, cancelable: true, submitter });
  if (form.dispatchEvent(ev)) formSubmitNative(form, submitter);
}
K.submitFlow = submitFlow;
function formSubmitNative(form, submitter) {
  const method = ((submitter && submitter.getAttribute('formmethod')) || form.getAttribute('method') || 'get').toLowerCase();
  let action = (submitter && submitter.getAttribute('formaction')) || form.getAttribute('action');
  let url;
  try { url = new URL(action == null || action === '' ? form.ownerDocument.URL : action, form.ownerDocument.baseURI); } catch (e) { return; }
  const entries = formEntries(form, submitter);
  if (method === 'dialog') { const d = form.closest('dialog'); if (d) d.close(submitter ? submitter.value : undefined); return; }
  const params = new URLSearchParams(entries);
  if (method === 'post') {
    const enc = (submitter && submitter.getAttribute('formenctype')) || form.getAttribute('enctype') || '';
    K.navigate(url.href, false, false, 'POST', params.toString(), enc || 'application/x-www-form-urlencoded');
  } else {
    url.search = params.toString();
    K.navigate(url.href, false, false);
  }
}

// 20_select.js - CSS selectors: querySelector, matches, closest.

const selCache = new Map();
function selError(sel) { return new DOMException("'" + sel + "' is not a valid selector.", 'SyntaxError'); }

function parseSelectorList(text) {
  let i = 0;
  const n = text.length;
  const ws = () => { while (i < n && /[ \t\n\r\f]/.test(text[i])) i++; };
  const identChar = (c) => /[-_a-zA-Z0-9\u0080-￿]/.test(c);
  function ident() {
    let out = '';
    while (i < n) {
      const c = text[i];
      if (c === '\\') {
        i++;
        if (i >= n) break;
        let m = /^[0-9a-fA-F]{1,6}/.exec(text.slice(i, i + 6));
        if (m) { out += String.fromCodePoint(parseInt(m[0], 16) || 0xfffd); i += m[0].length; if (/[ \t\n\r\f]/.test(text[i] || '')) i++; }
        else { out += text[i]; i++; }
      } else if (identChar(c)) { out += c; i++; } else break;
    }
    return out;
  }
  function str() {
    const q = text[i++];
    let out = '';
    while (i < n && text[i] !== q) {
      if (text[i] === '\\') { i++; out += text[i] || ''; i++; } else out += text[i++];
    }
    i++;
    return out;
  }
  function parseNth(arg) {
    arg = arg.trim().toLowerCase();
    if (arg === 'odd') return [2, 1];
    if (arg === 'even') return [2, 0];
    const m = /^([+-]?\d*)n\s*(?:([+-])\s*(\d+))?$/.exec(arg);
    if (m) {
      const a = m[1] === '' || m[1] === '+' ? 1 : m[1] === '-' ? -1 : parseInt(m[1], 10);
      const b = m[3] ? (m[2] === '-' ? -1 : 1) * parseInt(m[3], 10) : 0;
      return [a, b];
    }
    if (/^[+-]?\d+$/.test(arg)) return [0, parseInt(arg, 10)];
    throw selError(text);
  }
  function compound() {
    const c = { tag: null, id: null, classes: [], attrs: [], pseudos: [] };
    let any = false;
    if (text[i] === '*') { i++; c.tag = '*'; any = true; if (text[i] === '|') { i++; c.tag = text[i] === '*' ? (i++, '*') : ident(); } }
    else if (identChar(text[i] || '') || text[i] === '\\') { c.tag = ident(); any = true; if (text[i] === '|' && text[i + 1] !== '=') { i++; c.tag = text[i] === '*' ? (i++, '*') : ident(); } }
    else if (text[i] === '|') { i++; c.tag = ident(); any = true; }
    for (;;) {
      const ch = text[i];
      if (ch === '#') { i++; c.id = ident(); any = true; }
      else if (ch === '.') { i++; c.classes.push(ident()); any = true; }
      else if (ch === '[') {
        i++; ws();
        let name = ident();
        if (text[i] === '|' && text[i + 1] !== '=') { i++; name = ident(); }
        ws();
        let op = null, val = '', flag = '';
        if (text[i] !== ']') {
          const m = /^[~|^$*]?=/.exec(text.slice(i, i + 2));
          if (!m) throw selError(text);
          op = m[0]; i += op.length; ws();
          if (text[i] === '"' || text[i] === "'") val = str(); else val = ident();
          ws();
          if (/[iIsS]/.test(text[i] || '') && /[\s\]]/.test(text[i + 1] || ']')) { flag = text[i].toLowerCase(); i++; ws(); }
        }
        if (text[i] !== ']') throw selError(text);
        i++;
        c.attrs.push({ name: name.toLowerCase(), op, val, ci: flag === 'i' });
        any = true;
      } else if (ch === ':') {
        i++;
        if (text[i] === ':') { i++; ident(); if (text[i] === '(') { let d = 0; do { if (text[i] === '(') d++; if (text[i] === ')') d--; i++; } while (i < n && d > 0); } c.pseudos.push({ name: '::', never: true }); any = true; continue; }
        const name = ident().toLowerCase();
        let arg = null;
        if (text[i] === '(') {
          i++;
          const start = i;
          let d = 1, quote = '';
          while (i < n && d > 0) {
            const x = text[i];
            if (quote) { if (x === quote) quote = ''; else if (x === '\\') i++; }
            else if (x === '"' || x === "'") quote = x;
            else if (x === '(') d++;
            else if (x === ')') d--;
            i++;
          }
          arg = text.slice(start, i - 1);
        }
        const p = { name, arg };
        if (arg !== null) {
          if (name === 'not' || name === 'is' || name === 'where' || name === 'matches' || name === '-webkit-any' || name === '-moz-any') p.list = parseSelectorList(arg);
          else if (name === 'has') p.list = parseRelative(arg);
          else if (/^nth-/.test(name)) {
            const m = /^(.*?)(?:\s+of\s+(.*))?$/i.exec(arg);
            p.nth = parseNth(m[1]);
            if (m[2]) p.of = parseSelectorList(m[2]);
          }
        }
        if (['before', 'after', 'first-line', 'first-letter'].includes(name)) p.never = true;
        c.pseudos.push(p);
        any = true;
      } else break;
    }
    if (!any) throw selError(text);
    return c;
  }
  function complex() {
    const parts = [];
    ws();
    let comb = ' ';
    parts.push({ comb: null, c: compound() });
    for (;;) {
      const before = i;
      ws();
      const ch = text[i];
      if (i >= n || ch === ',' || ch === ')') break;
      if (ch === '>' || ch === '+' || ch === '~') { comb = ch; i++; ws(); }
      else if (i > before) comb = ' ';
      else throw selError(text);
      parts.push({ comb, c: compound() });
    }
    return parts;
  }
  const list = [];
  for (;;) {
    list.push(complex());
    ws();
    if (text[i] === ',') { i++; continue; }
    break;
  }
  if (i < n) throw selError(text);
  return list;
}
function parseRelative(text) {
  // :has(> a, + b, c) -> list of {comb, complex}
  return text.split(/,(?![^(]*\))/).map(part => {
    part = part.trim();
    let comb = ' ';
    if (/^[>+~]/.test(part)) { comb = part[0]; part = part.slice(1).trim(); }
    return { comb, sel: parseSelectorList(part) };
  });
}
function getSelector(sel) {
  sel = String(sel);
  let p = selCache.get(sel);
  if (!p) {
    if (selCache.size > 500) selCache.clear();
    p = parseSelectorList(sel.trim());
    selCache.set(sel, p);
  }
  return p;
}

const sibIndex = (el, sameType, fromEnd) => {
  const p = el._p;
  if (!p) return 1;
  let idx = 0;
  const list = p._c;
  if (fromEnd) { for (let k = list.length - 1; k >= 0; k--) { const s = list[k]; if (s.nodeType !== 1) continue; if (sameType && s.localName !== el.localName) continue; idx++; if (s === el) return idx; } }
  else { for (const s of list) { if (s.nodeType !== 1) continue; if (sameType && s.localName !== el.localName) continue; idx++; if (s === el) return idx; } }
  return idx;
};
function nthMatch(a, b, idx) {
  if (a === 0) return idx === b;
  const k = (idx - b) / a;
  return Number.isInteger(k) && k >= 0;
}
function matchPseudo(el, p, scope) {
  if (p.never) return false;
  switch (p.name) {
    case 'not': return !p.list.some(c => matchComplex(el, c, scope));
    case 'is': case 'where': case 'matches': case '-webkit-any': case '-moz-any': return p.list.some(c => matchComplex(el, c, scope));
    case 'has': return p.list.some(r => hasMatch(el, r, scope));
    case 'first-child': return sibIndex(el, false, false) === 1;
    case 'last-child': return sibIndex(el, false, true) === 1;
    case 'only-child': return sibIndex(el, false, false) === 1 && sibIndex(el, false, true) === 1;
    case 'first-of-type': return sibIndex(el, true, false) === 1;
    case 'last-of-type': return sibIndex(el, true, true) === 1;
    case 'only-of-type': return sibIndex(el, true, false) === 1 && sibIndex(el, true, true) === 1;
    case 'nth-child': if (p.of) { if (!p.of.some(c => matchComplex(el, c, scope))) return false; let idx = 0; for (const s of el._p._c) { if (s.nodeType === 1 && p.of.some(c => matchComplex(s, c, scope))) idx++; if (s === el) break; } return nthMatch(p.nth[0], p.nth[1], idx); } return nthMatch(p.nth[0], p.nth[1], sibIndex(el, false, false));
    case 'nth-last-child': return nthMatch(p.nth[0], p.nth[1], sibIndex(el, false, true));
    case 'nth-of-type': return nthMatch(p.nth[0], p.nth[1], sibIndex(el, true, false));
    case 'nth-last-of-type': return nthMatch(p.nth[0], p.nth[1], sibIndex(el, true, true));
    case 'empty': return !el._c || !el._c.some(c => c.nodeType === 1 || (c.nodeType === 3 && c._d !== ''));
    case 'root': return el === el.ownerDocument.documentElement;
    case 'scope': return scope ? el === scope : el === el.ownerDocument.documentElement;
    case 'checked': return (el.localName === 'input' && (el.type === 'checkbox' || el.type === 'radio') && el.checked) || (el.localName === 'option' && el.selected);
    case 'disabled': return !!el.disabled || (formLike(el) && !!el.closest('fieldset[disabled]'));
    case 'enabled': return formLike(el) && !el.disabled;
    case 'required': return formLike(el) && el.hasAttribute('required');
    case 'optional': return formLike(el) && !el.hasAttribute('required');
    case 'read-only': return !(el.localName === 'input' || el.localName === 'textarea') || el.hasAttribute('readonly') || el.disabled;
    case 'read-write': return (el.localName === 'input' || el.localName === 'textarea') && !el.hasAttribute('readonly') && !el.disabled;
    case 'focus': case 'focus-visible': return el.ownerDocument._focus === el;
    case 'focus-within': return !!el.ownerDocument._focus && el.contains(el.ownerDocument._focus);
    case 'hover': case 'active': case 'visited': case 'target': case 'fullscreen': case 'modal': case 'popover-open': return false;
    case 'link': case 'any-link': return (el.localName === 'a' || el.localName === 'area') && el.hasAttribute('href');
    case 'defined': return !el.localName.includes('-') || K.customByName.has(el.localName);
    case 'placeholder-shown': return (el.localName === 'input' || el.localName === 'textarea') && el.hasAttribute('placeholder') && el.value === '';
    case 'indeterminate': return el.localName === 'input' && !!el.indeterminate;
    case 'default': return (el.localName === 'input' && el.hasAttribute('checked')) || (el.localName === 'option' && el.hasAttribute('selected'));
    case 'valid': case 'in-range': case 'user-valid': return formLike(el);
    case 'invalid': case 'out-of-range': case 'user-invalid': return false;
    case 'lang': { for (let n = el; n && n.nodeType === 1; n = n._p) { const l = n.getAttribute('lang'); if (l !== null) return l.toLowerCase().startsWith(String(p.arg).toLowerCase().replace(/["']/g, '')); } return false; }
    case 'dir': return String(p.arg).trim() === 'ltr';
    case 'host': return !!el._sr;
    case 'open': return el.hasAttribute('open');
    case 'autofill': case 'blank': return false;
    default: throw selError(':' + p.name);
  }
}
const formLike = (el) => el.localName === 'input' || el.localName === 'button' || el.localName === 'select' || el.localName === 'textarea' || el.localName === 'fieldset' || el.localName === 'option';
function hasMatch(el, rel, scope) {
  const test = (c) => rel.sel.some(s => matchComplex(c, s, el));
  const c = rel.comb;
  if (c === ' ') { const all = collect(el, () => true); return all.some(test); }
  if (c === '>') return !!el._c && el._c.some(x => x.nodeType === 1 && test(x));
  if (c === '+') { let n = el._next; while (n && n.nodeType !== 1) n = n._next; return !!n && test(n); }
  if (c === '~') { for (let n = el._next; n; n = n._next) if (n.nodeType === 1 && test(n)) return true; return false; }
  return false;
}
function matchCompound(el, c, scope) {
  if (c.tag && c.tag !== '*') {
    if (el.namespaceURI === HTML_NS && el.ownerDocument._html !== false) { if (el.localName !== c.tag.toLowerCase()) return false; }
    else if (el.localName !== c.tag) return false;
  }
  if (c.id !== null && el.getAttribute('id') !== c.id) return false;
  if (c.classes.length) {
    const cls = el.getAttribute('class');
    if (!cls) return false;
    const have = cls.split(/\s+/);
    for (const k of c.classes) if (!have.includes(k)) return false;
  }
  for (const a of c.attrs) {
    const v0 = el.getAttribute(a.name);
    if (v0 === null) return false;
    if (!a.op) continue;
    let v = v0, w = a.val;
    if (a.ci) { v = v.toLowerCase(); w = w.toLowerCase(); }
    switch (a.op) {
      case '=': if (v !== w) return false; break;
      case '~=': if (!w || !v.split(/\s+/).includes(w)) return false; break;
      case '|=': if (v !== w && !v.startsWith(w + '-')) return false; break;
      case '^=': if (!w || !v.startsWith(w)) return false; break;
      case '$=': if (!w || !v.endsWith(w)) return false; break;
      case '*=': if (!w || !v.includes(w)) return false; break;
    }
  }
  for (const p of c.pseudos) if (!matchPseudo(el, p, scope)) return false;
  return true;
}
function matchComplex(el, parts, scope) {
  return matchFrom(el, parts, parts.length - 1, scope);
}
function matchFrom(el, parts, idx, scope) {
  if (!matchCompound(el, parts[idx].c, scope)) return false;
  if (idx === 0) return true;
  const comb = parts[idx].comb;
  if (comb === '>') { const p = el._p; return !!p && p.nodeType === 1 && matchFrom(p, parts, idx - 1, scope); }
  if (comb === ' ') { for (let p = el._p; p && p.nodeType === 1; p = p._p) if (matchFrom(p, parts, idx - 1, scope)) return true; return false; }
  if (comb === '+') { let s = el._prev; while (s && s.nodeType !== 1) s = s._prev; return !!s && matchFrom(s, parts, idx - 1, scope); }
  if (comb === '~') { for (let s = el._prev; s; s = s._prev) if (s.nodeType === 1 && matchFrom(s, parts, idx - 1, scope)) return true; return false; }
  return false;
}
function matchesSelector(el, sel, scope) {
  const list = getSelector(sel);
  return list.some(c => matchComplex(el, c, scope));
}
K.matches = matchesSelector;
function queryAll(root, sel) {
  const list = getSelector(sel);
  const out = new NodeList();
  const scope = root.nodeType === 1 ? root : null;
  const walk = (n) => {
    if (!n._c) return;
    for (const c of n._c) {
      if (c.nodeType !== 1) continue;
      for (const s of list) if (matchComplex(c, s, scope)) { out.push(c); break; }
      walk(c);
    }
  };
  walk(root);
  return out;
}
function queryOne(root, sel) {
  const list = getSelector(sel);
  const scope = root.nodeType === 1 ? root : null;
  let found = null;
  const walk = (n) => {
    if (!n._c) return false;
    for (const c of n._c) {
      if (c.nodeType !== 1) continue;
      for (const s of list) if (matchComplex(c, s, scope)) { found = c; return true; }
      if (walk(c)) return true;
    }
    return false;
  };
  walk(root);
  return found;
}

// 30_parse.js - reading HTML into nodes (innerHTML, the page itself) and
// writing nodes back out as HTML.

// ---- Character references ----
const entityTable = (() => {
  const t = Object.create(null);
  const latin = 'nbsp iexcl cent pound curren yen brvbar sect uml copy ordf laquo not shy reg macr deg plusmn sup2 sup3 acute micro para middot cedil sup1 ordm raquo frac14 frac12 frac34 iquest Agrave Aacute Acirc Atilde Auml Aring AElig Ccedil Egrave Eacute Ecirc Euml Igrave Iacute Icirc Iuml ETH Ntilde Ograve Oacute Ocirc Otilde Ouml times Oslash Ugrave Uacute Ucirc Uuml Yacute THORN szlig agrave aacute acirc atilde auml aring aelig ccedil egrave eacute ecirc euml igrave iacute icirc iuml eth ntilde ograve oacute ocirc otilde ouml divide oslash ugrave uacute ucirc uuml yacute thorn yuml'.split(' ');
  latin.forEach((n, i) => { t[n] = String.fromCharCode(160 + i); });
  const more = {
    amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", hellip: '…', ndash: '–', mdash: '—', lsquo: '‘', rsquo: '’', sbquo: '‚', ldquo: '“', rdquo: '”', bdquo: '„',
    bull: '•', trade: '™', larr: '←', uarr: '↑', rarr: '→', darr: '↓', harr: '↔', lArr: '⇐', uArr: '⇑', rArr: '⇒', dArr: '⇓', hArr: '⇔',
    minus: '−', infin: '∞', ne: '≠', le: '≤', ge: '≥', asymp: '≈', equiv: '≡', sum: '∑', prod: '∏', radic: '√', part: '∂', nabla: '∇', isin: '∈', cap: '∩', cup: '∪', int: '∫', there4: '∴', sim: '∼', sub: '⊂', sup: '⊃', forall: '∀', exist: '∃', empty: '∅', and: '∧', or: '∨', oplus: '⊕', otimes: '⊗', perp: '⊥', sdot: '⋅',
    euro: '€', permil: '‰', prime: '′', Prime: '″', dagger: '†', Dagger: '‡', lsaquo: '‹', rsaquo: '›', oline: '‾', frasl: '⁄', fnof: 'ƒ', circ: 'ˆ', tilde: '˜',
    ensp: ' ', emsp: ' ', thinsp: ' ', zwnj: '‌', zwj: '‍', lrm: '‎', rlm: '‏', OElig: 'Œ', oelig: 'œ', Scaron: 'Š', scaron: 'š', Yuml: 'Ÿ',
    spades: '♠', clubs: '♣', hearts: '♥', diams: '♦', loz: '◊', check: '✓', cross: '✗', star: '☆', starf: '★', phone: '☎', female: '♀', male: '♂',
    Alpha: 'Α', Beta: 'Β', Gamma: 'Γ', Delta: 'Δ', Epsilon: 'Ε', Zeta: 'Ζ', Eta: 'Η', Theta: 'Θ', Iota: 'Ι', Kappa: 'Κ', Lambda: 'Λ', Mu: 'Μ', Nu: 'Ν', Xi: 'Ξ', Omicron: 'Ο', Pi: 'Π', Rho: 'Ρ', Sigma: 'Σ', Tau: 'Τ', Upsilon: 'Υ', Phi: 'Φ', Chi: 'Χ', Psi: 'Ψ', Omega: 'Ω',
    alpha: 'α', beta: 'β', gamma: 'γ', delta: 'δ', epsilon: 'ε', zeta: 'ζ', eta: 'η', theta: 'θ', iota: 'ι', kappa: 'κ', lambda: 'λ', mu: 'μ', nu: 'ν', xi: 'ξ', omicron: 'ο', pi: 'π', rho: 'ρ', sigmaf: 'ς', sigma: 'σ', tau: 'τ', upsilon: 'υ', phi: 'φ', chi: 'χ', psi: 'ψ', omega: 'ω',
    lceil: '⌈', rceil: '⌉', lfloor: '⌊', rfloor: '⌋', lang: '〈', rang: '〉', NewLine: '\n', Tab: '\t', excl: '!', num: '#', dollar: '$', percnt: '%', lpar: '(', rpar: ')', ast: '*', plus: '+', comma: ',', period: '.', sol: '/', colon: ':', semi: ';', equals: '=', quest: '?', commat: '@', lsqb: '[', bsol: '\\', rsqb: ']', Hat: '^', lowbar: '_', grave: '`', lcub: '{', verbar: '|', vert: '|', rcub: '}', hyphen: '‐', dash: '‐',
  };
  Object.assign(t, more);
  return t;
})();
const legacyEntities = new Set('amp lt gt quot nbsp copy reg deg plusmn middot laquo raquo times divide eacute egrave agrave aacute uuml ouml auml szlig ccedil ntilde iexcl cent pound yen sect para micro frac12 frac14 frac34 iquest'.split(' '));
const win1252 = { 0x80: 0x20ac, 0x82: 0x201a, 0x83: 0x192, 0x84: 0x201e, 0x85: 0x2026, 0x86: 0x2020, 0x87: 0x2021, 0x88: 0x2c6, 0x89: 0x2030, 0x8a: 0x160, 0x8b: 0x2039, 0x8c: 0x152, 0x8e: 0x17d, 0x91: 0x2018, 0x92: 0x2019, 0x93: 0x201c, 0x94: 0x201d, 0x95: 0x2022, 0x96: 0x2013, 0x97: 0x2014, 0x98: 0x2dc, 0x99: 0x2122, 0x9a: 0x161, 0x9b: 0x203a, 0x9c: 0x153, 0x9e: 0x17e, 0x9f: 0x178 };
function decodeEntities(s, inAttr) {
  if (s.indexOf('&') < 0) return s;
  return s.replace(/&(#[xX][0-9a-fA-F]+|#[0-9]+|[A-Za-z][A-Za-z0-9]*);?/g, (m, body) => {
    if (body[0] === '#') {
      let cp = body[1] === 'x' || body[1] === 'X' ? parseInt(body.slice(2), 16) : parseInt(body.slice(1), 10);
      if (!cp || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return '�';
      if (win1252[cp]) cp = win1252[cp];
      return String.fromCodePoint(cp);
    }
    const hasSemi = m[m.length - 1] === ';';
    if (entityTable[body] !== undefined && (hasSemi || legacyEntities.has(body))) return entityTable[body];
    if (!hasSemi) {
      // legacy names can be followed directly by letters ("&copyright")
      for (let k = Math.min(body.length - 1, 6); k >= 2; k--) {
        const p = body.slice(0, k);
        if (legacyEntities.has(p) && entityTable[p] !== undefined && !inAttr) return entityTable[p] + body.slice(k);
      }
    }
    return m;
  });
}

// ---- The tokenizer + tree builder ----
const rawTextTags = new Set(['script', 'style', 'xmp', 'iframe', 'noembed', 'noframes', 'plaintext', 'noscript']);
const rcdataTags = new Set(['textarea', 'title']);
const closesP = new Set('address article aside blockquote center details dialog dir div dl fieldset figcaption figure footer form h1 h2 h3 h4 h5 h6 header hgroup hr li listing main menu nav ol p pre section summary table ul xmp'.split(' '));
const headTags = new Set(['title', 'meta', 'link', 'script', 'style', 'base', 'noscript', 'template', 'basefont', 'bgsound', 'noframes']);
const scopeBarrier = new Set(['applet', 'caption', 'html', 'table', 'td', 'th', 'marquee', 'object', 'template', 'button']);
const svgCase = 'altGlyph altGlyphDef altGlyphItem animateColor animateMotion animateTransform clipPath feBlend feColorMatrix feComponentTransfer feComposite feConvolveMatrix feDiffuseLighting feDisplacementMap feDistantLight feDropShadow feFlood feFuncA feFuncB feFuncG feFuncR feGaussianBlur feImage feMerge feMergeNode feMorphology feOffset fePointLight feSpecularLighting feSpotLight feTile feTurbulence foreignObject glyphRef linearGradient radialGradient textPath'.split(' ');
const svgCaseMap = Object.create(null);
for (const n of svgCase) svgCaseMap[n.toLowerCase()] = n;

function appendRaw(parent, child) {
  if (!parent._c) parent._c = new NodeList();
  const list = parent._c;
  const prev = list.length ? list[list.length - 1] : null;
  list.push(child);
  child._p = parent; child._prev = prev; child._next = null;
  if (prev) prev._next = child;
}

// Parses `html` into `root` (a Document or a fragment). `ctx` is the context
// element for innerHTML (or null for a whole document).
function parseInto(root, html, ctx, doc, isMain) {
  const n = html.length;
  let i = 0;
  const wholeDoc = root.nodeType === 9;
  const stack = [];                 // open elements
  let head = null, body = null, htmlEl = null;
  let mode = wholeDoc ? 'initial' : 'body';
  let foster = false;
  const top = () => stack.length ? stack[stack.length - 1] : root;
  const topName = () => stack.length ? stack[stack.length - 1].localName : (ctx ? ctx.localName : '');
  const inForeign = () => { const t = top(); return t.nodeType === 1 && t.namespaceURI !== HTML_NS && !(t.localName === 'foreignObject' || t.localName === 'desc' || t.localName === 'title'); };

  function makeEl(name, attrs, ns) {
    const el = createElementImpl(doc, name, ns, null);
    if (attrs.length) {
      el._a = new Map();
      for (const [k, v] of attrs) if (!el._a.has(k)) el._a.set(k, v);
      for (const [k, v] of el._a) {
        if (k.length > 2 && k[0] === 'o' && k[1] === 'n') el._attrChanged(k, null, v);
        else if (el._attrHook) el._attrHook(k, null, v);
      }
    }
    if (name === 'script') { el._parserInserted = isMain; el._started = !isMain; }
    return el;
  }
  function ensureHtml() {
    if (!wholeDoc || htmlEl) return;
    htmlEl = makeEl('html', [], HTML_NS);
    appendRaw(root, htmlEl);
    stack.push(htmlEl);
  }
  function ensureHead() {
    ensureHtml();
    if (head || !wholeDoc) return;
    head = makeEl('head', [], HTML_NS);
    appendRaw(htmlEl, head);
  }
  function ensureBody() {
    if (!wholeDoc || body) return;
    ensureHtml();
    ensureHead();
    // leave the head
    while (stack.length > 1) stack.pop();
    body = makeEl('body', [], HTML_NS);
    appendRaw(htmlEl, body);
    stack.push(body);
    mode = 'body';
  }
  function addText(text) {
    if (!text) return;
    if (wholeDoc && mode !== 'body') {
      if (/^[ \t\n\r\f]*$/.test(text)) {
        if (mode === 'head' && head) { /* keep whitespace in head */ }
        return;
      }
      if (mode === 'initial' || mode === 'head') ensureBody();
    }
    const parent = top();
    const last = parent._c && parent._c.length ? parent._c[parent._c.length - 1] : null;
    if (last && last.nodeType === 3) { last._d += text; return; }
    const t = new Text(text);
    t.ownerDocument = doc;
    appendRaw(parent, t);
  }
  function addComment(text) {
    const c = new Comment(text);
    c.ownerDocument = doc;
    if (wholeDoc && !stack.length) appendRaw(root, c); else appendRaw(top(), c);
  }
  function popTo(idx) { while (stack.length > idx) stack.pop(); }
  function findOpen(name, stopAtBarrier) {
    for (let k = stack.length - 1; k >= 0; k--) {
      const e = stack[k];
      if (e.localName === name && e.namespaceURI === HTML_NS) return k;
      if (stopAtBarrier && scopeBarrier.has(e.localName) && e.namespaceURI === HTML_NS) return -1;
    }
    return -1;
  }
  function closeP() { const k = findOpen('p', true); if (k >= 0) popTo(k); }

  function startTag(name, attrs, selfClosing) {
    // name is lower case; `orig` case kept for foreign content
    const foreign = inForeign();
    if (!foreign) {
      if (wholeDoc && mode !== 'body') {
        if (name === 'html') { ensureHtml(); for (const [k, v] of attrs) if (!htmlEl.hasAttribute(k)) htmlEl.setAttribute(k, v); mode = mode === 'initial' ? 'before-head' : mode; return; }
        if (name === 'head') { ensureHtml(); if (!head) { head = makeEl('head', attrs, HTML_NS); appendRaw(htmlEl, head); stack.push(head); } mode = 'head'; return; }
        if (name === 'body') { ensureHead(); while (stack.length > 1) stack.pop(); body = makeEl('body', attrs, HTML_NS); appendRaw(htmlEl, body); stack.push(body); mode = 'body'; return; }
        if (headTags.has(name)) {
          ensureHead();
          if (top() !== head) stack.push(head);
          mode = 'head';
        } else {
          ensureBody();
        }
      } else if (wholeDoc) {
        if (name === 'html') { for (const [k, v] of attrs) if (!htmlEl.hasAttribute(k)) htmlEl.setAttribute(k, v); return; }
        if (name === 'body') { if (body) for (const [k, v] of attrs) if (!body.hasAttribute(k)) body.setAttribute(k, v); return; }
        if (name === 'head') return;
      } else {
        if (name === 'html' || name === 'head' || name === 'body') return;
      }
      // implied end tags
      if (closesP.has(name)) closeP();
      switch (name) {
        case 'li': { for (let k = stack.length - 1; k >= 0; k--) { const e = stack[k]; if (e.localName === 'li') { popTo(k); break; } if (e.localName === 'ul' || e.localName === 'ol' || scopeBarrier.has(e.localName)) break; } break; }
        case 'dd': case 'dt': { for (let k = stack.length - 1; k >= 0; k--) { const e = stack[k]; if (e.localName === 'dd' || e.localName === 'dt') { popTo(k); break; } if (e.localName === 'dl' || scopeBarrier.has(e.localName)) break; } break; }
        case 'option': if (topName() === 'option') stack.pop(); break;
        case 'optgroup': if (topName() === 'option') stack.pop(); if (topName() === 'optgroup') stack.pop(); break;
        case 'h1': case 'h2': case 'h3': case 'h4': case 'h5': case 'h6': if (/^h[1-6]$/.test(topName())) stack.pop(); break;
        case 'a': { const k = findOpen('a', true); if (k >= 0) popTo(k); break; }
        case 'button': { const k = findOpen('button', true); if (k >= 0) popTo(k); break; }
        case 'nobr': { const k = findOpen('nobr', true); if (k >= 0) popTo(k); break; }
        case 'tr': {
          if (topName() === 'table' || (!stack.length && ctx && ctx.localName === 'table')) { const tb = makeEl('tbody', [], HTML_NS); appendRaw(top(), tb); stack.push(tb); }
          else { for (let k = stack.length - 1; k >= 0; k--) { const e = stack[k]; if (e.localName === 'tr') { popTo(k); break; } if (e.localName === 'tbody' || e.localName === 'thead' || e.localName === 'tfoot' || e.localName === 'table') break; } }
          break;
        }
        case 'td': case 'th': { for (let k = stack.length - 1; k >= 0; k--) { const e = stack[k]; if (e.localName === 'td' || e.localName === 'th') { popTo(k); break; } if (e.localName === 'tr' || e.localName === 'table') break; } break; }
        case 'thead': case 'tbody': case 'tfoot': { for (let k = stack.length - 1; k >= 0; k--) { const e = stack[k]; if (e.localName === 'thead' || e.localName === 'tbody' || e.localName === 'tfoot' || e.localName === 'tr' || e.localName === 'td' || e.localName === 'th') { if (e.localName !== 'table') { popTo(k); } } else if (e.localName === 'table') break; } break; }
        case 'caption': case 'colgroup': break;
        case 'col': if (topName() === 'table') { const cg = makeEl('colgroup', [], HTML_NS); appendRaw(top(), cg); } break;
        case 'rp': case 'rt': if (topName() === 'rb' || topName() === 'rt' || topName() === 'rp') stack.pop(); break;
        case 'form': if (findOpen('form', false) >= 0 && stack.every(e => e.localName !== 'template')) return; break;
        case 'image': name = 'img'; break;
      }
    }
    let ns = HTML_NS;
    let local = name;
    if (name === 'svg' || name === 'math') ns = name === 'svg' ? SVG_NS : MATHML_NS;
    else if (foreign) { const t = top(); ns = t.namespaceURI; if (ns === SVG_NS && svgCaseMap[name]) local = svgCaseMap[name]; }
    const el = makeEl(local, ns === HTML_NS ? attrs : attrs.map(([k, v]) => [k, v]), ns);
    appendRaw(top(), el);
    const isVoid = ns === HTML_NS && voidTags.has(name);
    if (isVoid) return;
    if (ns !== HTML_NS && selfClosing) return;
    stack.push(el);
    if (name === 'template' && ns === HTML_NS) { /* content goes to template.content */ el._tpl = true; }
  }
  function endTag(name) {
    if (!inForeign() || stack.some(e => e.localName.toLowerCase() === name)) {
      if (name === 'body' || name === 'html') { return; }
      if (name === 'p' && findOpen('p', true) < 0) { startTag('p', [], false); closeP(); return; }
      if (name === 'br') { startTag('br', [], false); return; }
      for (let k = stack.length - 1; k >= 0; k--) {
        const e = stack[k];
        if (e.localName.toLowerCase() === name) { popTo(k); return; }
        if (e.namespaceURI === HTML_NS && scopeBarrier.has(e.localName) && e.localName !== 'button' && !/^(span|a|b|i|em|strong|font|u|small|code|label)$/.test(name)) return;
      }
    }
  }

  while (i < n) {
    const lt = html.indexOf('<', i);
    if (lt < 0) { addText(decodeEntities(html.slice(i), false)); break; }
    if (lt > i) addText(decodeEntities(html.slice(i, lt), false));
    i = lt;
    const c1 = html[i + 1];
    if (c1 === '!') {
      if (html.startsWith('<!--', i)) {
        let end = html.indexOf('-->', i + 4);
        if (end < 0) end = n;
        addComment(html.slice(i + 4, end));
        i = end + 3;
      } else if (html.startsWith('<![CDATA[', i) && inForeign()) {
        let end = html.indexOf(']]>', i);
        if (end < 0) end = n;
        addText(html.slice(i + 9, end));
        i = end + 3;
      } else {
        let end = html.indexOf('>', i);
        if (end < 0) end = n;
        const m = /^<!doctype\s+([^\s>]*)/i.exec(html.slice(i, end + 1));
        if (m && wholeDoc && mode === 'initial') {
          const dt = new DocumentType(m[1].toLowerCase());
          dt.ownerDocument = doc;
          appendRaw(root, dt);
        }
        i = end + 1;
      }
      continue;
    }
    if (c1 === '?') { let end = html.indexOf('>', i); if (end < 0) end = n; addComment(html.slice(i + 1, end)); i = end + 1; continue; }
    if (c1 === '/') {
      const m = /^<\/([A-Za-z][^\s/>]*)[^>]*>?/.exec(html.slice(i, i + 200));
      if (m) { endTag(m[1].toLowerCase()); i += m[0].length; continue; }
      if (html[i + 2] === '>') { i += 3; continue; }
      let end = html.indexOf('>', i); i = end < 0 ? n : end + 1; continue;
    }
    if (!c1 || !/[A-Za-z]/.test(c1)) { addText('<'); i++; continue; }
    // a start tag
    let j = i + 1;
    while (j < n && !/[\s/>]/.test(html[j])) j++;
    const origName = html.slice(i + 1, j);
    const name = origName.toLowerCase();
    const attrs = [];
    let selfClosing = false;
    let foreignTag = false;
    {
      const t = top();
      foreignTag = (t.nodeType === 1 && t.namespaceURI !== HTML_NS && !(t.localName === 'foreignObject')) || name === 'svg' || name === 'math';
    }
    for (;;) {
      while (j < n && /[\s]/.test(html[j])) j++;
      if (j >= n) break;
      if (html[j] === '>') { j++; break; }
      if (html[j] === '/') { if (html[j + 1] === '>') { selfClosing = true; j += 2; break; } j++; continue; }
      let k = j;
      while (k < n && !/[\s=/>]/.test(html[k]) || (k === j && html[k] === '=')) k++;
      let aname = html.slice(j, k);
      if (!foreignTag) aname = aname.toLowerCase();
      j = k;
      while (j < n && /\s/.test(html[j])) j++;
      let val = '';
      if (html[j] === '=') {
        j++;
        while (j < n && /\s/.test(html[j])) j++;
        const q = html[j];
        if (q === '"' || q === "'") {
          const e = html.indexOf(q, j + 1);
          const end = e < 0 ? n : e;
          val = html.slice(j + 1, end);
          j = end + 1;
        } else {
          let e = j;
          while (e < n && !/[\s>]/.test(html[e])) e++;
          val = html.slice(j, e);
          j = e;
        }
        val = decodeEntities(val, true);
      }
      attrs.push([aname, val]);
    }
    i = j;
    startTag(name, attrs, selfClosing);
    // raw text content
    const cur = top();
    if (cur.nodeType === 1 && cur.namespaceURI === HTML_NS && cur.localName === name && !voidTags.has(name)) {
      if (rawTextTags.has(name) || rcdataTags.has(name)) {
        const closeRe = new RegExp('</' + name + '[\\s/>]', 'i');
        const m = closeRe.exec(html.slice(i));
        const end = m ? i + m.index : n;
        let text = html.slice(i, end);
        if (name === 'plaintext') { text = html.slice(i); }
        if (rcdataTags.has(name)) text = decodeEntities(text, false);
        if (text) { const t = new Text(text); t.ownerDocument = doc; appendRaw(cur, t); }
        i = end;
        const close = html.indexOf('>', i);
        i = m ? (close < 0 ? n : close + 1) : n;
        stack.pop();
        if (name === 'title' && wholeDoc) { /* title lives in head */ }
      } else if (name === 'textarea' || name === 'pre' || name === 'listing') {
        if (html[i] === '\n') i++;
      }
    }
  }
  if (wholeDoc) {
    ensureHtml();
    ensureHead();
    if (!body) { body = makeEl('body', [], HTML_NS); appendRaw(htmlEl, body); }
  }
}
// <template> contents: parsed into the content fragment
function fixTemplates(root) {
  const walk = (n) => {
    if (!n._c) return;
    for (const c of n._c.slice()) {
      if (c.nodeType !== 1) continue;
      if (c._tpl && c.localName === 'template') {
        const kids = c._c || [];
        c._c = null;
        const content = c.content;
        for (const k of kids) { k._p = null; k._prev = k._next = null; appendRaw(content, k); k.ownerDocument = content.ownerDocument; adopt(k, content.ownerDocument); }
        fixTemplates(content);
      } else walk(c);
    }
  };
  walk(root);
}
function parseFragment(html, ctx, doc) {
  const frag = new DocumentFragment();
  frag.ownerDocument = doc;
  parseInto(frag, html, ctx, doc, false);
  fixTemplates(frag);
  return frag;
}
function setInnerHTML(el, html) {
  html = html == null ? '' : String(html);
  const target = el.localName === 'template' && el.content ? el.content : el;
  const ctx = el.nodeType === 1 ? el : (el.host || null);
  const frag = parseFragment(html, ctx, el.ownerDocument);
  target._removeAll();
  if (frag._c && frag._c.length) target.appendChild(frag);
}
K.parseFragment = parseFragment;
K.parseDocumentInto = function (doc, html) { parseInto(doc, html, null, doc, true); fixTemplates(doc); };

// ---- Writing HTML ----
const rawSerialize = new Set(['script', 'style', 'xmp', 'iframe', 'noembed', 'noframes', 'plaintext', 'noscript']);
function escText(s) { return s.replace(/&/g, '&amp;').replace(/ /g, '&nbsp;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); }
function escAttr(s) { return s.replace(/&/g, '&amp;').replace(/ /g, '&nbsp;').replace(/"/g, '&quot;'); }
function serializeNode(n) {
  switch (n.nodeType) {
    case 1: {
      const name = n.namespaceURI === HTML_NS ? n.localName : (n.prefix ? n.prefix + ':' : '') + n.localName;
      let out = '<' + name;
      if (n._a) for (const [k, v] of n._a) out += ' ' + k + '="' + escAttr(v) + '"';
      out += '>';
      if (n.namespaceURI === HTML_NS && voidTags.has(n.localName)) return out;
      if (n.localName === 'template' && n.namespaceURI === HTML_NS) out += serializeChildren(n.content);
      else out += serializeChildren(n);
      return out + '</' + name + '>';
    }
    case 3: return n._p && n._p.nodeType === 1 && rawSerialize.has(n._p.localName) && n._p.namespaceURI === HTML_NS ? n._d : escText(n._d);
    case 4: return '<![CDATA[' + n._d + ']]>';
    case 8: return '<!--' + n._d + '-->';
    case 10: return '<!DOCTYPE ' + n.name + '>';
    case 11: case 9: return serializeChildren(n);
  }
  return '';
}
function serializeChildren(n) {
  let out = '';
  if (n._c) for (const c of n._c) out += serializeNode(c);
  return out;
}
K.serializeNode = serializeNode;

class DOMParser {
  parseFromString(str, type) {
    const doc = new HTMLDocument(type !== 'text/html');
    if (type === 'text/html') { K.parseDocumentInto(doc, String(str)); }
    else { const frag = parseFragment(String(str), null, doc); doc.appendChild(frag); }
    return doc;
  }
}
publish('DOMParser', DOMParser);
publish('XMLSerializer', class XMLSerializer { serializeToString(n) { return serializeNode(n); } });

// 40_url.js - URL, URLSearchParams, location and history.

const specialPorts = { 'http:': '80', 'https:': '443', 'ftp:': '21', 'ws:': '80', 'wss:': '443', 'file:': '' };
const pctEncode = (s, keep) => {
  let out = '';
  for (const ch of String(s)) {
    if (ch.length === 1 && ch.charCodeAt(0) < 0x80) {
      const c = ch.charCodeAt(0);
      if (c > 0x20 && c < 0x7f && !keep.includes(ch)) { out += ch; continue; }
      if (c <= 0x20 || keep.includes(ch) || c === 0x7f) { out += '%' + (c < 16 ? '0' : '') + c.toString(16).toUpperCase(); continue; }
    }
    out += encodeURIComponent(ch);
  }
  return out;
};
const PATH_ESC = ' "#<>?`{}';
const QUERY_ESC = ' "#<>\'';
const FRAG_ESC = ' "<>`';
function normalizePath(path, special) {
  const segs = path.split(special ? /[/\\]/ : '/');
  const out = [];
  for (let k = 0; k < segs.length; k++) {
    const s = segs[k];
    const last = k === segs.length - 1;
    if (s === '..' || s.toLowerCase() === '%2e%2e' || s.toLowerCase() === '.%2e' || s.toLowerCase() === '%2e.') { if (out.length > 1) out.pop(); if (last) out.push(''); }
    else if (s === '.' || s.toLowerCase() === '%2e') { if (last) out.push(''); }
    else out.push(s);
  }
  return out.join('/');
}
function parseURL(input, base) {
  input = String(input).replace(/^[\u0000- ]+|[\u0000- ]+$/g, '').replace(/[\t\n\r]/g, '');
  let m = /^([a-zA-Z][a-zA-Z0-9+.\-]*):/.exec(input);
  const r = { protocol: '', username: '', password: '', hostname: '', port: '', pathname: '', search: '', hash: '', opaque: false };
  let rest;
  if (m) {
    r.protocol = m[1].toLowerCase() + ':';
    rest = input.slice(m[0].length);
    const special = r.protocol in specialPorts;
    if (special && base && !/^[/\\]{2}/.test(rest) && base.protocol === r.protocol && r.protocol !== 'file:') {
      // "http:foo" relative to a base with the same scheme
      return parseURL(rest, base);
    }
    if (!special) {
      if (rest.startsWith('//')) return finishAuthority(r, rest.slice(2), false);
      r.opaque = !rest.startsWith('/');
      if (r.opaque) {
        let h = rest.indexOf('#'); if (h >= 0) { r.hash = '#' + pctEncode(rest.slice(h + 1), FRAG_ESC); rest = rest.slice(0, h); }
        let q = rest.indexOf('?'); if (q >= 0) { r.search = '?' + pctEncode(rest.slice(q + 1), QUERY_ESC); rest = rest.slice(0, q); }
        r.pathname = rest;
        return r;
      }
      return finishPath(r, rest, false);
    }
    return finishAuthority(r, rest.replace(/^[/\\]*/, ''), true);
  }
  if (!base) throw new TypeError("Failed to construct 'URL': Invalid URL");
  r.protocol = base.protocol;
  const special = r.protocol in specialPorts;
  if (base.opaque) {
    if (input[0] !== '#') throw new TypeError("Failed to construct 'URL': Invalid URL");
    Object.assign(r, base); r.hash = '#' + pctEncode(input.slice(1), FRAG_ESC); return r;
  }
  if (/^[/\\]{2}/.test(input) && special) return finishAuthority(r, input.slice(2), true);
  if (input.startsWith('//')) return finishAuthority(r, input.slice(2), false);
  r.username = base.username; r.password = base.password; r.hostname = base.hostname; r.port = base.port;
  if (input === '') { r.pathname = base.pathname; r.search = base.search; return r; }
  if (input[0] === '#') { r.pathname = base.pathname; r.search = base.search; r.hash = input.length > 1 ? '#' + pctEncode(input.slice(1), FRAG_ESC) : ''; return r; }
  if (input[0] === '?') {
    r.pathname = base.pathname;
    let h = input.indexOf('#'); let frag = ''; if (h >= 0) { frag = input.slice(h + 1); input = input.slice(0, h); r.hash = '#' + pctEncode(frag, FRAG_ESC); }
    r.search = input.length > 1 ? '?' + pctEncode(input.slice(1), QUERY_ESC) : '';
    return r;
  }
  if (input[0] === '/' || (special && input[0] === '\\')) return finishPath(r, input, special);
  const dir = base.pathname.slice(0, base.pathname.lastIndexOf('/') + 1) || '/';
  return finishPath(r, dir + input, special);
}
function finishAuthority(r, rest, special) {
  let end = rest.search(special ? /[/\\?#]/ : /[/?#]/);
  if (end < 0) end = rest.length;
  let auth = rest.slice(0, end);
  rest = rest.slice(end);
  const at = auth.lastIndexOf('@');
  if (at >= 0) {
    const cred = auth.slice(0, at); auth = auth.slice(at + 1);
    const colon = cred.indexOf(':');
    r.username = pctEncode(colon < 0 ? cred : cred.slice(0, colon), PATH_ESC + '/:;=@[\\]^|');
    r.password = colon < 0 ? '' : pctEncode(cred.slice(colon + 1), PATH_ESC + '/:;=@[\\]^|');
  }
  let port = '';
  const pm = /^(.*?):(\d*)$/.exec(auth);
  if (pm && !auth.endsWith(']')) { auth = pm[1]; port = pm[2]; }
  if (special && auth === '') { if (r.protocol !== 'file:') throw new TypeError("Failed to construct 'URL': Invalid URL"); }
  r.hostname = special ? decodeHost(auth).toLowerCase() : auth;
  if (/[\s<>^|%]/.test(r.hostname) && special) throw new TypeError("Failed to construct 'URL': Invalid URL");
  if (port !== '') { const p = String(parseInt(port, 10)); if (parseInt(port, 10) > 65535) throw new TypeError("Failed to construct 'URL': Invalid URL"); r.port = specialPorts[r.protocol] === p ? '' : p; }
  return finishPath(r, rest, special);
}
function decodeHost(h) { try { return decodeURIComponent(h); } catch (e) { return h; } }
function finishPath(r, rest, special) {
  let h = rest.indexOf('#');
  if (h >= 0) { r.hash = rest.length > h + 1 ? '#' + pctEncode(rest.slice(h + 1), FRAG_ESC) : ''; rest = rest.slice(0, h); }
  let q = rest.indexOf('?');
  if (q >= 0) { r.search = rest.length > q + 1 ? '?' + pctEncode(rest.slice(q + 1), QUERY_ESC) : ''; rest = rest.slice(0, q); }
  if (rest === '' && special) rest = '/';
  r.pathname = special || rest.startsWith('/') ? normalizePath(pctEncode(rest, PATH_ESC.replace('?', '').replace('#', '') + (special ? '' : '')).replace(/%5C/gi, special ? '/' : '%5C'), special) : rest;
  if (special && r.pathname[0] !== '/') r.pathname = '/' + r.pathname;
  return r;
}
const blobStore = new Map();
class URL {
  constructor(url, base) {
    let b = null;
    if (base !== undefined) b = base instanceof URL ? base._r : parseURL(String(base), null);
    this._r = parseURL(String(url), b);
    this._sp = null;
  }
  get href() {
    const r = this._r;
    if (r.opaque) return r.protocol + r.pathname + r.search + r.hash;
    const special = r.protocol in specialPorts;
    let out = r.protocol;
    if (special || r.hostname !== '' || r.protocol === 'file:') {
      out += '//';
      if (r.username || r.password) out += r.username + (r.password ? ':' + r.password : '') + '@';
      out += r.hostname + (r.port ? ':' + r.port : '');
    }
    return out + r.pathname + r.search + r.hash;
  }
  set href(v) { this._r = parseURL(String(v), null); this._syncParams(); }
  get origin() {
    const r = this._r;
    if (r.protocol === 'blob:') { try { return new URL(r.pathname).origin; } catch (e) { return 'null'; } }
    if (!(r.protocol in specialPorts) || r.protocol === 'file:') return 'null';
    return r.protocol + '//' + r.hostname + (r.port ? ':' + r.port : '');
  }
  get protocol() { return this._r.protocol; }
  set protocol(v) { v = String(v).replace(/:.*$/, '').toLowerCase() + ':'; if ((v in specialPorts) === (this._r.protocol in specialPorts)) this._r.protocol = v; }
  get username() { return this._r.username; } set username(v) { this._r.username = pctEncode(v, PATH_ESC + '/:;=@[\\]^|'); }
  get password() { return this._r.password; } set password(v) { this._r.password = pctEncode(v, PATH_ESC + '/:;=@[\\]^|'); }
  get host() { return this._r.hostname + (this._r.port ? ':' + this._r.port : ''); }
  set host(v) { const m = /^([^:/?#]*)(?::(\d*))?/.exec(String(v)); if (m) { this._r.hostname = m[1].toLowerCase(); if (m[2] !== undefined) this.port = m[2]; } }
  get hostname() { return this._r.hostname; } set hostname(v) { this._r.hostname = String(v).toLowerCase(); }
  get port() { return this._r.port; }
  set port(v) { v = String(v); if (v === '') { this._r.port = ''; return; } const p = parseInt(v, 10); if (!isNaN(p) && p >= 0 && p < 65536) this._r.port = specialPorts[this._r.protocol] === String(p) ? '' : String(p); }
  get pathname() { return this._r.pathname; }
  set pathname(v) { if (this._r.opaque) return; v = String(v); const special = this._r.protocol in specialPorts; this._r.pathname = normalizePath(pctEncode(v[0] === '/' ? v : '/' + v, PATH_ESC), special); }
  get search() { return this._r.search; }
  set search(v) { v = String(v); if (v[0] === '?') v = v.slice(1); this._r.search = v === '' ? '' : '?' + pctEncode(v, QUERY_ESC); this._syncParams(); }
  get hash() { return this._r.hash; }
  set hash(v) { v = String(v); if (v[0] === '#') v = v.slice(1); this._r.hash = v === '' ? '' : '#' + pctEncode(v, FRAG_ESC); }
  get searchParams() {
    if (!this._sp) { this._sp = new URLSearchParams(this._r.search); this._sp._url = this; }
    return this._sp;
  }
  _syncParams() { if (this._sp) this._sp._load(this._r.search); }
  toString() { return this.href; }
  toJSON() { return this.href; }
  static canParse(u, b) { try { new URL(u, b); return true; } catch (e) { return false; } }
  static parse(u, b) { try { return new URL(u, b); } catch (e) { return null; } }
  static createObjectURL(blob) { const id = 'blob:' + (K.doc ? new URL(K.doc.URL).origin : 'null') + '/' + K.uuid(); blobStore.set(id, blob); return id; }
  static revokeObjectURL(u) { blobStore.delete(u); }
}
K.blobStore = blobStore;
publish('URL', URL);
const formEncode = (s) => encodeURIComponent(s).replace(/%20/g, '+').replace(/[!'()~]/g, c => '%' + c.charCodeAt(0).toString(16).toUpperCase());
const formDecode = (s) => { s = s.replace(/\+/g, ' '); try { return decodeURIComponent(s); } catch (e) { return s.replace(/%([0-9a-fA-F]{2})/g, (m, h) => String.fromCharCode(parseInt(h, 16))); } };
class URLSearchParams {
  constructor(init) {
    this._l = [];
    this._url = null;
    if (init === undefined || init === null) return;
    if (typeof init === 'object' || typeof init === 'function') {
      if (typeof init[Symbol.iterator] === 'function') { for (const pair of init) { const p = [...pair]; if (p.length !== 2) throw new TypeError('Each query pair must be an iterable [name, value] tuple'); this._l.push([String(p[0]), String(p[1])]); } }
      else for (const k of Object.keys(init)) this._l.push([k, String(init[k])]);
    } else this._load(String(init));
  }
  _load(s) {
    this._l = [];
    if (s[0] === '?') s = s.slice(1);
    if (s === '') return;
    for (const part of s.split('&')) {
      if (part === '') continue;
      const e = part.indexOf('=');
      this._l.push(e < 0 ? [formDecode(part), ''] : [formDecode(part.slice(0, e)), formDecode(part.slice(e + 1))]);
    }
  }
  _update() { if (this._url) { const s = this.toString(); this._url._r.search = s ? '?' + s : ''; } }
  get size() { return this._l.length; }
  append(k, v) { this._l.push([String(k), String(v)]); this._update(); }
  delete(k, v) { k = String(k); this._l = this._l.filter(p => !(p[0] === k && (v === undefined || p[1] === String(v)))); this._update(); }
  get(k) { k = String(k); const p = this._l.find(x => x[0] === k); return p ? p[1] : null; }
  getAll(k) { k = String(k); return this._l.filter(x => x[0] === k).map(x => x[1]); }
  has(k, v) { k = String(k); return this._l.some(x => x[0] === k && (v === undefined || x[1] === String(v))); }
  set(k, v) {
    k = String(k); v = String(v);
    const i = this._l.findIndex(x => x[0] === k);
    if (i < 0) this._l.push([k, v]);
    else { this._l[i][1] = v; this._l = this._l.filter((x, j) => j <= i || x[0] !== k); }
    this._update();
  }
  sort() { this._l.sort((a, b) => (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)); this._update(); }
  forEach(fn, t) { for (const [k, v] of this._l.slice()) fn.call(t, v, k, this); }
  keys() { return this._l.map(p => p[0])[Symbol.iterator](); }
  values() { return this._l.map(p => p[1])[Symbol.iterator](); }
  entries() { return this._l.map(p => [p[0], p[1]])[Symbol.iterator](); }
  [Symbol.iterator]() { return this.entries(); }
  toString() { return this._l.map(([k, v]) => formEncode(k) + '=' + formEncode(v)).join('&'); }
}
publish('URLSearchParams', URLSearchParams);
K.parseURL = parseURL;
K.uuid = () => { const b = new Uint8Array(16); N.random(b.buffer); b[6] = (b[6] & 15) | 64; b[8] = (b[8] & 63) | 128; const h = [...b].map(x => (x < 16 ? '0' : '') + x.toString(16)); return h.slice(0, 4).join('') + '-' + h.slice(4, 6).join('') + '-' + h.slice(6, 8).join('') + '-' + h.slice(8, 10).join('') + '-' + h.slice(10).join(''); };

// ---- location and history ----
K.url = 'about:blank';
class Location {
  _u() { return new URL(K.url); }
  get href() { return K.url; }
  set href(v) { K.navigate(resolveURL(v), false, false); }
  get origin() { return this._u().origin; }
  get protocol() { return this._u().protocol; }
  set protocol(v) { const u = this._u(); u.protocol = v; K.navigate(u.href); }
  get host() { return this._u().host; }
  set host(v) { const u = this._u(); u.host = v; K.navigate(u.href); }
  get hostname() { return this._u().hostname; }
  set hostname(v) { const u = this._u(); u.hostname = v; K.navigate(u.href); }
  get port() { return this._u().port; }
  set port(v) { const u = this._u(); u.port = v; K.navigate(u.href); }
  get pathname() { return this._u().pathname; }
  set pathname(v) { const u = this._u(); u.pathname = v; K.navigate(u.href); }
  get search() { return this._u().search; }
  set search(v) { const u = this._u(); u.search = v; K.navigate(u.href); }
  get hash() { return this._u().hash; }
  set hash(v) { const u = this._u(); u.hash = v; K.navigate(u.href); }
  assign(u) { K.navigate(resolveURL(u), false, false); }
  replace(u) { K.navigate(resolveURL(u), true, false); }
  reload() { N.reload(); }
  toString() { return K.url; }
  get ancestorOrigins() { return []; }
}
function resolveURL(u) {
  try { return new URL(String(u), K.doc ? K.doc.baseURI : K.url).href; } catch (e) { throw new DOMException("Failed to parse URL '" + u + "'", 'SyntaxError'); }
}
K.resolveURL = resolveURL;
const location = new Location();
publish('Location', Location);

// Go to another page. `replace`: don't add a history entry. `newWindow`: a link
// with target=_blank (the browser opens it in this window).
K.navigate = function (url, replace, newWindow, method, body, contentType) {
  if (/^javascript:/i.test(url)) { try { (0, eval)(url.replace(/^javascript:/i, '')); } catch (e) { reportError(e); } return; }
  // only the part after # changed: stay on the page
  try {
    const a = new URL(url), b = new URL(K.url);
    if (!method && a.href.split('#')[0] === b.href.split('#')[0] && (a.hash !== '' || url.includes('#'))) {
      const old = K.url;
      if (a.href === b.href) return;
      history._push(null, a.href, replace);
      setTimeout(() => { G.dispatchEvent(new HashChangeEvent('hashchange', { oldURL: old, newURL: a.href })); }, 0);
      return;
    }
  } catch (e) { /* not a normal URL: fall through to the browser */ }
  N.navigate(url, replace ? 1 : 0, method || 'GET', body || '', contentType || '');
};

class History {
  constructor() { this._e = [{ url: 'about:blank', state: null }]; this._i = 0; this.scrollRestoration = 'auto'; }
  get length() { return this._e.length; }
  get state() { return this._e[this._i].state; }
  _push(state, url, replace) {
    if (replace) this._e[this._i] = { url, state };
    else { this._e.length = this._i + 1; this._e.push({ url, state }); this._i++; }
    K.url = url;
    N.urlChanged(url, replace ? 1 : 0);
  }
  pushState(state, title, url) { this._change(state, url, false); }
  replaceState(state, title, url) { this._change(state, url, true); }
  _change(state, url, replace) {
    let target = K.url;
    if (url !== undefined && url !== null) {
      target = resolveURL(url);
      const a = new URL(target), b = new URL(K.url);
      if (a.origin !== b.origin && !(a.protocol === b.protocol && a.host === b.host)) throw new DOMException("A history state object with URL '" + target + "' cannot be created in a document with origin '" + b.origin + "' and URL '" + K.url + "'.", 'SecurityError');
    }
    try { state = state === undefined ? null : structuredClone(state); } catch (e) { throw new DOMException('The object can not be cloned.', 'DataCloneError'); }
    this._push(state, target, replace);
  }
  go(delta) {
    delta = delta | 0;
    if (delta === 0) { N.reload(); return; }
    const to = this._i + delta;
    if (to < 0 || to >= this._e.length) { N.historyEdge(delta); return; }
    const old = K.url;
    this._i = to;
    K.url = this._e[to].url;
    N.urlChanged(K.url, 2);
    setTimeout(() => {
      G.dispatchEvent(new PopStateEvent('popstate', { state: this._e[to].state }));
      if (old.split('#')[0] === K.url.split('#')[0] && old !== K.url) G.dispatchEvent(new HashChangeEvent('hashchange', { oldURL: old, newURL: K.url }));
    }, 0);
  }
  back() { this.go(-1); }
  forward() { this.go(1); }
  // the browser moves through our entries by itself
  get canGoBack() { return this._i > 0; }
  get canGoForward() { return this._i < this._e.length - 1; }
}
const history = new History();
publish('History', History);
K.history = history;

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

// 60_web.js - web APIs that aren't the page itself: text encoding, Blob,
// FormData, fetch, XMLHttpRequest, storage, navigator, observers...

// ---- Text encoding ----
function utf8Encode(str) {
  const out = [];
  for (let i = 0; i < str.length; i++) {
    let c = str.charCodeAt(i);
    if (c >= 0xd800 && c < 0xdc00 && i + 1 < str.length) {
      const d = str.charCodeAt(i + 1);
      if (d >= 0xdc00 && d < 0xe000) { c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00); i++; }
    }
    if (c >= 0xd800 && c < 0xe000) c = 0xfffd;
    if (c < 0x80) out.push(c);
    else if (c < 0x800) out.push(0xc0 | (c >> 6), 0x80 | (c & 63));
    else if (c < 0x10000) out.push(0xe0 | (c >> 12), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
    else out.push(0xf0 | (c >> 18), 0x80 | ((c >> 12) & 63), 0x80 | ((c >> 6) & 63), 0x80 | (c & 63));
  }
  return new Uint8Array(out);
}
function utf8Decode(bytes, fatal) {
  let out = '';
  const n = bytes.length;
  let i = 0;
  const chunk = [];
  const flush = () => { out += String.fromCharCode.apply(null, chunk); chunk.length = 0; };
  while (i < n) {
    const b = bytes[i];
    let cp = 0xfffd, len = 1;
    if (b < 0x80) cp = b;
    else if (b >= 0xc2 && b < 0xe0 && i + 1 < n && (bytes[i + 1] & 0xc0) === 0x80) { cp = ((b & 31) << 6) | (bytes[i + 1] & 63); len = 2; }
    else if (b >= 0xe0 && b < 0xf0 && i + 2 < n && (bytes[i + 1] & 0xc0) === 0x80 && (bytes[i + 2] & 0xc0) === 0x80) { cp = ((b & 15) << 12) | ((bytes[i + 1] & 63) << 6) | (bytes[i + 2] & 63); len = 3; if (cp < 0x800 || (cp >= 0xd800 && cp < 0xe000)) { cp = 0xfffd; len = 1; } }
    else if (b >= 0xf0 && b < 0xf5 && i + 3 < n && (bytes[i + 1] & 0xc0) === 0x80 && (bytes[i + 2] & 0xc0) === 0x80 && (bytes[i + 3] & 0xc0) === 0x80) { cp = ((b & 7) << 18) | ((bytes[i + 1] & 63) << 12) | ((bytes[i + 2] & 63) << 6) | (bytes[i + 3] & 63); len = 4; if (cp < 0x10000 || cp > 0x10ffff) { cp = 0xfffd; len = 1; } }
    else if (fatal) throw new TypeError('The encoded data was not valid for encoding utf-8');
    if (cp >= 0x10000) { cp -= 0x10000; chunk.push(0xd800 + (cp >> 10), 0xdc00 + (cp & 1023)); } else chunk.push(cp);
    i += len;
    if (chunk.length > 4000) flush();
  }
  flush();
  return out;
}
K.utf8Encode = utf8Encode;
K.utf8Decode = utf8Decode;
const toBytes = (b) => b instanceof ArrayBuffer ? new Uint8Array(b) : ArrayBuffer.isView(b) ? new Uint8Array(b.buffer, b.byteOffset, b.byteLength) : new Uint8Array(0);
publish('TextEncoder', class TextEncoder {
  get encoding() { return 'utf-8'; }
  encode(s = '') { return utf8Encode(String(s)); }
  encodeInto(s, dest) { const b = utf8Encode(String(s)); const n = Math.min(b.length, dest.length); dest.set(b.subarray(0, n)); return { read: s.length, written: n }; }
});
publish('TextDecoder', class TextDecoder {
  constructor(label = 'utf-8', opts) {
    label = String(label).toLowerCase().trim();
    this.encoding = /^(utf-?8|unicode-1-1-utf-8)$/.test(label) ? 'utf-8' : /^utf-?16(le)?$/.test(label) ? 'utf-16le' : /^(latin1|iso-8859-1|windows-1252|ascii|us-ascii)$/.test(label) ? 'windows-1252' : null;
    if (!this.encoding) throw new RangeError("The encoding label provided ('" + label + "') is invalid.");
    this.fatal = !!(opts && opts.fatal); this.ignoreBOM = !!(opts && opts.ignoreBOM);
    this._pending = null;
  }
  decode(buf, opts) {
    let b = buf === undefined ? new Uint8Array(0) : toBytes(buf);
    if (this._pending) { const m = new Uint8Array(this._pending.length + b.length); m.set(this._pending); m.set(b, this._pending.length); b = m; this._pending = null; }
    if (opts && opts.stream) {
      // hold back an incomplete character at the end
      let k = b.length, back = 0;
      while (k > 0 && back < 3 && (b[k - 1] & 0xc0) === 0x80) { k--; back++; }
      if (k > 0 && b[k - 1] >= 0xc0) { const need = b[k - 1] >= 0xf0 ? 4 : b[k - 1] >= 0xe0 ? 3 : 2; if (b.length - (k - 1) < need) { this._pending = b.slice(k - 1); b = b.subarray(0, k - 1); } }
    }
    if (this.encoding === 'utf-8') { if (!this.ignoreBOM && b[0] === 0xef && b[1] === 0xbb && b[2] === 0xbf) b = b.subarray(3); return utf8Decode(b, this.fatal); }
    if (this.encoding === 'utf-16le') { let s = ''; for (let i = 0; i + 1 < b.length; i += 2) s += String.fromCharCode(b[i] | (b[i + 1] << 8)); return s; }
    let s = ''; for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i]); return s;
  }
});
const b64chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function b64encode(bytes) {
  let out = '';
  for (let i = 0; i < bytes.length; i += 3) {
    const a = bytes[i], b = bytes[i + 1], c = bytes[i + 2];
    out += b64chars[a >> 2] + b64chars[((a & 3) << 4) | ((b || 0) >> 4)] + (i + 1 < bytes.length ? b64chars[((b & 15) << 2) | ((c || 0) >> 6)] : '=') + (i + 2 < bytes.length ? b64chars[c & 63] : '=');
  }
  return out;
}
function b64decode(s) {
  s = String(s).replace(/[\t\n\f\r ]/g, '').replace(/=+$/, '').replace(/-/g, '+').replace(/_/g, '/');
  if (s.length % 4 === 1 || /[^A-Za-z0-9+/]/.test(s)) throw new DOMException('The string to be decoded is not correctly encoded.', 'InvalidCharacterError');
  const out = [];
  let bits = 0, acc = 0;
  for (const ch of s) { acc = (acc << 6) | b64chars.indexOf(ch); bits += 6; if (bits >= 8) { bits -= 8; out.push((acc >> bits) & 255); } }
  return new Uint8Array(out);
}
K.b64encode = b64encode; K.b64decode = b64decode;
publish('btoa', (s) => { s = String(s); const b = new Uint8Array(s.length); for (let i = 0; i < s.length; i++) { const c = s.charCodeAt(i); if (c > 255) throw new DOMException('The string to be encoded contains characters outside of the Latin1 range.', 'InvalidCharacterError'); b[i] = c; } return b64encode(b); });
publish('atob', (s) => { const b = b64decode(s); let out = ''; for (let i = 0; i < b.length; i++) out += String.fromCharCode(b[i]); return out; });

// ---- structuredClone ----
function clone(v, seen) {
  if (typeof v === 'function' || typeof v === 'symbol') throw new DOMException('Cannot clone ' + typeof v, 'DataCloneError');
  if (v === null || typeof v !== 'object') return v;
  if (seen.has(v)) return seen.get(v);
  let out;
  if (v instanceof Date) out = new Date(v.getTime());
  else if (v instanceof RegExp) out = new RegExp(v.source, v.flags);
  else if (v instanceof Map) { out = new Map(); seen.set(v, out); for (const [a, b] of v) out.set(clone(a, seen), clone(b, seen)); return out; }
  else if (v instanceof Set) { out = new Set(); seen.set(v, out); for (const a of v) out.add(clone(a, seen)); return out; }
  else if (v instanceof ArrayBuffer) out = v.slice(0);
  else if (ArrayBuffer.isView(v)) out = new v.constructor(v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength));
  else if (v instanceof Error) { out = new (v.constructor || Error)(v.message); out.stack = v.stack; }
  else if (v instanceof Node || v instanceof Event) throw new DOMException('Cannot clone a DOM node', 'DataCloneError');
  else if (Array.isArray(v)) { out = new Array(v.length); seen.set(v, out); for (let i = 0; i < v.length; i++) if (i in v) out[i] = clone(v[i], seen); return out; }
  else { out = {}; seen.set(v, out); for (const k of Object.keys(v)) out[k] = clone(v[k], seen); return out; }
  seen.set(v, out);
  return out;
}
publish('structuredClone', (v) => clone(v, new Map()));

// ---- Blob, File, FormData ----
class Blob {
  constructor(parts = [], opts = {}) {
    const chunks = [];
    for (const p of parts) chunks.push(p instanceof Blob ? p._b : typeof p === 'string' ? utf8Encode(p) : toBytes(p));
    let n = 0; for (const c of chunks) n += c.length;
    const all = new Uint8Array(n); let o = 0;
    for (const c of chunks) { all.set(c, o); o += c.length; }
    this._b = all;
    this.type = opts.type ? String(opts.type).toLowerCase() : '';
  }
  get size() { return this._b.length; }
  slice(s = 0, e = this._b.length, type = '') { const b = new Blob([], { type }); b._b = this._b.slice(s, e); return b; }
  text() { return Promise.resolve(utf8Decode(this._b)); }
  arrayBuffer() { return Promise.resolve(this._b.slice().buffer); }
  bytes() { return Promise.resolve(this._b.slice()); }
  stream() { return new ReadableStream({ start: (c) => { c.enqueue(this._b.slice()); c.close(); } }); }
}
class File extends Blob {
  constructor(parts, name, opts = {}) { super(parts, opts); this.name = String(name); this.lastModified = opts.lastModified || Date.now(); }
}
publish('Blob', Blob); publish('File', File);
publish('FileList', function FileList() {});
class FileReader extends EventTarget {
  constructor() { super(); this.readyState = 0; this.result = null; this.error = null; }
  _read(blob, how) {
    this.readyState = 1;
    setTimeout(() => {
      try {
        const b = blob._b;
        if (how === 'text') this.result = utf8Decode(b);
        else if (how === 'dataurl') this.result = 'data:' + (blob.type || 'application/octet-stream') + ';base64,' + b64encode(b);
        else if (how === 'binary') { let s = ''; for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i]); this.result = s; }
        else this.result = b.slice().buffer;
        this.readyState = 2;
        this._fire('load'); this._fire('loadend');
      } catch (e) { this.error = e; this._fire('error'); }
    }, 0);
  }
  _fire(t) { const ev = new ProgressEvent(t); this.dispatchEvent(ev); const h = this['on' + t]; if (typeof h === 'function') h.call(this, ev); }
  readAsText(b) { this._read(b, 'text'); } readAsDataURL(b) { this._read(b, 'dataurl'); } readAsArrayBuffer(b) { this._read(b, 'buffer'); } readAsBinaryString(b) { this._read(b, 'binary'); }
  abort() {}
}
publish('FileReader', FileReader);
class FormData {
  constructor(form) {
    this._l = [];
    if (form && form.localName === 'form') for (const [k, v] of formEntries(form, null)) this._l.push([k, v]);
  }
  append(k, v, name) { this._l.push([String(k), v instanceof Blob ? (v instanceof File && name === undefined ? v : new File([v], name === undefined ? 'blob' : name, { type: v.type })) : String(v)]); }
  delete(k) { this._l = this._l.filter(p => p[0] !== k); }
  get(k) { const p = this._l.find(x => x[0] === k); return p ? p[1] : null; }
  getAll(k) { return this._l.filter(x => x[0] === k).map(x => x[1]); }
  has(k) { return this._l.some(x => x[0] === k); }
  set(k, v, name) { const i = this._l.findIndex(x => x[0] === k); const val = v instanceof Blob ? v : String(v); if (i < 0) this.append(k, v, name); else { this._l[i][1] = val; this._l = this._l.filter((x, j) => j <= i || x[0] !== k); } }
  forEach(fn, t) { for (const [k, v] of this._l) fn.call(t, v, k, this); }
  keys() { return this._l.map(p => p[0])[Symbol.iterator](); }
  values() { return this._l.map(p => p[1])[Symbol.iterator](); }
  entries() { return this._l.map(p => [p[0], p[1]])[Symbol.iterator](); }
  [Symbol.iterator]() { return this.entries(); }
}
publish('FormData', FormData);
function encodeMultipart(fd) {
  const boundary = '----AREOSFormBoundary' + K.uuid().replace(/-/g, '');
  const parts = [];
  for (const [k, v] of fd._l) {
    if (v instanceof Blob) { parts.push(utf8Encode('--' + boundary + '\r\nContent-Disposition: form-data; name="' + k + '"; filename="' + (v.name || 'blob') + '"\r\nContent-Type: ' + (v.type || 'application/octet-stream') + '\r\n\r\n'), v._b, utf8Encode('\r\n')); }
    else parts.push(utf8Encode('--' + boundary + '\r\nContent-Disposition: form-data; name="' + k + '"\r\n\r\n' + v + '\r\n'));
  }
  parts.push(utf8Encode('--' + boundary + '--\r\n'));
  return { body: new Blob(parts)._b, type: 'multipart/form-data; boundary=' + boundary };
}

// ---- Streams (just enough for Response.body) ----
class ReadableStreamDefaultReader {
  constructor(s) { this._s = s; this.closed = new Promise((res) => { this._closedRes = res; }); }
  read() {
    const s = this._s;
    if (s._q.length) return Promise.resolve({ value: s._q.shift(), done: false });
    if (s._closed) return Promise.resolve({ value: undefined, done: true });
    if (s._err) return Promise.reject(s._err);
    return new Promise((res) => { s._waiters.push(res); });
  }
  releaseLock() { this._s._locked = false; }
  cancel() { this._s._closed = true; return Promise.resolve(); }
}
class ReadableStream {
  constructor(src = {}) {
    this._q = []; this._closed = false; this._err = null; this._waiters = []; this._locked = false;
    const self = this;
    const controller = {
      enqueue(c) { if (self._waiters.length) self._waiters.shift()({ value: c, done: false }); else self._q.push(c); },
      close() { self._closed = true; while (self._waiters.length) self._waiters.shift()({ value: undefined, done: true }); },
      error(e) { self._err = e; },
      get desiredSize() { return 1; },
    };
    if (src.start) src.start(controller);
  }
  get locked() { return this._locked; }
  getReader() { this._locked = true; return new ReadableStreamDefaultReader(this); }
  cancel() { this._closed = true; return Promise.resolve(); }
  tee() { return [this, this]; }
  async *[Symbol.asyncIterator]() { const r = this.getReader(); for (;;) { const { value, done } = await r.read(); if (done) return; yield value; } }
}
publish('ReadableStream', ReadableStream);
publish('ReadableStreamDefaultReader', ReadableStreamDefaultReader);
publish('WritableStream', class WritableStream { constructor(s = {}) { this._s = s; } getWriter() { const s = this._s; return { write: (c) => Promise.resolve(s.write && s.write(c)), close: () => Promise.resolve(s.close && s.close()), abort() { return Promise.resolve(); }, releaseLock() {}, ready: Promise.resolve(), closed: Promise.resolve() }; } });
publish('TransformStream', class TransformStream { constructor() { this.readable = new ReadableStream(); this.writable = {}; } });

// ---- AbortController ----
class AbortSignal extends EventTarget {
  constructor() { super(); this.aborted = false; this.reason = undefined; }
  throwIfAborted() { if (this.aborted) throw this.reason; }
  _abort(reason) {
    if (this.aborted) return;
    this.aborted = true;
    this.reason = reason === undefined ? new DOMException('signal is aborted without reason', 'AbortError') : reason;
    const ev = new Event('abort');
    this.dispatchEvent(ev);
    if (typeof this.onabort === 'function') { try { this.onabort(ev); } catch (e) { reportError(e); } }
  }
  static abort(reason) { const s = new AbortSignal(); s._abort(reason); return s; }
  static timeout(ms) { const s = new AbortSignal(); setTimeout(() => s._abort(new DOMException('signal timed out', 'TimeoutError')), ms); return s; }
  static any(signals) { const s = new AbortSignal(); for (const x of signals) { if (x.aborted) { s._abort(x.reason); break; } x.addEventListener('abort', () => s._abort(x.reason)); } return s; }
}
AbortSignal.prototype.onabort = null;
publish('AbortSignal', AbortSignal);
publish('AbortController', class AbortController { constructor() { this.signal = new AbortSignal(); } abort(reason) { this.signal._abort(reason); } });

// ---- Headers, Request, Response ----
class Headers {
  constructor(init) {
    this._m = new Map();
    if (init instanceof Headers) for (const [k, v] of init) this.append(k, v);
    else if (Array.isArray(init)) for (const [k, v] of init) this.append(k, v);
    else if (init && typeof init === 'object') for (const k of Object.keys(init)) this.append(k, init[k]);
  }
  append(k, v) { k = String(k).toLowerCase(); v = String(v).trim(); const o = this._m.get(k); this._m.set(k, o === undefined ? v : o + ', ' + v); }
  set(k, v) { this._m.set(String(k).toLowerCase(), String(v).trim()); }
  get(k) { const v = this._m.get(String(k).toLowerCase()); return v === undefined ? null : v; }
  has(k) { return this._m.has(String(k).toLowerCase()); }
  delete(k) { this._m.delete(String(k).toLowerCase()); }
  getSetCookie() { return []; }
  forEach(fn, t) { for (const [k, v] of [...this._m].sort()) fn.call(t, v, k, this); }
  keys() { return [...this._m.keys()].sort()[Symbol.iterator](); }
  values() { return [...this._m].sort().map(p => p[1])[Symbol.iterator](); }
  entries() { return [...this._m].sort()[Symbol.iterator](); }
  [Symbol.iterator]() { return this.entries(); }
}
publish('Headers', Headers);
function bodyInit(body, headers) {
  if (body === null || body === undefined) return null;
  if (typeof body === 'string') { if (!headers.has('content-type')) headers.set('content-type', 'text/plain;charset=UTF-8'); return utf8Encode(body); }
  if (body instanceof URLSearchParams) { if (!headers.has('content-type')) headers.set('content-type', 'application/x-www-form-urlencoded;charset=UTF-8'); return utf8Encode(body.toString()); }
  if (body instanceof FormData) { const m = encodeMultipart(body); headers.set('content-type', m.type); return m.body; }
  if (body instanceof Blob) { if (body.type && !headers.has('content-type')) headers.set('content-type', body.type); return body._b; }
  if (body instanceof ArrayBuffer || ArrayBuffer.isView(body)) return toBytes(body).slice();
  return utf8Encode(String(body));
}
class Body {
  _initBody(body, headers) { this._bytes = bodyInit(body, headers); this.bodyUsed = false; }
  _take() { if (this.bodyUsed) return Promise.reject(new TypeError('Body has already been consumed.')); this.bodyUsed = true; return Promise.resolve(this._bytes || new Uint8Array(0)); }
  get body() { if (!this._bytes) return null; const b = this._bytes; return new ReadableStream({ start: (c) => { c.enqueue(b.slice()); c.close(); } }); }
  text() { return this._take().then(b => utf8Decode(b)); }
  json() { return this.text().then(t => JSON.parse(t)); }
  arrayBuffer() { return this._take().then(b => b.slice().buffer); }
  bytes() { return this._take().then(b => b.slice()); }
  blob() { return this._take().then(b => { const bl = new Blob([b], { type: this.headers.get('content-type') || '' }); return bl; }); }
  formData() { return this.text().then(t => { const fd = new FormData(); for (const [k, v] of new URLSearchParams(t)) fd.append(k, v); return fd; }); }
}
class Request extends Body {
  constructor(input, init = {}) {
    super();
    if (input instanceof Request) { init = Object.assign({ method: input.method, headers: input.headers, signal: input.signal, credentials: input.credentials, mode: input.mode, redirect: input.redirect, cache: input.cache, referrer: input.referrer }, input._bytes ? { body: input._bytes } : {}, init); input = input.url; }
    this.url = resolveURL(String(input));
    this.method = String(init.method || 'GET').toUpperCase();
    this.headers = new Headers(init.headers);
    this.signal = init.signal || new AbortSignal();
    this.credentials = init.credentials || 'same-origin'; this.mode = init.mode || 'cors'; this.redirect = init.redirect || 'follow'; this.cache = init.cache || 'default';
    this.referrer = init.referrer || 'about:client'; this.referrerPolicy = init.referrerPolicy || ''; this.integrity = init.integrity || ''; this.keepalive = !!init.keepalive; this.destination = '';
    if (init.body != null && (this.method === 'GET' || this.method === 'HEAD')) throw new TypeError('Request with GET/HEAD method cannot have body.');
    this._initBody(init.body, this.headers);
  }
  clone() { const r = new Request(this.url, { method: this.method, headers: this.headers, signal: this.signal }); r._bytes = this._bytes; return r; }
}
class Response extends Body {
  constructor(body = null, init = {}) {
    super();
    this.status = init.status === undefined ? 200 : init.status;
    if (this.status < 200 || this.status > 599) throw new RangeError('The status provided (' + this.status + ') is outside the range [200, 599].');
    this.statusText = init.statusText === undefined ? '' : String(init.statusText);
    this.headers = new Headers(init.headers);
    this.ok = this.status >= 200 && this.status < 300;
    this.url = ''; this.redirected = false; this.type = 'default';
    this._initBody(body, this.headers);
  }
  clone() { const r = new Response(null, { status: this.status, statusText: this.statusText, headers: this.headers }); r._bytes = this._bytes; r.url = this.url; r.redirected = this.redirected; r.type = this.type; return r; }
  static error() { const r = new Response(null, { status: 200 }); r.status = 0; r.ok = false; r.type = 'error'; return r; }
  static json(data, init = {}) { const h = new Headers(init.headers); if (!h.has('content-type')) h.set('content-type', 'application/json'); return new Response(JSON.stringify(data), Object.assign({}, init, { headers: h })); }
  static redirect(url, status = 302) { return new Response(null, { status, headers: { location: String(url) } }); }
}
publish('Request', Request); publish('Response', Response);

// Ask the browser for a page. Returns {status, statusText, url, headers, body} or throws.
function netFetch(method, url, headers, body, timeout) {
  const list = [];
  for (const [k, v] of headers) list.push(k, v);
  return N.http(method, url, list, body ? body.buffer.slice(body.byteOffset, body.byteOffset + body.byteLength) : null, timeout || 30000);
}
K.netFetch = netFetch;
function dataURL(url) {
  const m = /^data:([^,]*?)(;base64)?,(.*)$/s.exec(url);
  if (!m) throw new TypeError('Invalid data URL');
  const bytes = m[2] ? b64decode(decodeURIComponent(m[3])) : utf8Encode(decodeURIComponent(m[3]));
  return { type: m[1] || 'text/plain;charset=US-ASCII', bytes };
}
publish('fetch', (input, init) => new Promise((resolve, reject) => {
  let req;
  try { req = new Request(input, init); } catch (e) { reject(e); return; }
  if (req.signal.aborted) { reject(req.signal.reason); return; }
  let aborted = false;
  req.signal.addEventListener('abort', () => { aborted = true; reject(req.signal.reason); });
  setTimeout(() => {
    if (aborted) return;
    try {
      const url = req.url;
      let res;
      if (url.startsWith('data:')) {
        const d = dataURL(url);
        res = new Response(d.bytes, { status: 200, headers: { 'content-type': d.type } });
      } else if (url.startsWith('blob:')) {
        const b = blobStore.get(url);
        if (!b) throw new TypeError('Failed to fetch');
        res = new Response(b._b, { status: 200, headers: { 'content-type': b.type } });
      } else if (/^https?:/.test(url)) {
        const h = new Headers(req.headers);
        const r = netFetch(req.method, url, h, req._bytes, 30000);
        res = new Response(r.method === 'HEAD' || r.status === 204 || r.status === 304 ? null : new Uint8Array(r.body), { status: r.status, statusText: r.statusText });
        for (let i = 0; i < r.headers.length; i += 2) res.headers.append(r.headers[i], r.headers[i + 1]);
        res.url = r.url; res.redirected = r.url !== url; res.type = 'basic';
      } else throw new TypeError('Failed to fetch');
      if (!aborted) resolve(res);
    } catch (e) {
      logLine(2, 'fetch failed: ' + req.url + ' ' + (e && e.message ? e.message : e));
      if (!aborted) reject(e instanceof TypeError ? e : new TypeError('Failed to fetch'));
    }
  }, 0);
}));

// ---- XMLHttpRequest ----
class XMLHttpRequestEventTarget extends EventTarget {}
class XMLHttpRequest extends XMLHttpRequestEventTarget {
  constructor() {
    super();
    this.readyState = 0; this.status = 0; this.statusText = ''; this.responseType = ''; this.timeout = 0; this.withCredentials = false;
    this.responseURL = ''; this.upload = new EventTarget();
    this._resp = null; this._headers = new Headers(); this._method = 'GET'; this._url = ''; this._async = true; this._aborted = false; this._sent = false;
    for (const t of ['readystatechange', 'load', 'error', 'abort', 'timeout', 'loadstart', 'loadend', 'progress']) this['on' + t] = null;
  }
  open(method, url, async = true) {
    this._method = String(method).toUpperCase(); this._url = resolveURL(url); this._async = async !== false;
    this._headers = new Headers(); this._resp = null; this._aborted = false; this._sent = false;
    this._state(1);
  }
  setRequestHeader(k, v) { if (this.readyState !== 1) throw new DOMException('The object\'s state must be OPENED.', 'InvalidStateError'); this._headers.append(k, v); }
  overrideMimeType() {}
  _fire(t, extra) { const ev = new ProgressEvent(t, extra); this.dispatchEvent(ev); const h = this['on' + t]; if (typeof h === 'function') { try { h.call(this, ev); } catch (e) { reportError(e); } } }
  _state(s) { this.readyState = s; this._fire('readystatechange'); }
  send(body = null) {
    if (this.readyState !== 1) throw new DOMException('The object\'s state must be OPENED.', 'InvalidStateError');
    this._sent = true;
    const h = this._headers;
    let bytes = null;
    if (this._method !== 'GET' && this._method !== 'HEAD' && body != null) bytes = bodyInit(body instanceof Document ? serializeNode(body) : body, h);
    const run = () => {
      if (this._aborted) return;
      this._fire('loadstart');
      try {
        const url = this._url;
        let status, statusText = '', hdrs = [], data, finalUrl = url;
        if (url.startsWith('data:')) { const d = dataURL(url); status = 200; hdrs = ['content-type', d.type]; data = d.bytes; }
        else if (url.startsWith('blob:')) { const b = blobStore.get(url); if (!b) throw new Error('blob gone'); status = 200; hdrs = ['content-type', b.type]; data = b._b; }
        else { const r = netFetch(this._method, url, h, bytes, this.timeout || 30000); status = r.status; statusText = r.statusText; hdrs = r.headers; data = new Uint8Array(r.body); finalUrl = r.url; }
        this.status = status; this.statusText = statusText; this.responseURL = finalUrl;
        this._resp = new Headers();
        for (let i = 0; i < hdrs.length; i += 2) this._resp.append(hdrs[i], hdrs[i + 1]);
        this._data = data;
        this._state(2); this._state(3);
        this._state(4);
        this._fire('progress', { loaded: data.length, total: data.length, lengthComputable: true });
        this._fire('load', { loaded: data.length, total: data.length });
        this._fire('loadend');
      } catch (e) {
        logLine(2, 'XMLHttpRequest failed: ' + this._url + ' ' + (e && e.message ? e.message : e));
        this.status = 0; this._data = new Uint8Array(0);
        this._state(4);
        this._fire('error'); this._fire('loadend');
      }
    };
    if (this._async) setTimeout(run, 0); else run();
  }
  abort() { this._aborted = true; if (this._sent && this.readyState !== 4) { this._state(4); this._fire('abort'); this._fire('loadend'); } this.readyState = 0; }
  getResponseHeader(n) { return this._resp ? this._resp.get(n) : null; }
  getAllResponseHeaders() { if (!this._resp) return ''; let s = ''; for (const [k, v] of this._resp) s += k + ': ' + v + '\r\n'; return s; }
  get responseText() { return this.readyState < 3 || !this._data ? '' : (this.responseType === '' || this.responseType === 'text' ? utf8Decode(this._data) : (() => { throw new DOMException("Failed to read the 'responseText' property from 'XMLHttpRequest': The value is only accessible if the object's 'responseType' is '' or 'text'.", 'InvalidStateError'); })()); }
  get response() {
    if (this.readyState !== 4 || !this._data) return this.responseType === '' || this.responseType === 'text' ? '' : null;
    switch (this.responseType) {
      case 'arraybuffer': return this._data.slice().buffer;
      case 'blob': return new Blob([this._data], { type: this.getResponseHeader('content-type') || '' });
      case 'json': try { return JSON.parse(utf8Decode(this._data)); } catch (e) { return null; }
      case 'document': { const d = new DOMParser().parseFromString(utf8Decode(this._data), 'text/html'); return d; }
      default: return utf8Decode(this._data);
    }
  }
  get responseXML() { return null; }
}
XMLHttpRequest.UNSENT = 0; XMLHttpRequest.OPENED = 1; XMLHttpRequest.HEADERS_RECEIVED = 2; XMLHttpRequest.LOADING = 3; XMLHttpRequest.DONE = 4;
Object.assign(XMLHttpRequest.prototype, { UNSENT: 0, OPENED: 1, HEADERS_RECEIVED: 2, LOADING: 3, DONE: 4 });
publish('XMLHttpRequest', XMLHttpRequest);
publish('XMLHttpRequestUpload', EventTarget);

// ---- Storage (kept while AREOS is on, so pages in the same session share it) ----
function storageScope(kind) { let o = 'null'; try { o = new URL(K.url).origin; } catch (e) { /* ignore */ } return kind + ':' + o; }
class Storage {
  constructor(kind) { Object.defineProperty(this, '_k', { value: kind }); }
  get length() { return N.storeKeys(storageScope(this._k)).length; }
  key(i) { const k = N.storeKeys(storageScope(this._k)); return i < k.length ? k[i] : null; }
  getItem(k) { const v = N.storeGet(storageScope(this._k), String(k)); return v === undefined ? null : v; }
  setItem(k, v) { N.storeSet(storageScope(this._k), String(k), String(v)); }
  removeItem(k) { N.storeSet(storageScope(this._k), String(k), undefined); }
  clear() { for (const k of N.storeKeys(storageScope(this._k))) N.storeSet(storageScope(this._k), k, undefined); }
}
function storageProxy(kind) {
  const s = new Storage(kind);
  return new Proxy(s, {
    get(t, p) { if (typeof p === 'symbol' || p in Storage.prototype || p === '_k') return typeof t[p] === 'function' ? t[p].bind(t) : t[p]; const v = t.getItem(p); return v === null ? undefined : v; },
    set(t, p, v) { t.setItem(p, v); return true; },
    has(t, p) { return p in Storage.prototype || t.getItem(p) !== null; },
    deleteProperty(t, p) { t.removeItem(p); return true; },
    ownKeys(t) { return N.storeKeys(storageScope(t._k)); },
    getOwnPropertyDescriptor(t, p) { const v = typeof p === 'string' ? t.getItem(p) : null; return v === null ? undefined : { value: v, enumerable: true, configurable: true, writable: true }; },
  });
}
publish('Storage', Storage);
publish('localStorage', storageProxy('l'));
publish('sessionStorage', storageProxy('s'));

// ---- navigator, screen, performance, crypto ----
const nav = {
  userAgent: 'Mozilla/5.0 (X11; AREOS x86_64) AppleWebKit/537.36 (KHTML, like Gecko) AREOS-Browser/3.0',
  appName: 'Netscape', appVersion: '5.0 (X11; AREOS x86_64)', appCodeName: 'Mozilla', product: 'Gecko', productSub: '20030107', vendor: 'AREOS', vendorSub: '',
  platform: 'AREOS x86_64', language: 'en-US', languages: ['en-US', 'en'], onLine: true, cookieEnabled: false, doNotTrack: '1', webdriver: false,
  hardwareConcurrency: 1, maxTouchPoints: 0, deviceMemory: 2, pdfViewerEnabled: false, plugins: [], mimeTypes: [],
  javaEnabled: () => false, sendBeacon: () => true, vibrate: () => false,
  clipboard: { writeText: () => Promise.resolve(), readText: () => Promise.resolve(''), write: () => Promise.resolve(), read: () => Promise.resolve([]) },
  permissions: { query: () => Promise.resolve(Object.assign(new EventTarget(), { state: 'denied', onchange: null })) },
  geolocation: { getCurrentPosition(ok, err) { if (err) setTimeout(() => err({ code: 1, message: 'Location is not available in AREOS.' }), 0); }, watchPosition(ok, err) { if (err) setTimeout(() => err({ code: 1, message: 'Location is not available in AREOS.' }), 0); return 0; }, clearWatch() {} },
  storage: { estimate: () => Promise.resolve({ quota: 0, usage: 0 }), persist: () => Promise.resolve(false), persisted: () => Promise.resolve(false) },
  locks: undefined,
};
Object.setPrototypeOf(nav, { constructor: function Navigator() {} });
publish('navigator', nav);
publish('Navigator', nav.constructor);
publish('screen', Object.assign(new EventTarget(), { width: N.width(), height: N.height(), availWidth: N.width(), availHeight: N.height(), availLeft: 0, availTop: 0, colorDepth: 24, pixelDepth: 24, orientation: Object.assign(new EventTarget(), { type: 'landscape-primary', angle: 0, lock: () => Promise.resolve(), unlock() {} }) }));
const perfEntries = [];
publish('performance', {
  now: () => now(),
  get timeOrigin() { return Date.now() - now(); },
  mark(name) { const e = { name, entryType: 'mark', startTime: now(), duration: 0, detail: null }; perfEntries.push(e); return e; },
  measure(name, a, b) { const e = { name, entryType: 'measure', startTime: now(), duration: 0 }; perfEntries.push(e); return e; },
  getEntries: () => perfEntries.slice(), getEntriesByName: (n) => perfEntries.filter(e => e.name === n), getEntriesByType: (t) => perfEntries.filter(e => e.entryType === t),
  clearMarks() {}, clearMeasures() {}, clearResourceTimings() {}, setResourceTimingBufferSize() {},
  timing: { navigationStart: Date.now(), fetchStart: Date.now(), domLoading: Date.now(), domInteractive: Date.now(), domContentLoadedEventStart: Date.now(), domContentLoadedEventEnd: Date.now(), loadEventStart: Date.now(), loadEventEnd: Date.now(), responseStart: Date.now(), requestStart: Date.now() },
  navigation: { type: 0, redirectCount: 0 }, memory: { jsHeapSizeLimit: 2e9, totalJSHeapSize: 1e7, usedJSHeapSize: 1e7 },
  toJSON() { return {}; }, addEventListener() {}, removeEventListener() {}, eventCounts: new Map(),
});
publish('PerformanceObserver', class PerformanceObserver { constructor(cb) { this.cb = cb; } observe() {} disconnect() {} takeRecords() { return []; } static get supportedEntryTypes() { return []; } });
publish('crypto', {
  getRandomValues(a) {
    if (!ArrayBuffer.isView(a)) throw new TypeError('Argument is not a typed array');
    const b = new Uint8Array(a.buffer, a.byteOffset, a.byteLength);
    for (let o = 0; o < b.length; o += 65536) { const tmp = new Uint8Array(Math.min(65536, b.length - o)); N.random(tmp.buffer); b.set(tmp, o); }
    return a;
  },
  randomUUID: () => K.uuid(),
  subtle: undefined,
});
publish('Crypto', function Crypto() {});

// ---- Observers and other do-nothing APIs ----
publish('IntersectionObserver', class IntersectionObserver {
  constructor(cb, opts) { this._cb = cb; this.root = (opts && opts.root) || null; this.rootMargin = (opts && opts.rootMargin) || '0px'; this.thresholds = [0]; }
  observe(el) {
    // Nothing is laid out, so call everything "visible" (this makes lazy-loading content appear).
    setTimeout(() => { try { this._cb([{ target: el, isIntersecting: true, intersectionRatio: 1, time: now(), boundingClientRect: el.getBoundingClientRect(), intersectionRect: el.getBoundingClientRect(), rootBounds: null }], this); } catch (e) { reportError(e); } }, 0);
  }
  unobserve() {} disconnect() {} takeRecords() { return []; }
});
publish('ResizeObserver', class ResizeObserver { constructor(cb) { this._cb = cb; } observe() {} unobserve() {} disconnect() {} });
publish('ReportingObserver', class ReportingObserver { observe() {} disconnect() {} takeRecords() { return []; } });
class DOMRectReadOnly {
  constructor(x = 0, y = 0, w = 0, h = 0) { this.x = x; this.y = y; this.width = w; this.height = h; }
  get left() { return Math.min(this.x, this.x + this.width); } get right() { return Math.max(this.x, this.x + this.width); }
  get top() { return Math.min(this.y, this.y + this.height); } get bottom() { return Math.max(this.y, this.y + this.height); }
  toJSON() { return { x: this.x, y: this.y, width: this.width, height: this.height, top: this.top, right: this.right, bottom: this.bottom, left: this.left }; }
  static fromRect(r = {}) { return new DOMRectReadOnly(r.x, r.y, r.width, r.height); }
}
publish('DOMRect', class DOMRect extends DOMRectReadOnly {}); publish('DOMRectReadOnly', DOMRectReadOnly);
publish('DOMPoint', class DOMPoint { constructor(x = 0, y = 0, z = 0, w = 1) { this.x = x; this.y = y; this.z = z; this.w = w; } });
publish('DOMMatrix', class DOMMatrix { constructor() { this.a = this.d = 1; this.b = this.c = this.e = this.f = 0; this.is2D = true; this.isIdentity = true; } });
publish('WebSocket', class WebSocket extends EventTarget {
  constructor(url) {
    super(); this.url = String(url); this.readyState = 0; this.bufferedAmount = 0; this.protocol = ''; this.extensions = ''; this.binaryType = 'blob';
    for (const t of ['open', 'message', 'error', 'close']) this['on' + t] = null;
    setTimeout(() => {
      this.readyState = 3;
      const e = new Event('error'); this.dispatchEvent(e); if (typeof this.onerror === 'function') this.onerror(e);
      const c = new CloseEvent('close', { code: 1006, reason: 'WebSockets are not supported in AREOS yet' }); this.dispatchEvent(c); if (typeof this.onclose === 'function') this.onclose(c);
    }, 0);
  }
  send() { throw new DOMException('WebSocket is not open', 'InvalidStateError'); }
  close() {}
});
Object.assign(G.WebSocket, { CONNECTING: 0, OPEN: 1, CLOSING: 2, CLOSED: 3 });
publish('EventSource', class EventSource extends EventTarget { constructor(url) { super(); this.url = url; this.readyState = 2; this.onerror = null; setTimeout(() => { const e = new Event('error'); this.dispatchEvent(e); if (typeof this.onerror === 'function') this.onerror(e); }, 0); } close() {} });
publish('Worker', class Worker { constructor() { throw new DOMException('Web workers are not supported in AREOS', 'NotSupportedError'); } });
publish('SharedWorker', class SharedWorker { constructor() { throw new DOMException('Shared workers are not supported in AREOS', 'NotSupportedError'); } });
publish('Notification', Object.assign(class Notification { constructor() {} close() {} }, { permission: 'denied', requestPermission: () => Promise.resolve('denied') }));
publish('BroadcastChannel', class BroadcastChannel extends EventTarget { constructor(n) { super(); this.name = n; } postMessage() {} close() {} });
class MessagePort extends EventTarget {
  constructor() { super(); this.onmessage = null; this._other = null; this._started = false; }
  postMessage(data) {
    const o = this._other;
    if (!o) return;
    setTimeout(() => { const ev = new MessageEvent('message', { data }); o.dispatchEvent(ev); if (typeof o.onmessage === 'function') o.onmessage(ev); }, 0);
  }
  start() {} close() { this._other = null; }
}
publish('MessagePort', MessagePort);
publish('MessageChannel', class MessageChannel { constructor() { this.port1 = new MessagePort(); this.port2 = new MessagePort(); this.port1._other = this.port2; this.port2._other = this.port1; } });
publish('Option', G.Option);
publish('Plugin', function Plugin() {}); publish('PluginArray', function PluginArray() {});
publish('DataTransfer', class DataTransfer { constructor() { this.dropEffect = 'none'; this.effectAllowed = 'all'; this.files = []; this.items = []; this.types = []; this._d = {}; } setData(t, v) { this._d[t] = v; } getData(t) { return this._d[t] || ''; } clearData() { this._d = {}; } setDragImage() {} });
publish('ImageData', class ImageData { constructor(w, h) { this.width = w; this.height = h; this.data = new Uint8ClampedArray(w * h * 4); } });
publish('Path2D', class Path2D {});
publish('Lock', function Lock() {});
publish('AudioContext', undefined); publish('webkitAudioContext', undefined);
publish('RTCPeerConnection', undefined);
publish('trustedTypes', undefined);

// Promise rejections nobody handled
const pending = new Map();
N.trackRejections((promise, reason, handled) => {
  if (handled) { pending.delete(promise); return; }
  pending.set(promise, reason);
  Promise.resolve().then(() => Promise.resolve()).then(() => {
    if (!pending.has(promise)) return;
    pending.delete(promise);
    const ev = new PromiseRejectionEvent('unhandledrejection', { promise, reason, cancelable: true });
    G.dispatchEvent(ev);
    if (!ev.defaultPrevented) logLine(3, 'Uncaught (in promise) ' + (reason && reason.stack ? reason.stack : String(reason)));
  });
});

// 62_intl.js - a small English-only Intl, and toLocaleString that uses it.

if (typeof G.Intl === 'undefined') {
  const group = (s) => s.replace(/\B(?=(\d{3})+(?!\d))/g, ',');
  const currencySymbols = { USD: '$', EUR: '€', GBP: '£', JPY: '¥', CNY: 'CN¥', INR: '₹', CAD: 'CA$', AUD: 'A$', KRW: '₩', MXN: 'MX$', BRL: 'R$', CHF: 'CHF ' };
  class NumberFormat {
    constructor(locale, opts = {}) {
      this.o = Object.assign({ style: 'decimal', useGrouping: true, notation: 'standard' }, opts);
      const o = this.o;
      const cur = o.style === 'currency';
      const pct = o.style === 'percent';
      if (o.minimumFractionDigits === undefined) o.minimumFractionDigits = cur ? 2 : 0;
      if (o.maximumFractionDigits === undefined) o.maximumFractionDigits = Math.max(o.minimumFractionDigits, cur ? 2 : pct ? 0 : 3);
      this.locale = 'en-US';
    }
    format(v) {
      const o = this.o;
      v = typeof v === 'bigint' ? Number(v) : Number(v);
      if (isNaN(v)) return 'NaN';
      if (!isFinite(v)) return (v < 0 ? '-' : '') + '∞';
      if (o.style === 'percent') v *= 100;
      let suffix = '';
      if (o.notation === 'compact') {
        const a = Math.abs(v);
        const units = [[1e12, 'T'], [1e9, 'B'], [1e6, 'M'], [1e3, 'K']];
        for (const [n, u] of units) if (a >= n) { v /= n; suffix = u; break; }
      }
      const neg = v < 0 || (v === 0 && 1 / v < 0);
      let a = Math.abs(v);
      let maxF = o.maximumFractionDigits, minF = o.minimumFractionDigits;
      if (o.notation === 'compact' && o.maximumFractionDigits === 3 && o.minimumFractionDigits === 0) { maxF = a < 100 && suffix ? 1 : 0; }
      if (o.maximumSignificantDigits) { a = Number(a.toPrecision(o.maximumSignificantDigits)); maxF = 20; }
      let s = a.toFixed(Math.min(maxF, 100));
      if (a >= 1e21) s = String(a);
      let [ip, fp = ''] = s.split('.');
      while (fp.length > minF && fp.endsWith('0')) fp = fp.slice(0, -1);
      if (o.minimumIntegerDigits) ip = ip.padStart(o.minimumIntegerDigits, '0');
      if (o.useGrouping !== false && o.useGrouping !== 'false') ip = group(ip);
      let out = ip + (fp ? '.' + fp : '') + suffix;
      if (o.style === 'currency') out = (currencySymbols[o.currency] || o.currency + ' ') + out;
      if (o.style === 'percent') out += '%';
      if (o.style === 'unit') out += ' ' + (o.unit || '');
      if (neg && Number(s) !== 0) out = '-' + out;
      return out;
    }
    formatToParts(v) { return [{ type: 'literal', value: this.format(v) }]; }
    resolvedOptions() { return Object.assign({ locale: 'en-US', numberingSystem: 'latn' }, this.o); }
    static supportedLocalesOf(l) { return [].concat(l || []); }
  }
  const months = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
  const days = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  const p2 = (n) => (n < 10 ? '0' : '') + n;
  class DateTimeFormat {
    constructor(locale, opts = {}) { this.o = Object.assign({}, opts); this.locale = 'en-US'; }
    format(d) {
      d = d === undefined ? new Date() : new Date(d);
      if (isNaN(d)) return 'Invalid Date';
      const o = this.o;
      const utc = o.timeZone === 'UTC';
      const get = (k) => utc ? d['getUTC' + k]() : d['get' + k]();
      const hasDate = o.year || o.month || o.day || o.weekday || o.dateStyle;
      const hasTime = o.hour || o.minute || o.second || o.timeStyle;
      let useDate = hasDate, useTime = hasTime;
      if (!hasDate && !hasTime) useDate = true;
      const parts = [];
      if (useDate) {
        let year = o.year, month = o.month, day = o.day, wd = o.weekday;
        if (o.dateStyle) { const st = o.dateStyle; year = 'numeric'; if (st === 'full') { wd = 'long'; month = 'long'; day = 'numeric'; } else if (st === 'long') { month = 'long'; day = 'numeric'; } else if (st === 'medium') { month = 'short'; day = 'numeric'; } else { month = 'numeric'; day = 'numeric'; year = '2-digit'; } }
        if (!hasDate) { year = 'numeric'; month = 'numeric'; day = 'numeric'; }
        const y = get('FullYear'), m = get('Month'), dd = get('Date'), w = get('Day');
        const ys = year === '2-digit' ? p2(y % 100) : String(y);
        const ds = day === '2-digit' ? p2(dd) : String(dd);
        let s;
        if (month === 'long' || month === 'short' || month === 'narrow') {
          const mn = month === 'long' ? months[m] : month === 'short' ? months[m].slice(0, 3) : months[m][0];
          s = mn + (day ? ' ' + ds : '') + (year ? (day ? ', ' : ' ') + ys : '');
        } else {
          const ms = month === '2-digit' ? p2(m + 1) : String(m + 1);
          const bits = [];
          if (month) bits.push(ms); if (day) bits.push(ds); if (year) bits.push(ys);
          s = bits.join('/');
        }
        if (wd) { const wn = wd === 'long' ? days[w] : days[w].slice(0, 3); s = s ? wn + ', ' + s : wn; }
        parts.push(s);
      }
      if (useTime) {
        let hr = o.hour, mi = o.minute, se = o.second;
        if (o.timeStyle) { hr = 'numeric'; mi = '2-digit'; if (o.timeStyle !== 'short') se = '2-digit'; }
        if (!hasTime) { hr = 'numeric'; mi = '2-digit'; se = '2-digit'; }
        const h24 = o.hour12 === false || o.hourCycle === 'h23';
        let h = get('Hours');
        const ampm = h >= 12 ? 'PM' : 'AM';
        if (!h24) h = h % 12 || 12;
        const bits = [];
        if (hr) bits.push(hr === '2-digit' || h24 ? p2(h) : String(h));
        if (mi) bits.push(hr ? p2(get('Minutes')) : String(get('Minutes')));
        if (se) bits.push(p2(get('Seconds')));
        let s = bits.join(':');
        if (hr && !h24) s += ' ' + ampm;
        if (o.timeZoneName) s += ' ' + (utc ? 'UTC' : 'GMT');
        parts.push(s);
      }
      return parts.join(o.dateStyle && o.timeStyle && (o.dateStyle === 'full' || o.dateStyle === 'long') ? ' at ' : ', ');
    }
    formatToParts(d) { return [{ type: 'literal', value: this.format(d) }]; }
    formatRange(a, b) { return this.format(a) + ' – ' + this.format(b); }
    resolvedOptions() { return Object.assign({ locale: 'en-US', calendar: 'gregory', numberingSystem: 'latn', timeZone: this.o.timeZone || 'UTC' }, this.o); }
    static supportedLocalesOf(l) { return [].concat(l || []); }
  }
  class PluralRules {
    constructor(l, o = {}) { this.type = o.type || 'cardinal'; }
    select(n) {
      if (this.type === 'ordinal') { const a = n % 10, b = n % 100; return a === 1 && b !== 11 ? 'one' : a === 2 && b !== 12 ? 'two' : a === 3 && b !== 13 ? 'few' : 'other'; }
      return n === 1 ? 'one' : 'other';
    }
    resolvedOptions() { return { locale: 'en-US', type: this.type }; }
    static supportedLocalesOf(l) { return [].concat(l || []); }
  }
  class Collator {
    constructor(l, o = {}) { this.o = o; }
    compare = (a, b) => {
      a = String(a); b = String(b);
      if (this.o.numeric) { const re = /(\d+)|(\D+)/g; const pa = a.match(re) || [], pb = b.match(re) || []; for (let i = 0; i < Math.min(pa.length, pb.length); i++) { const x = pa[i], y = pb[i]; if (/^\d/.test(x) && /^\d/.test(y)) { if (+x !== +y) return +x < +y ? -1 : 1; } else { const c = x.toLowerCase().localeCompare(y.toLowerCase()); if (c) return c; } } return pa.length - pb.length; }
      if (this.o.sensitivity === 'base' || this.o.sensitivity === 'accent') { a = a.toLowerCase(); b = b.toLowerCase(); }
      return a.localeCompare(b);
    };
    resolvedOptions() { return { locale: 'en-US', usage: 'sort', sensitivity: 'variant' }; }
    static supportedLocalesOf(l) { return [].concat(l || []); }
  }
  class RelativeTimeFormat {
    constructor(l, o = {}) { this.o = o; }
    format(v, unit) {
      unit = String(unit).replace(/s$/, '');
      const a = Math.abs(v);
      if (this.o.numeric === 'auto') { if (v === 0) return unit === 'day' ? 'today' : 'this ' + unit; if (v === 1 && unit === 'day') return 'tomorrow'; if (v === -1 && unit === 'day') return 'yesterday'; if (v === 1) return 'next ' + unit; if (v === -1) return 'last ' + unit; }
      const u = a === 1 ? unit : unit + 's';
      return v < 0 || (v === 0 && 1 / v < 0) ? a + ' ' + u + ' ago' : 'in ' + a + ' ' + u;
    }
    static supportedLocalesOf(l) { return [].concat(l || []); }
  }
  class ListFormat {
    constructor(l, o = {}) { this.o = o; }
    format(list) {
      const a = [...list];
      const word = this.o.type === 'disjunction' ? 'or' : 'and';
      if (a.length <= 1) return a.join('');
      if (a.length === 2) return a[0] + ' ' + word + ' ' + a[1];
      return a.slice(0, -1).join(', ') + ', ' + word + ' ' + a[a.length - 1];
    }
  }
  class Segmenter {
    constructor(l, o = {}) { this.g = o.granularity || 'grapheme'; }
    segment(str) {
      str = String(str);
      const out = [];
      if (this.g === 'word') { const re = /[\p{L}\p{N}_']+|[^\p{L}\p{N}_']/gu; let m; while ((m = re.exec(str))) out.push({ segment: m[0], index: m.index, input: str, isWordLike: /[\p{L}\p{N}]/u.test(m[0]) }); }
      else { let i = 0; for (const ch of str) { out.push({ segment: ch, index: i, input: str }); i += ch.length; } }
      return out;
    }
  }
  class DisplayNames { constructor(l, o = {}) { this.o = o; } of(code) { return code; } }
  class Locale { constructor(tag) { this.baseName = String(tag); const p = this.baseName.split('-'); this.language = p[0]; this.region = p[1]; } toString() { return this.baseName; } }
  const Intl = {
    NumberFormat, DateTimeFormat, PluralRules, Collator, RelativeTimeFormat, ListFormat, Segmenter, DisplayNames, Locale,
    getCanonicalLocales: (l) => [].concat(l || []),
    supportedValuesOf: () => [],
  };
  publish('Intl', Intl);
  const nf = (o) => new NumberFormat('en-US', o);
  hid(Number.prototype, 'toLocaleString', function (l, o) { return nf(o).format(this.valueOf()); });
  hid(BigInt.prototype, 'toLocaleString', function (l, o) { return nf(o).format(Number(this.valueOf())); });
  const df = (o, d) => new DateTimeFormat('en-US', o).format(d);
  hid(Date.prototype, 'toLocaleDateString', function (l, o) { return df(o && (o.year || o.month || o.day || o.weekday || o.dateStyle) ? o : Object.assign({ year: 'numeric', month: 'numeric', day: 'numeric' }, o), this); });
  hid(Date.prototype, 'toLocaleTimeString', function (l, o) { return df(o && (o.hour || o.minute || o.second || o.timeStyle) ? o : Object.assign({ hour: 'numeric', minute: '2-digit', second: '2-digit' }, o), this); });
  hid(Date.prototype, 'toLocaleString', function (l, o) { return df(o && (o.year || o.month || o.day || o.weekday || o.hour || o.minute || o.dateStyle || o.timeStyle) ? o : Object.assign({ year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: '2-digit', second: '2-digit' }, o), this); });
  hid(Array.prototype, 'toLocaleString', function () { return this.map(x => x == null ? '' : x.toLocaleString()).join(','); });
}

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

// 90_boot.js - what the host calls. Returns the object jshost.c keeps.

K.nextTimer = function () {
  let wait = -1;
  const t = now();
  for (const x of timerList) { const w = Math.max(0, x.due - t); if (wait < 0 || w < wait) wait = w; }
  if (rafList.length) { const w = Math.max(0, nextFrame - t); if (wait < 0 || w < wait) wait = w; }
  return Math.ceil(wait);
};

return {
  start: K.start,
  step: K.step,
  loading: K.loading,
  timers: K.runTimers,
  nextTimer: K.nextTimer,
  render: K.render,
  renderFull: K.renderFull,
  click: K.userClick,
  input: K.userInput,
  key: K.userKey,
  resolveModule: K.resolveModule,
  canGo: (d) => { const h = K.history; const to = h._i + d; return to >= 0 && to < h._e.length; },
  go: (d) => { K.history.go(d); return 1; },
  consoleText: K.consoleText,
};

})
