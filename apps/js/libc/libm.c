// libm.c - the maths functions JavaScript's Math object needs.
//
// The processor's own floating point unit (x87) can do sines, logarithms and
// the like to more precision than a double holds, so these are thin wrappers
// around its instructions, computing in 80-bit "long double" and rounding at
// the end. The rest is plain arithmetic.

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

typedef long double ld;

static inline uint64_t bits_of(double x) { union { double d; uint64_t u; } v = { .d = x }; return v.u; }
static inline double from_bits(uint64_t u) { union { double d; uint64_t u; } v = { .u = u }; return v.d; }

double fabs(double x) { return from_bits(bits_of(x) & ~(1ull << 63)); }
double sqrt(double x) { double r; __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x)); return r; }
double fmax(double x, double y) { if (x != x) return y; if (y != y) return x; return x > y ? x : y; }
double fmin(double x, double y) { if (x != x) return y; if (y != y) return x; return x < y ? x : y; }

double trunc(double x) {
    if (!(fabs(x) < 4503599627370496.0)) return x;
    double t = (double)(int64_t)x;
    return t == 0 && (bits_of(x) >> 63) ? -0.0 : t;
}
double floor(double x) {
    double t = trunc(x);
    return t > x ? t - 1.0 : t;
}
double ceil(double x) {
    double t = trunc(x);
    double r = t < x ? t + 1.0 : t;
    return r == 0 && (x < 0 || (bits_of(x) >> 63)) ? -0.0 : r;
}
double round(double x) {
    if (!(fabs(x) < 4503599627370496.0)) return x;
    double r = floor(fabs(x) + 0.5);
    return x < 0 || (x == 0 && (bits_of(x) >> 63)) ? -r : r;
}
long lrint(double x) {
    long r;
    __asm__("cvtsd2si %1, %0" : "=r"(r) : "x"(x));
    return r;
}

// ---- x87 building blocks ----

