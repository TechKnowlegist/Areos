// math.c - the maths functions the speech engine needs.
//
// Flite only uses a handful: sqrt, fabs, ceil, floor, exp, log, pow and cos.
// These are written to be simple and accurate to about 15 digits, which is
// far more than speech needs.

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

static inline uint64_t bits_of(double x) {
    union { double d; uint64_t u; } v = { .d = x };
    return v.u;
}
static inline double double_of(uint64_t u) {
    union { double d; uint64_t u; } v = { .u = u };
    return v.d;
}

double fabs(double x) { return double_of(bits_of(x) & ~(1ull << 63)); }
float fabsf(float x) { return (float)fabs(x); }

double sqrt(double x) {
    double r;
    __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}
float sqrtf(float x) { return (float)sqrt(x); }

double trunc(double x) {
    if (!(fabs(x) < 4503599627370496.0)) return x;   // already whole (or NaN/inf)
    return (double)(int64_t)x;
}

double floor(double x) {
    double t = trunc(x);
    return (t > x) ? t - 1.0 : t;
}
float floorf(float x) { return (float)floor(x); }

double ceil(double x) {
    double t = trunc(x);
    return (t < x) ? t + 1.0 : t;
}
float ceilf(float x) { return (float)ceil(x); }

double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }
double rint(double x) { return round(x); }
long lrint(double x) { return (long)round(x); }

double fmod(double x, double y) {
    if (y == 0) return NAN;
    return x - trunc(x / y) * y;
}

// x * 2^e, done by adjusting the exponent bits.
double ldexp(double x, int e) {
    while (e > 1023) { x *= 8.98846567431158e307; e -= 1023; }    // 2^1023
    while (e < -1022) { x *= 2.2250738585072014e-308; e += 1022; } // 2^-1022
    return x * double_of((uint64_t)(e + 1023) << 52);
}

// Split x into m * 2^e with m in [0.5, 1).
double frexp(double x, int *e) {
    uint64_t u = bits_of(x);
    int raw = (int)((u >> 52) & 0x7FF);
    if (raw == 0) {
        if (x == 0) { *e = 0; return x; }
        x *= 18014398509481984.0;   // 2^54: make it a normal number first
        u = bits_of(x);
        raw = (int)((u >> 52) & 0x7FF) - 54;
    } else if (raw == 0x7FF) {
        *e = 0;
        return x;
    }
    *e = raw - 1022;
    return double_of((u & ~(0x7FFull << 52)) | (1022ull << 52));
}

double exp(double x) {
    if (x != x) return x;
    if (x > 709.78) return INFINITY;
    if (x < -745.2) return 0.0;
    // x = k*ln2 + r, with |r| <= ln2/2. ln2 is split in two for precision.
    const double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10;
    double kf = floor(x * 1.44269504088896338700 + 0.5);
    int k = (int)kf;
    double r = (x - kf * ln2_hi) - kf * ln2_lo;
    // e^r by its Taylor series; |r| < 0.35 so 14 terms are plenty.
    double term = 1.0, sum = 1.0;
    for (int n = 1; n <= 14; n++) {
        term *= r / n;
        sum += term;
    }
    return ldexp(sum, k);
}
float expf(float x) { return (float)exp(x); }

double log(double x) {
    if (x != x || x < 0) return NAN;
    if (x == 0) return -INFINITY;
    if (x == INFINITY) return x;
    int e;
    double m = frexp(x, &e);           // x = m * 2^e, m in [0.5, 1)
    if (m < 0.70710678118654752440) {  // keep m near 1: [sqrt(.5), sqrt(2))
        m *= 2;
        e--;
    }
    // log(m) = 2 * atanh(s) with s = (m-1)/(m+1), |s| < 0.172
    double s = (m - 1) / (m + 1), s2 = s * s;
    double term = s, sum = 0;
    for (int n = 1; n <= 31; n += 2) {
        sum += term / n;
        term *= s2;
    }
    return 2 * sum + e * 0.69314718055994530942;
}
float logf(float x) { return (float)log(x); }
double log10(double x) { return log(x) / 2.30258509299404568402; }
double log2(double x) { return log(x) / 0.69314718055994530942; }

