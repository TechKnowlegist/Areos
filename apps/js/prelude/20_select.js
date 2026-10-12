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
