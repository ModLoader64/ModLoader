#pragma once

#include <stdio.h>

extern "C" {

FILE* __modloader_fopen(const char* folder, const char* path, const char* mode);
FILE* __modloader_freopen(const char* folder, const char* path, const char* mode, FILE* stream);
int __modloader_remove(const char* folder, const char* path);
int __modloader_rename(const char* folder, const char* from, const char* to);
FILE* __modloader_tmpfile(const char* folder);
FILE* __modloader_standard_stream(const char* source, size_t source_length, int descriptor);
void __modloader_perror(FILE* stream, const char* prefix);

}
