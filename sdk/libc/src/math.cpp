#include "internal.h"
#include "shared/math.h"
#include <errno.h>

namespace Shared = LIBC_NAMESPACE::shared;

namespace {

constexpr double gLn2 = 0.69314718055994530942;
constexpr double gLogMaxDouble = 709.78271289338397;
constexpr double gSqrtPi = 1.77245385090551602730;

// log(Gamma(x)) for x > 0 (Lanczos, g = 7, n = 9; relative error around 1e-15)
double Log_Gamma_Positive(double x) {
    static constexpr double coefficients[9] = {
        0.99999999999980993, 676.5203681218851,    -1259.1392167224028,   771.32342877765313,    -176.61502916214059,
        12.507343278686905,  -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7,
    };
    double sum;
    double t;

    if (x < 0.5) {
        // Reflection: Gamma(x) Gamma(1 - x) = pi / sin(pi x)
        return Shared::log(M_PI / Shared::fabs(Shared::sin(M_PI * x))) - Log_Gamma_Positive(1.0 - x);
    }

    x -= 1.0;
    sum = coefficients[0];
    for (int index = 1; index < 9; index++) {
        sum += coefficients[index] / (x + index);
    }

    t = x + 7.5;
    return 0.5 * Shared::log(2.0 * M_PI) + (x + 0.5) * Shared::log(t) - t + Shared::log(sum);
}

// erfc(x) for x >= 0.5 by its continued fraction (modified Lentz)
double Erfc_Continued_Fraction(double x) {
    const double tiny = 1e-300;
    double b = x * x + 0.5;
    double c = 1.0 / tiny;
    double d = 1.0 / b;
    double h = d;

    for (int index = 1; index < 300; index++) {
        double a = -index * (index - 0.5);
        double delta;

        b += 2.0;
        d = a * d + b;
        if (Shared::fabs(d) < tiny) {
            d = tiny;
        }

        c = b + a / c;
        if (Shared::fabs(c) < tiny) {
            c = tiny;
        }

        d = 1.0 / d;
        delta = d * c;
        h *= delta;
        if (Shared::fabs(delta - 1.0) < 1e-16) {
            break;
        }
    }
    return x * Shared::exp(-x * x) * h / gSqrtPi;
}

// erf(x) for |x| < 0.5 by its Taylor series
double Erf_Series(double x) {
    double square = x * x;
    double term = x;
    double sum = x;

    for (int index = 1; index < 40; index++) {
        double next;

        term *= -square / index;
        next = term / (2 * index + 1);
        sum += next;
        if (Shared::fabs(next) < 1e-17 * Shared::fabs(sum)) {
            break;
        }
    }
    return sum * 2.0 / gSqrtPi;
}

} // namespace

