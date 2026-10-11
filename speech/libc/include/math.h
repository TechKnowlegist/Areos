#ifndef SPEECH_MATH_H
#define SPEECH_MATH_H
#define M_PI 3.14159265358979323846
#define M_E 2.7182818284590452354
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define HUGE_VAL __builtin_huge_val()
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
double sqrt(double x);   float sqrtf(float x);
double fabs(double x);   float fabsf(float x);
double floor(double x);  float floorf(float x);
double ceil(double x);   float ceilf(float x);
double round(double x);  double rint(double x); long lrint(double x);
double trunc(double x);
double fmod(double x, double y);
double exp(double x);    float expf(float x);
double log(double x);    float logf(float x);
double log10(double x);  double log2(double x);
double pow(double x, double y); float powf(float x, float y);
double sin(double x);    double cos(double x);   double tan(double x);
float sinf(float x);     float cosf(float x);
double atan(double x);   double atan2(double y, double x);
double asin(double x);   double acos(double x);
double sinh(double x);   double cosh(double x);  double tanh(double x);
double frexp(double x, int *e); double ldexp(double x, int e);
double modf(double x, double *ip);
double hypot(double x, double y);
#endif