static inline ld x_sin(ld x) { ld r; __asm__("fsin" : "=t"(r) : "0"(x)); return r; }
static inline ld x_cos(ld x) { ld r; __asm__("fcos" : "=t"(r) : "0"(x)); return r; }
static inline ld x_tan(ld x) { ld r; __asm__("fptan; fstp %%st(0)" : "=t"(r) : "0"(x) : "st(1)"); return r; }
static inline ld x_atan2(ld y, ld x) { ld r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static inline ld x_log2(ld x) { ld r; __asm__("fld1; fxch; fyl2x" : "=t"(r) : "0"(x) : "st(1)"); return r; }
static inline ld x_log2p1(ld x) { ld r; __asm__("fld1; fxch; fyl2xp1" : "=t"(r) : "0"(x) : "st(1)"); return r; }
static inline ld x_2xm1(ld x) { ld r; __asm__("f2xm1" : "=t"(r) : "0"(x)); return r; }
static inline ld x_rint(ld x) { ld r; __asm__("frndint" : "=t"(r) : "0"(x)); return r; }
static inline ld x_scale(ld x, ld e) { ld r; __asm__("fscale" : "=t"(r) : "0"(x), "u"(e) : "st(1)"); return r; }

// 2^y
static ld exp2l_(ld y) {
    if (y > 16384) return (ld)INFINITY;
    if (y < -16445) return 0;
    ld n = x_rint(y);
    ld f = y - n;                       // |f| <= 0.5
    return x_scale(x_2xm1(f) + 1, n);
}
static const ld LOG2E = 1.4426950408889634073599246810018921L;
static const ld LN2 = 0.6931471805599453094172321214581766L;
static ld expl_(ld x) { return exp2l_(x * LOG2E); }
static ld logl_(ld x) { return x_log2(x) * LN2; }

// Bring a huge angle into a range fsin/fcos/fptan accept.
static ld reduce_angle(ld x) {
    if (x > -9.0e18L && x < 9.0e18L) return x;
    ld twopi = 6.2831853071795864769252867665590058L;
    ld r = x;
    __asm__("1: fprem1; fnstsw %%ax; testb $4, %%ah; jnz 1b" : "+t"(r) : "u"(twopi) : "ax", "cc", "st(1)");
    return r;
}

double sin(double x) {
    if (x != x || x == INFINITY || x == -INFINITY) return NAN;
    return (double)x_sin(reduce_angle(x));
}
double cos(double x) {
    if (x != x || x == INFINITY || x == -INFINITY) return NAN;
    return (double)x_cos(reduce_angle(x));
}
double tan(double x) {
    if (x != x || x == INFINITY || x == -INFINITY) return NAN;
    return (double)x_tan(reduce_angle(x));
}
double atan(double x) { return (double)x_atan2(x, 1); }
double atan2(double y, double x) { return (double)x_atan2(y, x); }
static ld x_sqrt(ld x) { ld r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
double asin(double x) {
    if (x != x || x > 1 || x < -1) return NAN;
    ld s = x;
    return (double)x_atan2(s, x_sqrt(1 - s * s));
}
double acos(double x) {
    if (x != x || x > 1 || x < -1) return NAN;
    ld s = x;
    return (double)x_atan2(x_sqrt(1 - s * s), s);
}

double exp(double x) {
    if (x != x) return x;
    if (x > 709.79) return INFINITY;
    if (x < -745.2) return 0;
    return (double)expl_(x);
}
double expm1(double x) {
    if (x != x) return x;
    if (x > 709.79) return INFINITY;
    if (x < -40) return -1;
    ld y = (ld)x * LOG2E;
    if (y > -1 && y < 1) return (double)x_2xm1(y);
    return (double)(expl_(x) - 1);
}
double log(double x) {
    if (x != x || x < 0) return NAN;
    if (x == 0) return -INFINITY;
    if (x == INFINITY) return x;
    return (double)logl_(x);
}
double log2(double x) {
    if (x != x || x < 0) return NAN;
    if (x == 0) return -INFINITY;
    if (x == INFINITY) return x;
    return (double)x_log2(x);
}
double log10(double x) {
    if (x != x || x < 0) return NAN;
    if (x == 0) return -INFINITY;
    if (x == INFINITY) return x;
    return (double)(x_log2(x) * 0.30102999566398119521373889472449302L);
}
double log1p(double x) {
    if (x != x || x < -1) return NAN;
    if (x == -1) return -INFINITY;
    if (x == INFINITY) return x;
    if (fabs(x) < 0.29) return (double)(x_log2p1(x) * LN2);
    return (double)logl_(1 + (ld)x);
}

double pow(double x, double y) {
    if (y == 0) return 1;
    if (x == 1) return 1;
    if (x != x || y != y) return NAN;
    if (y == INFINITY || y == -INFINITY) {
        double a = fabs(x);
        if (a == 1) return NAN;
        return (a > 1) == (y > 0) ? INFINITY : 0;
    }
    bool y_int = y == trunc(y);
    bool y_odd = y_int && fabs(y) < 9007199254740992.0 && ((int64_t)y & 1);
    if (x == 0) {
        bool neg0 = (bits_of(x) >> 63) != 0;
        if (y > 0) return (neg0 && y_odd) ? -0.0 : 0.0;
        return (neg0 && y_odd) ? -INFINITY : INFINITY;
    }
    if (x == INFINITY) return y > 0 ? INFINITY : 0;
    if (x == -INFINITY) {
        if (y > 0) return y_odd ? -INFINITY : INFINITY;
        return y_odd ? -0.0 : 0.0;
    }
    if (x < 0 && !y_int) return NAN;
    // Whole-number powers by repeated squaring keep results like 10**2 exact.
    if (y_int && fabs(y) <= 1024) {
        ld result = 1, base = x;
        long n = (long)fabs(y);
        while (n) {
            if (n & 1) result *= base;
            base *= base;
            n >>= 1;
        }
        return (double)(y < 0 ? 1 / result : result);
    }
    ld r = exp2l_((ld)y * x_log2(x < 0 ? -(ld)x : (ld)x));
    return (double)((x < 0 && y_odd) ? -r : r);
}

double fmod(double x, double y) {
    if (x != x || y != y || y == 0 || x == INFINITY || x == -INFINITY) return NAN;
    if (y == INFINITY || y == -INFINITY) return x;
    ld r = x, d = y;
    __asm__("1: fprem; fnstsw %%ax; testb $4, %%ah; jnz 1b" : "+t"(r) : "u"(d) : "ax", "cc", "st(1)");
    return (double)r;
}

double hypot(double x, double y) {
    if (x == INFINITY || x == -INFINITY || y == INFINITY || y == -INFINITY) return INFINITY;
    ld a = x, b = y;
    ld s = a * a + b * b;
    return (double)x_sqrt(s);
}

double sinh(double x) {
    if (x != x || x == 0) return x;
    if (fabs(x) < 1e-5) return x;
    ld e = expl_(fabs(x));
    ld r = (e - 1 / e) / 2;
    return x < 0 ? -(double)r : (double)r;
}
double cosh(double x) {
    if (x != x) return x;
    ld e = expl_(fabs(x));
    return (double)((e + 1 / e) / 2);
}
double tanh(double x) {
    if (x != x || x == 0) return x;
    if (x > 22) return 1;
    if (x < -22) return -1;
    ld e = expl_(2 * (ld)x);
    return (double)((e - 1) / (e + 1));
}
double asinh(double x) {
    if (x != x || x == 0 || x == INFINITY || x == -INFINITY) return x;
    ld a = fabs(x);
    ld r = logl_(a + x_sqrt(a * a + 1));
    if (a < 1e-4L) r = a;
    return x < 0 ? -(double)r : (double)r;
}
double acosh(double x) {
    if (x != x || x < 1) return NAN;
    if (x == INFINITY) return x;
    ld a = x;
    return (double)logl_(a + x_sqrt(a * a - 1));
}
double atanh(double x) {
    if (x != x || x > 1 || x < -1) return NAN;
    if (x == 1) return INFINITY;
    if (x == -1) return -INFINITY;
    ld a = x;
    return (double)(x_log2p1(2 * a / (1 - a)) * LN2 / 2);
}
double cbrt(double x) {
    if (x != x || x == 0 || x == INFINITY || x == -INFINITY) return x;
    ld a = x < 0 ? -(ld)x : (ld)x;
    ld r = expl_(logl_(a) / 3);
    r = r - (r * r * r - a) / (3 * r * r);   // one Newton step
    r = r - (r * r * r - a) / (3 * r * r);
    return x < 0 ? -(double)r : (double)r;
}
