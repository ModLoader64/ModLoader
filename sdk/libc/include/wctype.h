#ifndef MODLOADER_LIBC_WCTYPE_H
#define MODLOADER_LIBC_WCTYPE_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef const int* wctrans_t;

int iswalnum(wint_t character);
int iswalpha(wint_t character);
int iswblank(wint_t character);
int iswcntrl(wint_t character);
int iswdigit(wint_t character);
int iswgraph(wint_t character);
int iswlower(wint_t character);
int iswprint(wint_t character);
int iswpunct(wint_t character);
int iswspace(wint_t character);
int iswupper(wint_t character);
int iswxdigit(wint_t character);
wint_t towlower(wint_t character);
wint_t towupper(wint_t character);
wctype_t wctype(const char* name);
int iswctype(wint_t character, wctype_t type);

#ifdef __cplusplus
}
#endif

#endif
