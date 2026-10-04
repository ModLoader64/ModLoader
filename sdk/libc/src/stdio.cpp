#include "internal.h"
#include <modloader/detail/libc.h>

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr size_t gReadBufferSize = 4096;
constexpr size_t gLineSize = 1024;

// modloader/io/file.h
constexpr uint32_t gModeRead = 0;
constexpr uint32_t gModeWrite = 1;
constexpr uint32_t gModeReadWrite = 3;

enum Stream_Kind : uint32_t {
    Stream_File,
    Stream_Stdin,
    Stream_Stdout,
    Stream_Stderr,
};

} // namespace

struct ModLoader_File {
    Stream_Kind kind = Stream_File;
    uint32_t handle = 0;
    uint64_t position = 0;
    bool readable = false;
    bool writable = false;
    bool append = false;
    bool atEnd = false;
    bool failed = false;
    int unget = -1; // -1: none
    unsigned char* readBuffer = nullptr; // the file's bytes from readStart
    uint64_t readStart = 0;
    uint32_t readLength = 0;
    char line[gLineSize] = {}; // stdout and stderr: the line so far
    uint32_t lineLength = 0;
    Libc::Spin_Lock lock = {};
    const char* source = "modloader-runtime";
    size_t sourceLength = sizeof("modloader-runtime") - 1;
    ModLoader_File* nextLog = nullptr;
};

namespace {

ModLoader_File sStdin = { .kind = Stream_Stdin, .readable = true, .atEnd = true };
ModLoader_File sStderr = { .kind = Stream_Stderr, .writable = true };
ModLoader_File sStdout = { .kind = Stream_Stdout, .writable = true, .nextLog = &sStderr };
ModLoader_File* sLogStreams = &sStdout;
Libc::Spin_Lock sLogStreamsLock;
uint32_t sTemporaryCount;

void Flush_Line(FILE* stream) {
    if (stream->lineLength != 0) {
        ModLoader_Host_Log_Source(stream->kind == Stream_Stderr ? Libc::gLogError : Libc::gLogInfo,
            stream->source, stream->sourceLength, stream->line, stream->lineLength);
        stream->lineLength = 0;
    }
}

// Under the stream's lock
size_t Write_Locked(FILE* stream, const void* data, size_t size) {
    int64_t written;

    if (!stream->writable) {
        stream->failed = true;
        errno = EBADF;
        return 0;
    }

    if (stream->kind != Stream_File) {
        const char* text = static_cast<const char*>(data);

        for (size_t index = 0; index < size; index++) {
            if (text[index] == '\n') {
                Flush_Line(stream);
                continue;
            }

            if (stream->lineLength == gLineSize) {
                Flush_Line(stream);
            }
            stream->line[stream->lineLength++] = text[index];
        }
        return size;
    }

    stream->readLength = 0;
    stream->unget = -1;
    if (stream->append) {
        int64_t file_size = ModLoader_Host_File_Size(stream->handle);

        stream->position = file_size > 0 ? static_cast<uint64_t>(file_size) : 0;
    }

    written = ModLoader_Host_File_Write(stream->handle, data, size, stream->position);
    if (written < 0) {
        stream->failed = true;
        errno = EIO;
        return 0;
    }
    stream->position += static_cast<uint64_t>(written);
    return static_cast<size_t>(written);
}

// Under the stream's lock
size_t Read_Locked(FILE* stream, void* buffer, size_t size) {
    unsigned char* out;
    size_t done = 0;
    int64_t read;

    if (!stream->readable) {
        stream->failed = true;
        errno = EBADF;
        return 0;
    }

    out = static_cast<unsigned char*>(buffer);
    if (stream->unget >= 0 && size > 0) {
        out[done++] = static_cast<unsigned char>(stream->unget);
        stream->unget = -1;
    }

    if (stream->kind != Stream_File) {
        stream->atEnd = done < size;
        return done;
    }

    while (done < size) {
        // From the buffer, when it holds the position
        if (stream->readBuffer != nullptr && stream->position >= stream->readStart && stream->position < stream->readStart + stream->readLength) {
            size_t offset = static_cast<size_t>(stream->position - stream->readStart);
            size_t available = stream->readLength - offset;
            size_t chunk = size - done < available ? size - done : available;

            memcpy(out + done, stream->readBuffer + offset, chunk);
            done += chunk;
            stream->position += chunk;
            continue;
        }

        if (size - done >= gReadBufferSize) {
            read = ModLoader_Host_File_Read(stream->handle, out + done, size - done, stream->position);
            if (read <= 0) {
                if (read < 0) {
                    stream->failed = true;
                }
                break;
            }
            done += static_cast<size_t>(read);
            stream->position += static_cast<uint64_t>(read);
            continue;
        }

        if (stream->readBuffer == nullptr) {
            stream->readBuffer = static_cast<unsigned char*>(malloc(gReadBufferSize));
            if (stream->readBuffer == nullptr) {
                stream->failed = true;
                break;
            }
        }

        read = ModLoader_Host_File_Read(stream->handle, stream->readBuffer, gReadBufferSize, stream->position);
        if (read <= 0) {
            if (read < 0) {
                stream->failed = true;
            }
            stream->readLength = 0;
            break;
        }
        stream->readStart = stream->position;
        stream->readLength = static_cast<uint32_t>(read);
    }

    if (done < size && !stream->failed) {
        stream->atEnd = true;
    }
    return done;
}

} // namespace

