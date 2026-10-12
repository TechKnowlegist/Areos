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
