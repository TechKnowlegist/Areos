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