extern "C" {

FILE* __modloader_stdin(void) {
    return &sStdin;
}

FILE* __modloader_stdout(void) {
    return &sStdout;
}

FILE* __modloader_stderr(void) {
    return &sStderr;
}

FILE* __modloader_standard_stream(const char* source, size_t source_length, int descriptor) {
    FILE* stream = static_cast<FILE*>(calloc(1, sizeof(FILE)));
    if (stream == nullptr) {
        Libc::Fatal("standard stream allocation failed");
    }

    stream->kind = static_cast<Stream_Kind>(Stream_Stdin + descriptor);
    stream->readable = descriptor == 0;
    stream->writable = descriptor != 0;
    stream->atEnd = descriptor == 0;
    stream->unget = -1;
    stream->source = source;
    stream->sourceLength = source_length;
    if (stream->writable) {
        Libc::Scoped lock(sLogStreamsLock);
        stream->nextLog = sLogStreams;
        sLogStreams = stream;
    }
    return stream;
}

FILE* __modloader_fopen(const char* folder, const char* path, const char* mode) {
    bool plus = strchr(mode, '+') != nullptr;
    bool exclusive = strchr(mode, 'x') != nullptr;
    uint32_t host_mode;
    size_t path_length = strlen(path);
    size_t folder_length = strlen(folder);
    uint32_t handle = 0;
    FILE* stream;

    if (mode[0] == 'r') {
        handle = ModLoader_Host_File_Open(folder, folder_length, path, path_length, gModeRead);
        if (handle == 0) {
            errno = ENOENT;
            return nullptr;
        }

        host_mode = plus ? gModeReadWrite : gModeRead;
        if (plus) {
            ModLoader_Host_File_Close(handle);
            handle = 0;
        }
    }
    else if (mode[0] == 'w') {
        if (exclusive) {
            uint32_t probe = ModLoader_Host_File_Open(folder, folder_length, path, path_length, gModeRead);

            if (probe != 0) {
                ModLoader_Host_File_Close(probe);
                errno = EEXIST;
                return nullptr;
            }
        }
        if (plus) {
            ModLoader_Host_File_Delete(folder, folder_length, path, path_length);
            host_mode = gModeReadWrite;
        }
        else {
            host_mode = gModeWrite;
        }
    }
    else if (mode[0] == 'a') {
        host_mode = gModeReadWrite;
    }
    else {
        errno = EINVAL;
        return nullptr;
    }

    if (handle == 0) {
        handle = ModLoader_Host_File_Open(folder, folder_length, path, path_length, host_mode);
    }
    if (handle == 0) {
        errno = EACCES;
        return nullptr;
    }

    stream = static_cast<FILE*>(calloc(1, sizeof(FILE)));
    if (stream == nullptr) {
        ModLoader_Host_File_Close(handle);
        errno = ENOMEM;
        return nullptr;
    }

    stream->kind = Stream_File;
    stream->handle = handle;
    stream->readable = mode[0] == 'r' || plus;
    stream->writable = mode[0] != 'r' || plus;
    stream->append = mode[0] == 'a';
    stream->unget = -1;
    return stream;
}

FILE* __modloader_freopen(const char* folder, const char* path, const char* mode, FILE* stream) {
    if (stream != nullptr && stream->kind == Stream_File) {
        fclose(stream);
    }

    return path != nullptr ? __modloader_fopen(folder, path, mode) : nullptr;
}

int fclose(FILE* stream) {
    if (stream == nullptr) {
        return EOF;
    }

    if (stream->kind != Stream_File) {
        fflush(stream);
        return 0;
    }

    ModLoader_Host_File_Close(stream->handle);
    free(stream->readBuffer);
    free(stream);
    return 0;
}

size_t fread(void* __restrict buffer, size_t size, size_t count, FILE* __restrict stream) {
    if (size == 0 || count == 0) {
        return 0;
    }

    Libc::Scoped lock(stream->lock);
    return Read_Locked(stream, buffer, size * count) / size;
}

size_t fwrite(const void* __restrict data, size_t size, size_t count, FILE* __restrict stream) {
    if (size == 0 || count == 0) {
        return 0;
    }

    Libc::Scoped lock(stream->lock);
    return Write_Locked(stream, data, size * count) / size;
}

int fseeko(FILE* stream, off_t offset, int origin) {
    int64_t base = 0;

    if (stream->kind != Stream_File) {
        errno = ESPIPE;
        return -1;
    }

    Libc::Scoped lock(stream->lock);
    if (origin == SEEK_CUR) {
        base = static_cast<int64_t>(stream->position) - (stream->unget >= 0 ? 1 : 0);
    }
    else if (origin == SEEK_END) {
        base = ModLoader_Host_File_Size(stream->handle);
        if (base < 0) {
            errno = EIO;
            return -1;
        }
    }
    else if (origin != SEEK_SET) {
        errno = EINVAL;
        return -1;
    }

    if (base + offset < 0) {
        errno = EINVAL;
        return -1;
    }
    stream->position = static_cast<uint64_t>(base + offset);
    stream->unget = -1;
    stream->atEnd = false;
    return 0;
}

int fseek(FILE* stream, long offset, int origin) {
    return fseeko(stream, offset, origin);
}

off_t ftello(FILE* stream) {
    if (stream->kind != Stream_File) {
        errno = ESPIPE;
        return -1;
    }

    Libc::Scoped lock(stream->lock);
    return static_cast<off_t>(stream->position) - (stream->unget >= 0 ? 1 : 0);
}

long ftell(FILE* stream) {
    return static_cast<long>(ftello(stream));
}

void rewind(FILE* stream) {
    fseeko(stream, 0, SEEK_SET);
    stream->failed = false;
}

int fgetpos(FILE* __restrict stream, fpos_t* __restrict out_position) {
    off_t position = ftello(stream);

    if (position < 0) {
        return -1;
    }
    *out_position = position;
    return 0;
}

int fsetpos(FILE* stream, const fpos_t* position) {
    return fseeko(stream, *position, SEEK_SET);
}

int feof(FILE* stream) {
    return stream->atEnd ? 1 : 0;
}

int ferror(FILE* stream) {
    return stream->failed ? 1 : 0;
}

void clearerr(FILE* stream) {
    stream->atEnd = false;
    stream->failed = false;
}

int fflush(FILE* stream) {
    if (stream == nullptr) {
        Libc::Scoped lock(sLogStreamsLock);
        for (FILE* output = sLogStreams; output != nullptr; output = output->nextLog) {
            fflush(output);
        }
        return 0;
    }

    if (stream->kind == Stream_Stdout || stream->kind == Stream_Stderr) {
        Libc::Scoped lock(stream->lock);

        Flush_Line(stream);
    }
    return 0;
}

int setvbuf(FILE* __restrict, char* __restrict, int, size_t) {
    return 0;
}

void setbuf(FILE* __restrict, char* __restrict) {
}

int fileno(FILE* stream) {
    switch (stream->kind) {
    case Stream_Stdin:
        return 0;
    case Stream_Stdout:
        return 1;
    case Stream_Stderr:
        return 2;
    default:
        return static_cast<int>(stream->handle) + 2;
    }
}

int fgetc(FILE* stream) {
    unsigned char character;

    return fread(&character, 1, 1, stream) == 1 ? character : EOF;
}

int getc(FILE* stream) {
    return fgetc(stream);
}

int getchar(void) {
    return EOF;
}

int ungetc(int character, FILE* stream) {
    if (character == EOF) {
        return EOF;
    }

    Libc::Scoped lock(stream->lock);
    if (stream->unget >= 0) {
        return EOF;
    }
    stream->unget = static_cast<unsigned char>(character);
    stream->atEnd = false;
    return character;
}

char* fgets(char* __restrict buffer, int size, FILE* __restrict stream) {
    int length = 0;

    if (size <= 0) {
        return nullptr;
    }

    while (length < size - 1) {
        int character = fgetc(stream);

        if (character == EOF) {
            break;
        }

        buffer[length++] = static_cast<char>(character);
        if (character == '\n') {
            break;
        }
    }

    if (length == 0) {
        return nullptr;
    }
    buffer[length] = '\0';
    return buffer;
}

int fputc(int character, FILE* stream) {
    unsigned char byte = static_cast<unsigned char>(character);

    return fwrite(&byte, 1, 1, stream) == 1 ? byte : EOF;
}

int putc(int character, FILE* stream) {
    return fputc(character, stream);
}

int putchar(int character) {
    return fputc(character, stdout);
}

int fputs(const char* __restrict text, FILE* __restrict stream) {
    size_t length = strlen(text);

    return fwrite(text, 1, length, stream) == length ? 0 : EOF;
}

int puts(const char* text) {
    if (fputs(text, stdout) == EOF) {
        return EOF;
    }

    return putchar('\n') == EOF ? EOF : 0;
}

int vfprintf(FILE* __restrict stream, const char* __restrict format, va_list arguments) {
    Libc::Scoped lock(stream->lock);
    auto sink = [](const char* text, size_t length, void* context) -> int {
        FILE* target = static_cast<FILE*>(context);

        return Write_Locked(target, text, length) == length ? 0 : -1;
    };

    return Libc::Format_To(sink, stream, format, arguments);
}

int fprintf(FILE* __restrict stream, const char* __restrict format, ...) {
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vfprintf(stream, format, arguments);
    va_end(arguments);
    return length;
}

int vprintf(const char* __restrict format, va_list arguments) {
    return vfprintf(stdout, format, arguments);
}

int printf(const char* __restrict format, ...) {
    va_list arguments;
    int length;

    va_start(arguments, format);
    length = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return length;
}

void __modloader_perror(FILE* stream, const char* prefix) {
    if (prefix != nullptr && prefix[0] != '\0') {
        fprintf(stream, "%s: %s\n", prefix, strerror(errno));
    }
    else {
        fprintf(stream, "%s\n", strerror(errno));
    }
}

void perror(const char* prefix) {
    __modloader_perror(stderr, prefix);
}

int __modloader_remove(const char* folder, const char* path) {
    if (ModLoader_Host_File_Delete(folder, strlen(folder), path, strlen(path)) == 0) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int __modloader_rename(const char* folder, const char* from, const char* to) {
    // Copy, then delete
    FILE* source = __modloader_fopen(folder, from, "rb");
    FILE* destination;
    char buffer[4096];
    size_t read;
    bool ok = true;

    if (source == nullptr) {
        return -1;
    }

    if (strcmp(from, to) == 0) {
        fclose(source);
        return 0;
    }

    destination = __modloader_fopen(folder, to, "wb");
    if (destination == nullptr) {
        fclose(source);
        return -1;
    }

    while ((read = fread(buffer, 1, sizeof(buffer), source)) != 0) {
        if (fwrite(buffer, 1, read, destination) != read) {
            ok = false;
            break;
        }
    }
    
    ok = ok && !ferror(source);
    fclose(source);
    fclose(destination);
    if (!ok) {
        errno = EIO;
        return -1;
    }
    return __modloader_remove(folder, from);
}

FILE* __modloader_tmpfile(const char* folder) {
    char name[L_tmpnam];
    return __modloader_fopen(folder, tmpnam(name), "w+b");
}

char* tmpnam(char* name) {
    char* out;

    static thread_local char sName[L_tmpnam];
    out = name != nullptr ? name : sName;
    snprintf(out, L_tmpnam, "tmp/%u.tmp", __atomic_fetch_add(&sTemporaryCount, 1, __ATOMIC_RELAXED));
    return out;
}

} // extern "C"
