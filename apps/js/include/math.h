#ifndef AREOS_JS_MATH_H
#define AREOS_JS_MATH_H
#define M_PI 3.14159265358979323846
#define M_E 2.7182818284590452354
#define HUGE_VAL __builtin_huge_val()
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
double sqrt(double x); double fabs(double x);
double floor(double x); double ceil(double x); double round(double x); double trunc(double x);
long lrint(double x);
double fmod(double x, double y);
double fmax(double x, double y); double fmin(double x, double y);
double exp(double x); double expm1(double x);
double log(double x); double log2(double x); double log10(double x); double log1p(double x);
double pow(double x, double y);
double sin(double x); double cos(double x); double tan(double x);
double asin(double x); double acos(double x); double atan(double x); double atan2(double y, double x);
double sinh(double x); double cosh(double x); double tanh(double x);
double asinh(double x); double acosh(double x); double atanh(double x);
double cbrt(double x); double hypot(double x, double y);
#endif
