// Mostly LLVM libc's correctly rounded functions; long double is IEEE quad on wasm64 and computes in double
#ifndef MODLOADER_LIBC_MATH_H
#define MODLOADER_LIBC_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef float float_t;
typedef double double_t;

#define HUGE_VAL __builtin_huge_val()
#define HUGE_VALF __builtin_huge_valf()
#define HUGE_VALL __builtin_huge_vall()
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))

#define FP_NAN 0
#define FP_INFINITE 1
#define FP_ZERO 2
#define FP_SUBNORMAL 3
#define FP_NORMAL 4
#define FP_ILOGB0 (-2147483647 - 1)
#define FP_ILOGBNAN (-2147483647 - 1)
#define MATH_ERRNO 1
#define MATH_ERREXCEPT 2
#define math_errhandling MATH_ERRNO

#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isfinite(x) __builtin_isfinite(x)
#define isinf(x) __builtin_isinf(x)
#define isnan(x) __builtin_isnan(x)
#define isnormal(x) __builtin_isnormal(x)
#define signbit(x) __builtin_signbit(x)
#define isgreater(x, y) __builtin_isgreater(x, y)
#define isgreaterequal(x, y) __builtin_isgreaterequal(x, y)
#define isless(x, y) __builtin_isless(x, y)
#define islessequal(x, y) __builtin_islessequal(x, y)
#define islessgreater(x, y) __builtin_islessgreater(x, y)
#define isunordered(x, y) __builtin_isunordered(x, y)

#define M_E 2.7182818284590452354
#define M_LOG2E 1.4426950408889634074
#define M_LOG10E 0.43429448190325182765
#define M_LN2 0.69314718055994530942
#define M_LN10 2.30258509299404568402
#define M_PI 3.14159265358979323846
#define M_PI_2 1.57079632679489661923
#define M_PI_4 0.78539816339744830962
#define M_1_PI 0.31830988618379067154
#define M_2_PI 0.63661977236758134308
#define M_2_SQRTPI 1.12837916709551257390
#define M_SQRT2 1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440

#define MODLOADER_MATH_FUNCTION_1(name) \
    double name(double x); \
    float name##f(float x); \
    long double name##l(long double x);
#define MODLOADER_MATH_FUNCTION_2(name) \
    double name(double x, double y); \
    float name##f(float x, float y); \
    long double name##l(long double x, long double y);

MODLOADER_MATH_FUNCTION_1(acos)
MODLOADER_MATH_FUNCTION_1(asin)
MODLOADER_MATH_FUNCTION_1(atan)
MODLOADER_MATH_FUNCTION_2(atan2)
MODLOADER_MATH_FUNCTION_1(cos)
MODLOADER_MATH_FUNCTION_1(sin)
MODLOADER_MATH_FUNCTION_1(tan)
MODLOADER_MATH_FUNCTION_1(acosh)
MODLOADER_MATH_FUNCTION_1(asinh)
MODLOADER_MATH_FUNCTION_1(atanh)
MODLOADER_MATH_FUNCTION_1(cosh)
MODLOADER_MATH_FUNCTION_1(sinh)
MODLOADER_MATH_FUNCTION_1(tanh)
MODLOADER_MATH_FUNCTION_1(exp)
MODLOADER_MATH_FUNCTION_1(exp2)
MODLOADER_MATH_FUNCTION_1(expm1)
MODLOADER_MATH_FUNCTION_1(log)
MODLOADER_MATH_FUNCTION_1(log10)
MODLOADER_MATH_FUNCTION_1(log1p)
MODLOADER_MATH_FUNCTION_1(log2)
MODLOADER_MATH_FUNCTION_1(logb)
MODLOADER_MATH_FUNCTION_1(cbrt)
MODLOADER_MATH_FUNCTION_1(fabs)
MODLOADER_MATH_FUNCTION_2(hypot)
MODLOADER_MATH_FUNCTION_2(pow)
MODLOADER_MATH_FUNCTION_1(sqrt)
MODLOADER_MATH_FUNCTION_1(erf)
MODLOADER_MATH_FUNCTION_1(erfc)
MODLOADER_MATH_FUNCTION_1(lgamma)
MODLOADER_MATH_FUNCTION_1(tgamma)
MODLOADER_MATH_FUNCTION_1(ceil)
MODLOADER_MATH_FUNCTION_1(floor)
MODLOADER_MATH_FUNCTION_1(nearbyint)
MODLOADER_MATH_FUNCTION_1(rint)
MODLOADER_MATH_FUNCTION_1(round)
MODLOADER_MATH_FUNCTION_1(trunc)
MODLOADER_MATH_FUNCTION_2(fmod)
MODLOADER_MATH_FUNCTION_2(remainder)
MODLOADER_MATH_FUNCTION_2(copysign)
MODLOADER_MATH_FUNCTION_2(nextafter)
MODLOADER_MATH_FUNCTION_2(fdim)
MODLOADER_MATH_FUNCTION_2(fmax)
MODLOADER_MATH_FUNCTION_2(fmin)

#undef MODLOADER_MATH_FUNCTION_1
#undef MODLOADER_MATH_FUNCTION_2

double fma(double x, double y, double z);
float fmaf(float x, float y, float z);
long double fmal(long double x, long double y, long double z);
double frexp(double x, int* out_exponent);
float frexpf(float x, int* out_exponent);
long double frexpl(long double x, int* out_exponent);
double ldexp(double x, int exponent);
float ldexpf(float x, int exponent);
long double ldexpl(long double x, int exponent);
double scalbn(double x, int exponent);
float scalbnf(float x, int exponent);
long double scalbnl(long double x, int exponent);
double scalbln(double x, long exponent);
float scalblnf(float x, long exponent);
long double scalblnl(long double x, long exponent);
int ilogb(double x);
int ilogbf(float x);
int ilogbl(long double x);
double modf(double x, double* out_integer);
float modff(float x, float* out_integer);
long double modfl(long double x, long double* out_integer);
double remquo(double x, double y, int* out_quotient);
float remquof(float x, float y, int* out_quotient);
long double remquol(long double x, long double y, int* out_quotient);
long lrint(double x);
long lrintf(float x);
long lrintl(long double x);
long long llrint(double x);
long long llrintf(float x);
long long llrintl(long double x);
long lround(double x);
long lroundf(float x);
long lroundl(long double x);
long long llround(double x);
long long llroundf(float x);
long long llroundl(long double x);
double nan(const char* payload);
float nanf(const char* payload);
long double nanl(const char* payload);
double nexttoward(double x, long double y);
float nexttowardf(float x, long double y);
long double nexttowardl(long double x, long double y);

#ifdef __cplusplus
}
#endif

#endif
