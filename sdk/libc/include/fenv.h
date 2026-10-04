// WebAssembly has no floating-point exception flags or rounding modes: only FE_TONEAREST
#ifndef MODLOADER_LIBC_FENV_H
#define MODLOADER_LIBC_FENV_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int fenv_t;
typedef unsigned int fexcept_t;

#define FE_INVALID 1
#define FE_DIVBYZERO 2
#define FE_OVERFLOW 4
#define FE_UNDERFLOW 8
#define FE_INEXACT 16
#define FE_ALL_EXCEPT 31
#define FE_TONEAREST 0
#define FE_DOWNWARD 1
#define FE_UPWARD 2
#define FE_TOWARDZERO 3
#define FE_DFL_ENV ((const fenv_t*)0)

int feclearexcept(int exceptions);
int fegetexceptflag(fexcept_t* out_flags, int exceptions);
int feraiseexcept(int exceptions);
int fesetexceptflag(const fexcept_t* flags, int exceptions);
int fetestexcept(int exceptions);
int fegetround(void);
int fesetround(int mode);
int fegetenv(fenv_t* out_environment);
int feholdexcept(fenv_t* out_environment);
int fesetenv(const fenv_t* environment);
int feupdateenv(const fenv_t* environment);

#ifdef __cplusplus
}
#endif

#endif
