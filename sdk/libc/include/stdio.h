#ifndef MODLOADER_LIBC_STDIO_H
#define MODLOADER_LIBC_STDIO_H

#include <stdarg.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ModLoader_File FILE;
typedef long long fpos_t;

#define EOF (-1)
#define BUFSIZ 4096
#define FILENAME_MAX 1024
#define FOPEN_MAX 256
#define L_tmpnam 32
#define TMP_MAX 0
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

FILE* __modloader_stdin(void);
FILE* __modloader_stdout(void);
FILE* __modloader_stderr(void);
#define stdin (__modloader_stdin())
#define stdout (__modloader_stdout())
#define stderr (__modloader_stderr())

int printf(const char* __restrict format, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE* __restrict stream, const char* __restrict format, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char* __restrict buffer, const char* __restrict format, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char* __restrict buffer, size_t size, const char* __restrict format, ...) __attribute__((format(printf, 3, 4)));
int asprintf(char** __restrict out_text, const char* __restrict format, ...) __attribute__((format(printf, 2, 3)));
int vprintf(const char* __restrict format, va_list arguments);
int vfprintf(FILE* __restrict stream, const char* __restrict format, va_list arguments);
int vsprintf(char* __restrict buffer, const char* __restrict format, va_list arguments);
int vsnprintf(char* __restrict buffer, size_t size, const char* __restrict format, va_list arguments);
int vasprintf(char** __restrict out_text, const char* __restrict format, va_list arguments);

int sscanf(const char* __restrict text, const char* __restrict format, ...);
int vsscanf(const char* __restrict text, const char* __restrict format, va_list arguments);
int fscanf(FILE* __restrict stream, const char* __restrict format, ...);
int vfscanf(FILE* __restrict stream, const char* __restrict format, va_list arguments);
int scanf(const char* __restrict format, ...);
int vscanf(const char* __restrict format, va_list arguments);

int puts(const char* text);
int fputs(const char* __restrict text, FILE* __restrict stream);
int putchar(int character);
int fputc(int character, FILE* stream);
int putc(int character, FILE* stream);
int getchar(void);
int fgetc(FILE* stream);
int getc(FILE* stream);
int ungetc(int character, FILE* stream);
char* fgets(char* __restrict buffer, int size, FILE* __restrict stream);
void perror(const char* prefix);

FILE* fopen(const char* __restrict path, const char* __restrict mode);
FILE* freopen(const char* __restrict path, const char* __restrict mode, FILE* __restrict stream);
int fclose(FILE* stream);
size_t fread(void* __restrict buffer, size_t size, size_t count, FILE* __restrict stream);
size_t fwrite(const void* __restrict data, size_t size, size_t count, FILE* __restrict stream);
int fseek(FILE* stream, long offset, int origin);
int fseeko(FILE* stream, off_t offset, int origin);
long ftell(FILE* stream);
off_t ftello(FILE* stream);
void rewind(FILE* stream);
int fgetpos(FILE* __restrict stream, fpos_t* __restrict out_position);
int fsetpos(FILE* stream, const fpos_t* position);
int feof(FILE* stream);
int ferror(FILE* stream);
void clearerr(FILE* stream);
int fflush(FILE* stream);
int setvbuf(FILE* __restrict stream, char* __restrict buffer, int mode, size_t size);
void setbuf(FILE* __restrict stream, char* __restrict buffer);
int fileno(FILE* stream); // File stream IDs are not usable with read()/write()/close()
int remove(const char* path);
int rename(const char* from, const char* to);
FILE* tmpfile(void); // Creates tmp/<number>.tmp in the mounted folder; closing does not delete it
char* tmpnam(char* name);

#ifdef __cplusplus
}
#endif

#endif