double pow(double x, double y) {
    if (y == 0) return 1.0;
    if (x == 1) return 1.0;
    if (y != y || x != x) return NAN;
    // Whole-number powers: multiply, which is exact and handles negative x.
    if (y == trunc(y) && fabs(y) <= 64) {
        double result = 1, base = x;
        long n = (long)fabs(y);
        while (n) {
            if (n & 1) result *= base;
            base *= base;
            n >>= 1;
        }
        return y < 0 ? 1 / result : result;
    }
    if (x == 0) return y > 0 ? 0.0 : INFINITY;
    if (x < 0) {
        if (y != trunc(y)) return NAN;
        double r = exp(y * log(-x));
        return fmod(y, 2.0) != 0 ? -r : r;
    }
    return exp(y * log(x));
}
float powf(float x, float y) { return (float)pow(x, y); }

// sin and cos: bring x into [-pi/4, pi/4], then use Taylor series.
static double sin_small(double x) {
    double x2 = x * x, term = x, sum = x;
    for (int n = 3; n <= 21; n += 2) {
        term *= -x2 / ((n - 1) * n);
        sum += term;
    }
    return sum;
}
static double cos_small(double x) {
    double x2 = x * x, term = 1, sum = 1;
    for (int n = 2; n <= 22; n += 2) {
        term *= -x2 / ((n - 1) * n);
        sum += term;
    }
    return sum;
}
static double quarter_turns(double x, int *quadrant) {
    // x = q * pi/2 + r. pi/2 is split in three for precision with big x.
    const double p1 = 1.57079632673412561417e+00, p2 = 6.07710050650619224932e-11,
                 p3 = 2.02226624879595063154e-21;
    double q = floor(x * 0.63661977236758134308 + 0.5);
    *quadrant = (int)((int64_t)q & 3);
    return ((x - q * p1) - q * p2) - q * p3;
}
double sin(double x) {
    int quadrant;
    double r = quarter_turns(x, &quadrant);
    switch (quadrant) {
    case 0: return sin_small(r);
    case 1: return cos_small(r);
    case 2: return -sin_small(r);
    default: return -cos_small(r);
    }
}
double cos(double x) {
    int quadrant;
    double r = quarter_turns(x, &quadrant);
    switch (quadrant) {
    case 0: return cos_small(r);
    case 1: return -sin_small(r);
    case 2: return -cos_small(r);
    default: return sin_small(r);
    }
}
float sinf(float x) { return (float)sin(x); }
float cosf(float x) { return (float)cos(x); }
double tan(double x) { return sin(x) / cos(x); }

double atan(double x) {
    // Reduce to |x| <= 0.4142 (tan(pi/8)) using atan(x) = pi/2 - atan(1/x)
    // and atan(x) = pi/4 + atan((x-1)/(x+1)), then a series.
    bool negative = x < 0;
    if (negative) x = -x;
    double offset = 0;
    if (x > 1) { x = -1 / x; offset = 1.57079632679489661923; }
    if (x > 0.41421356237309504880) { x = (x - 1) / (x + 1); offset += 0.78539816339744830962; }
    else if (x < -0.41421356237309504880) { x = (x + 1) / (1 - x); offset -= 0.78539816339744830962; }
    double x2 = x * x, term = x, sum = 0;
    for (int n = 1; n <= 41; n += 2) {
        sum += term / n;
        term *= -x2;
    }
    double r = offset + sum;
    return negative ? -r : r;
}
double atan2(double y, double x) {
    if (x > 0) return atan(y / x);
    if (x < 0) return atan(y / x) + (y >= 0 ? M_PI : -M_PI);
    if (y > 0) return M_PI / 2;
    if (y < 0) return -M_PI / 2;
    return 0;
}
double asin(double x) { return atan2(x, sqrt(1 - x * x)); }
double acos(double x) { return atan2(sqrt(1 - x * x), x); }
double sinh(double x) { double e = exp(x); return (e - 1 / e) / 2; }
double cosh(double x) { double e = exp(x); return (e + 1 / e) / 2; }
double tanh(double x) {
    if (x > 20) return 1;
    if (x < -20) return -1;
    double e = exp(2 * x);
    return (e - 1) / (e + 1);
}
double modf(double x, double *ip) {
    double t = trunc(x);
    *ip = t;
    return x - t;
}
double hypot(double x, double y) { return sqrt(x * x + y * y); }
