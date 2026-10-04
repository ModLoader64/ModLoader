#ifndef MODLOADER_LIBC_CTYPE_H
#define MODLOADER_LIBC_CTYPE_H

#ifdef __cplusplus
extern "C" {
#endif

int isalnum(int character);
int isalpha(int character);
int isblank(int character);
int iscntrl(int character);
int isdigit(int character);
int isgraph(int character);
int islower(int character);
int isprint(int character);
int ispunct(int character);
int isspace(int character);
int isupper(int character);
int isxdigit(int character);
int isascii(int character);
int tolower(int character);
int toupper(int character);
int toascii(int character);

#ifdef __cplusplus
}
#endif

#endif
