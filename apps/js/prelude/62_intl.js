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
