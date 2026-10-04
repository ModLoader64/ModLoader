// Include before libultra/decompilation headers, and use #pragma modloader use_space(cpu)
// Declarations use MIPS O32 layout
// #pragma modloader use_space(host) after
#pragma once

#include <modloader/platforms/n64/spaces.h>

// Compiler definitions expected by libultra/decompilation headers
#ifndef _LANGUAGE_C
#define _LANGUAGE_C 1
#endif
#ifndef _LANGUAGE_C_PLUS_PLUS
#define _LANGUAGE_C_PLUS_PLUS 1
#endif
#ifndef _MIPS_SZINT
#define _MIPS_SZINT 32
#endif
#ifndef _MIPS_SZLONG
#define _MIPS_SZLONG 32
#endif
#ifndef _MIPS_SZPTR
#define _MIPS_SZPTR 32
#endif

// Keep module size_t; suppress libultra's 32-bit typedef
#ifndef _SIZE_T
#define _SIZE_T
#endif
#ifndef _SIZE_T_DEF
#define _SIZE_T_DEF
#endif
