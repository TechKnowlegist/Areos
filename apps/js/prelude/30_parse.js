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
