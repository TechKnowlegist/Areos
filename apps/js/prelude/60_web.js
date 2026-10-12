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
