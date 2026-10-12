// web_color_f.c - colors in the newer CSS color spaces (oklab, oklch, lab, lch).
//
// These need floating point, so this file is built with it switched on. The
// rest of the browser only passes whole numbers in and gets a color back.

#include "web_css.h"
#include <math.h>

static double gamma_encode(double v) {
    if (v <= 0.0031308) return 12.92 * v;
    return 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}

static uint32_t pack(double r, double g, double b) {
    r = gamma_encode(r); g = gamma_encode(g); b = gamma_encode(b);
    int ri = (int)(r * 255.0 + 0.5), gi = (int)(g * 255.0 + 0.5), bi = (int)(b * 255.0 + 0.5);
    if (ri < 0) ri = 0; if (ri > 255) ri = 255;
    if (gi < 0) gi = 0; if (gi > 255) gi = 255;
    if (bi < 0) bi = 0; if (bi > 255) bi = 255;
    return ((uint32_t)ri << 16) | ((uint32_t)gi << 8) | (uint32_t)bi;
}

static uint32_t oklab(double L, double a, double b) {
    double l_ = L + 0.3963377774 * a + 0.2158037573 * b;
    double m_ = L - 0.1055613458 * a - 0.0638541728 * b;
    double s_ = L - 0.0894841775 * a - 1.2914855480 * b;
    double l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
    double r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    double g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    double bl = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    return pack(r, g, bl);
}

uint32_t web_oklab_to_rgb(int l_milli, int a_milli, int b_milli) {
    return oklab(l_milli / 1000.0, a_milli / 1000.0, b_milli / 1000.0);
}

uint32_t web_oklch_to_rgb(int l_milli, int c_milli, int h_milli) {
    double h = (h_milli / 1000.0) * 3.14159265358979323846 / 180.0;
    double c = c_milli / 1000.0;
    return oklab(l_milli / 1000.0, c * cos(h), c * sin(h));
}

uint32_t web_lab_to_rgb(int l_milli, int a_milli, int b_milli) {
    double L = l_milli / 1000.0, a = a_milli / 1000.0, b = b_milli / 1000.0;
    // D50 reference white
    double fy = (L + 16.0) / 116.0, fx = a / 500.0 + fy, fz = fy - b / 200.0;
    const double e = 216.0 / 24389.0, k = 24389.0 / 27.0;
    double xr = fx * fx * fx > e ? fx * fx * fx : (116.0 * fx - 16.0) / k;
    double yr = L > k * e ? fy * fy * fy : L / k;
    double zr = fz * fz * fz > e ? fz * fz * fz : (116.0 * fz - 16.0) / k;
    double X = xr * 0.96422, Y = yr, Z = zr * 0.82521;
    // Bradford D50 -> D65
    double X2 = 0.9555766 * X - 0.0230393 * Y + 0.0631636 * Z;
    double Y2 = -0.0282895 * X + 1.0099416 * Y + 0.0210077 * Z;
    double Z2 = 0.0122982 * X - 0.0204830 * Y + 1.3299098 * Z;
    double r = 3.2404542 * X2 - 1.5371385 * Y2 - 0.4985314 * Z2;
    double g = -0.9692660 * X2 + 1.8760108 * Y2 + 0.0415560 * Z2;
    double bl = 0.0556434 * X2 - 0.2040259 * Y2 + 1.0572252 * Z2;
    return pack(r, g, bl);
}

uint32_t web_lch_to_rgb(int l_milli, int c_milli, int h_milli) {
    double h = (h_milli / 1000.0) * 3.14159265358979323846 / 180.0;
    double c = c_milli / 1000.0;
    return web_lab_to_rgb(l_milli, (int)(c * cos(h) * 1000.0), (int)(c * sin(h) * 1000.0));
}
