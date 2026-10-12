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