extern "C" {

// from LLVM libc

double acos(double x) {
    return Shared::acos(x);
}

float acosf(float x) {
    return Shared::acosf(x);
}

long double acosl(long double x) {
    return Shared::acos(static_cast<double>(x));
}

double asin(double x) {
    return Shared::asin(x);
}

float asinf(float x) {
    return Shared::asinf(x);
}

long double asinl(long double x) {
    return Shared::asin(static_cast<double>(x));
}

double atan(double x) {
    return Shared::atan(x);
}

float atanf(float x) {
    return Shared::atanf(x);
}

long double atanl(long double x) {
    return Shared::atan(static_cast<double>(x));
}

double atan2(double x, double y) {
    return Shared::atan2(x, y);
}

float atan2f(float x, float y) {
    return Shared::atan2f(x, y);
}

long double atan2l(long double x, long double y) {
    return Shared::atan2(static_cast<double>(x), static_cast<double>(y));
}

double cos(double x) {
    return Shared::cos(x);
}

float cosf(float x) {
    return Shared::cosf(x);
}

long double cosl(long double x) {
    return Shared::cos(static_cast<double>(x));
}

double sin(double x) {
    return Shared::sin(x);
}

float sinf(float x) {
    return Shared::sinf(x);
}

long double sinl(long double x) {
    return Shared::sin(static_cast<double>(x));
}

double tan(double x) {
    return Shared::tan(x);
}

float tanf(float x) {
    return Shared::tanf(x);
}

long double tanl(long double x) {
    return Shared::tan(static_cast<double>(x));
}

double exp(double x) {
    return Shared::exp(x);
}

float expf(float x) {
    return Shared::expf(x);
}

long double expl(long double x) {
    return Shared::exp(static_cast<double>(x));
}

double exp2(double x) {
    return Shared::exp2(x);
}

float exp2f(float x) {
    return Shared::exp2f(x);
}

long double exp2l(long double x) {
    return Shared::exp2(static_cast<double>(x));
}

double expm1(double x) {
    return Shared::expm1(x);
}

float expm1f(float x) {
    return Shared::expm1f(x);
}

long double expm1l(long double x) {
    return Shared::expm1(static_cast<double>(x));
}

double log(double x) {
    return Shared::log(x);
}

float logf(float x) {
    return Shared::logf(x);
}

long double logl(long double x) {
    return Shared::log(static_cast<double>(x));
}

double log10(double x) {
    return Shared::log10(x);
}

float log10f(float x) {
    return Shared::log10f(x);
}

long double log10l(long double x) {
    return Shared::log10(static_cast<double>(x));
}

double log1p(double x) {
    return Shared::log1p(x);
}

float log1pf(float x) {
    return Shared::log1pf(x);
}

long double log1pl(long double x) {
    return Shared::log1p(static_cast<double>(x));
}

double log2(double x) {
    return Shared::log2(x);
}

float log2f(float x) {
    return Shared::log2f(x);
}

long double log2l(long double x) {
    return Shared::log2(static_cast<double>(x));
}

double logb(double x) {
    return Shared::logb(x);
}

float logbf(float x) {
    return Shared::logbf(x);
}

long double logbl(long double x) {
    return Shared::logb(static_cast<double>(x));
}

double cbrt(double x) {
    return Shared::cbrt(x);
}

float cbrtf(float x) {
    return Shared::cbrtf(x);
}

long double cbrtl(long double x) {
    return Shared::cbrt(static_cast<double>(x));
}

double fabs(double x) {
    return Shared::fabs(x);
}

float fabsf(float x) {
    return Shared::fabsf(x);
}

long double fabsl(long double x) {
    return Shared::fabs(static_cast<double>(x));
}

double hypot(double x, double y) {
    return Shared::hypot(x, y);
}

float hypotf(float x, float y) {
    return Shared::hypotf(x, y);
}

long double hypotl(long double x, long double y) {
    return Shared::hypot(static_cast<double>(x), static_cast<double>(y));
}

double pow(double x, double y) {
    return Shared::pow(x, y);
}

float powf(float x, float y) {
    return Shared::powf(x, y);
}

long double powl(long double x, long double y) {
    return Shared::pow(static_cast<double>(x), static_cast<double>(y));
}

double sqrt(double x) {
    return Shared::sqrt(x);
}

float sqrtf(float x) {
    return Shared::sqrtf(x);
}

long double sqrtl(long double x) {
    return Shared::sqrt(static_cast<double>(x));
}

double ceil(double x) {
    return Shared::ceil(x);
}

float ceilf(float x) {
    return Shared::ceilf(x);
}

long double ceill(long double x) {
    return Shared::ceil(static_cast<double>(x));
}

double floor(double x) {
    return Shared::floor(x);
}

float floorf(float x) {
    return Shared::floorf(x);
}

long double floorl(long double x) {
    return Shared::floor(static_cast<double>(x));
}

double nearbyint(double x) {
    return Shared::nearbyint(x);
}

float nearbyintf(float x) {
    return Shared::nearbyintf(x);
}

long double nearbyintl(long double x) {
    return Shared::nearbyint(static_cast<double>(x));
}

double rint(double x) {
    return Shared::rint(x);
}

float rintf(float x) {
    return Shared::rintf(x);
}

long double rintl(long double x) {
    return Shared::rint(static_cast<double>(x));
}

double round(double x) {
    return Shared::round(x);
}

float roundf(float x) {
    return Shared::roundf(x);
}

long double roundl(long double x) {
    return Shared::round(static_cast<double>(x));
}

double trunc(double x) {
    return Shared::trunc(x);
}

float truncf(float x) {
    return Shared::truncf(x);
}

long double truncl(long double x) {
    return Shared::trunc(static_cast<double>(x));
}

double fmod(double x, double y) {
    return Shared::fmod(x, y);
}

float fmodf(float x, float y) {
    return Shared::fmodf(x, y);
}

long double fmodl(long double x, long double y) {
    return Shared::fmod(static_cast<double>(x), static_cast<double>(y));
}

double remainder(double x, double y) {
    return Shared::remainder(x, y);
}

float remainderf(float x, float y) {
    return Shared::remainderf(x, y);
}

long double remainderl(long double x, long double y) {
    return Shared::remainder(static_cast<double>(x), static_cast<double>(y));
}

double copysign(double x, double y) {
    return Shared::copysign(x, y);
}

float copysignf(float x, float y) {
    return Shared::copysignf(x, y);
}

long double copysignl(long double x, long double y) {
    return Shared::copysign(static_cast<double>(x), static_cast<double>(y));
}

double nextafter(double x, double y) {
    return Shared::nextafter(x, y);
}

float nextafterf(float x, float y) {
    return Shared::nextafterf(x, y);
}

long double nextafterl(long double x, long double y) {
    return Shared::nextafter(static_cast<double>(x), static_cast<double>(y));
}

double fdim(double x, double y) {
    return Shared::fdim(x, y);
}

float fdimf(float x, float y) {
    return Shared::fdimf(x, y);
}

long double fdiml(long double x, long double y) {
    return Shared::fdim(static_cast<double>(x), static_cast<double>(y));
}

double fmax(double x, double y) {
    return Shared::fmax(x, y);
}

float fmaxf(float x, float y) {
    return Shared::fmaxf(x, y);
}

long double fmaxl(long double x, long double y) {
    return Shared::fmax(static_cast<double>(x), static_cast<double>(y));
}

double fmin(double x, double y) {
    return Shared::fmin(x, y);
}

float fminf(float x, float y) {
    return Shared::fminf(x, y);
}

long double fminl(long double x, long double y) {
    return Shared::fmin(static_cast<double>(x), static_cast<double>(y));
}

double fma(double x, double y, double z) {
    return Shared::fma(x, y, z);
}

float fmaf(float x, float y, float z) {
    return Shared::fmaf(x, y, z);
}

long double fmal(long double x, long double y, long double z) {
    return Shared::fma(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
}

double frexp(double x, int* out_exponent) {
    return Shared::frexp(x, out_exponent);
}

float frexpf(float x, int* out_exponent) {
    return Shared::frexpf(x, out_exponent);
}

long double frexpl(long double x, int* out_exponent) {
    return Shared::frexp(static_cast<double>(x), out_exponent);
}

double ldexp(double x, int exponent) {
    return Shared::ldexp(x, exponent);
}

float ldexpf(float x, int exponent) {
    return Shared::ldexpf(x, exponent);
}

long double ldexpl(long double x, int exponent) {
    return Shared::ldexp(static_cast<double>(x), exponent);
}

double scalbn(double x, int exponent) {
    return Shared::scalbn(x, exponent);
}

float scalbnf(float x, int exponent) {
    return Shared::scalbnf(x, exponent);
}

long double scalbnl(long double x, int exponent) {
    return Shared::scalbn(static_cast<double>(x), exponent);
}

double scalbln(double x, long exponent) {
    return Shared::scalbln(x, exponent);
}

float scalblnf(float x, long exponent) {
    return Shared::scalblnf(x, exponent);
}

long double scalblnl(long double x, long exponent) {
    return Shared::scalbln(static_cast<double>(x), exponent);
}

int ilogb(double x) {
    return Shared::ilogb(x);
}

int ilogbf(float x) {
    return Shared::ilogbf(x);
}

int ilogbl(long double x) {
    return Shared::ilogb(static_cast<double>(x));
}

double modf(double x, double* out_integer) {
    return Shared::modf(x, out_integer);
}

float modff(float x, float* out_integer) {
    return Shared::modff(x, out_integer);
}

long double modfl(long double x, long double* out_integer) {
    double integer;
    double fraction = Shared::modf(static_cast<double>(x), &integer);

    *out_integer = integer;
    return fraction;
}

double remquo(double x, double y, int* out_quotient) {
    return Shared::remquo(x, y, out_quotient);
}

float remquof(float x, float y, int* out_quotient) {
    return Shared::remquof(x, y, out_quotient);
}

long double remquol(long double x, long double y, int* out_quotient) {
    return Shared::remquo(static_cast<double>(x), static_cast<double>(y), out_quotient);
}

long lrint(double x) {
    return Shared::lrint(x);
}

long lrintf(float x) {
    return Shared::lrintf(x);
}

long lrintl(long double x) {
    return Shared::lrint(static_cast<double>(x));
}

long long llrint(double x) {
    return Shared::llrint(x);
}

long long llrintf(float x) {
    return Shared::llrintf(x);
}

long long llrintl(long double x) {
    return Shared::llrint(static_cast<double>(x));
}

long lround(double x) {
    return Shared::lround(x);
}

long lroundf(float x) {
    return Shared::lroundf(x);
}

long lroundl(long double x) {
    return Shared::lround(static_cast<double>(x));
}

long long llround(double x) {
    return Shared::llround(x);
}

long long llroundf(float x) {
    return Shared::llroundf(x);
}

long long llroundl(long double x) {
    return Shared::llround(static_cast<double>(x));
}

double nan(const char* payload) {
    return Shared::nan(payload);
}

float nanf(const char* payload) {
    return Shared::nanf(payload);
}

long double nanl(const char* payload) {
    return Shared::nan(payload);
}

double nexttoward(double x, long double y) {
    return Shared::nextafter(x, static_cast<double>(y));
}

float nexttowardf(float x, long double y) {
    return Shared::nextafterf(x, static_cast<float>(y));
}

long double nexttowardl(long double x, long double y) {
    return Shared::nextafter(static_cast<double>(x), static_cast<double>(y));
}

// built here

double sinh(double x) {
    double magnitude = Shared::fabs(x);
    double half = x < 0 ? -0.5 : 0.5;
    double root;

    if (magnitude < 1e-8) {
        return x;
    }

    if (magnitude < gLogMaxDouble) {
        double t = Shared::expm1(magnitude);

        if (magnitude < 1.0) {
            return half * (2.0 * t - t * t / (t + 1.0));
        }
        return half * (t + t / (t + 1.0));
    }

    // exp(|x|) overflows before sinh does: exp(|x| / 2)^2 / 2
    root = Shared::exp(0.5 * magnitude);
    return half * root * root;
}

double cosh(double x) {
    double magnitude = Shared::fabs(x);
    double root;

    if (magnitude < gLn2) {
        double t = Shared::expm1(magnitude);

        return 1.0 + t * t / (2.0 * (1.0 + t));
    }

    if (magnitude < gLogMaxDouble) {
        double t = Shared::exp(magnitude);

        return 0.5 * (t + 1.0 / t);
    }

    root = Shared::exp(0.5 * magnitude);
    return 0.5 * root * root;
}

double tanh(double x) {
    double magnitude = Shared::fabs(x);
    double result;

    if (magnitude > 22.0) {
        result = 1.0;
    }
    else if (magnitude < 1e-8) {
        return x;
    }
    else {
        double t = Shared::expm1(2.0 * magnitude);

        result = t / (t + 2.0);
    }

    return x < 0 ? -result : result;
}

double asinh(double x) {
    double magnitude = Shared::fabs(x);
    double result;

    if (magnitude > 1e8) {
        result = Shared::log(magnitude) + gLn2;
    }
    else if (magnitude < 1e-8) {
        return x;
    }
    else {
        double square = magnitude * magnitude;

        result = Shared::log1p(magnitude + square / (1.0 + Shared::sqrt(1.0 + square)));
    }

    return x < 0 ? -result : result;
}

double acosh(double x) {
    double t;

    if (x < 1.0) {
        errno = EDOM;
        return Shared::nan("");
    }

    if (x > 1e8) {
        return Shared::log(x) + gLn2;
    }

    t = x - 1.0;
    return Shared::log1p(t + Shared::sqrt(2.0 * t + t * t));
}

double atanh(double x) {
    double magnitude = Shared::fabs(x);
    double result;

    if (magnitude > 1.0) {
        errno = EDOM;
        return Shared::nan("");
    }

    if (magnitude == 1.0) {
        errno = ERANGE;
        return x < 0 ? -__builtin_huge_val() : __builtin_huge_val();
    }

    if (magnitude < 1e-8) {
        return x;
    }

    result = 0.5 * Shared::log1p(2.0 * magnitude / (1.0 - magnitude));
    return x < 0 ? -result : result;
}

double erf(double x) {
    double magnitude;
    double result;

    if (x != x) {
        return x;
    }

    magnitude = Shared::fabs(x);
    if (magnitude < 0.5) {
        return Erf_Series(x);
    }

    if (magnitude > 6.0) {
        return x < 0 ? -1.0 : 1.0;
    }

    result = 1.0 - Erfc_Continued_Fraction(magnitude);
    return x < 0 ? -result : result;
}

double erfc(double x) {
    if (x != x) {
        return x;
    }

    if (x < 0.5) {
        return 1.0 - erf(x);
    }

    if (x > 27.3) {
        return 0.0;
    }

    return Erfc_Continued_Fraction(x);
}

double lgamma(double x) {
    if (x != x || __builtin_isinf(x)) {
        return __builtin_huge_val();
    }

    if (x <= 0 && Shared::floor(x) == x) {
        errno = ERANGE;
        return __builtin_huge_val();
    }

    return Log_Gamma_Positive(x);
}

double tgamma(double x) {
    double magnitude;

    if (x != x) {
        return x;
    }

    if (x == 0.0) {
        errno = ERANGE;
        return __builtin_signbit(x) ? -__builtin_huge_val() : __builtin_huge_val();
    }

    if (x < 0 && Shared::floor(x) == x) {
        errno = EDOM;
        return Shared::nan("");
    }
    if (x > 171.7) {
        errno = ERANGE;
        return __builtin_huge_val();
    }

    // Small positive integers exactly
    if (x > 0 && x < 23 && Shared::floor(x) == x) {
        double product = 1.0;

        for (int factor = 2; factor < static_cast<int>(x); factor++) {
            product *= factor;
        }
        return product;
    }

    magnitude = Shared::exp(Log_Gamma_Positive(x));
    // Gamma is negative on (-1, 0), (-3, -2), etc
    if (x < 0 && static_cast<long long>(Shared::floor(x)) % 2 != 0) {
        return -magnitude;
    }
    return magnitude;
}

float sinhf(float x) {
    return Shared::sinhf(x);
}

float coshf(float x) {
    return Shared::coshf(x);
}

float tanhf(float x) {
    return Shared::tanhf(x);
}

float asinhf(float x) {
    return Shared::asinhf(x);
}

float acoshf(float x) {
    return Shared::acoshf(x);
}

float atanhf(float x) {
    return Shared::atanhf(x);
}

float erff(float x) {
    return Shared::erff(x);
}

long double sinhl(long double x) {
    return sinh(static_cast<double>(x));
}

long double coshl(long double x) {
    return cosh(static_cast<double>(x));
}

long double tanhl(long double x) {
    return tanh(static_cast<double>(x));
}

long double asinhl(long double x) {
    return asinh(static_cast<double>(x));
}

long double acoshl(long double x) {
    return acosh(static_cast<double>(x));
}

long double atanhl(long double x) {
    return atanh(static_cast<double>(x));
}

long double erfl(long double x) {
    return erf(static_cast<double>(x));
}

float erfcf(float x) {
    return static_cast<float>(erfc(x));
}

long double erfcl(long double x) {
    return erfc(static_cast<double>(x));
}

float lgammaf(float x) {
    return static_cast<float>(lgamma(x));
}

long double lgammal(long double x) {
    return lgamma(static_cast<double>(x));
}

float tgammaf(float x) {
    return static_cast<float>(tgamma(x));
}

long double tgammal(long double x) {
    return tgamma(static_cast<double>(x));
}

} // extern "C"
