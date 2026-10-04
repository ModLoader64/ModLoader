#include "zlib.h"

__attribute__((import_module("modloader"), import_name("zlib_compress"))) int ModLoader_Zlib_Compress(
    Bytef* dest,
    z_size_t* dest_length,
    const Bytef* source,
    z_size_t source_length,
    int level
);
__attribute__((import_module("modloader"), import_name("zlib_uncompress"))) int ModLoader_Zlib_Uncompress(
    Bytef* dest,
    z_size_t* dest_length,
    const Bytef* source,
    z_size_t* source_length
);

int ZEXPORT compress2_z(Bytef* dest, z_size_t* destLen, const Bytef* source, z_size_t sourceLen, int level) {
    return ModLoader_Zlib_Compress(dest, destLen, source, sourceLen, level);
}

int ZEXPORT compress2(Bytef* dest, uLongf* destLen, const Bytef* source, uLong sourceLen, int level) {
    z_size_t length = *destLen;
    int result = compress2_z(dest, &length, source, sourceLen, level);

    *destLen = (uLongf)length;
    return result;
}

int ZEXPORT compress_z(Bytef* dest, z_size_t* destLen, const Bytef* source, z_size_t sourceLen) {
    return compress2_z(dest, destLen, source, sourceLen, Z_DEFAULT_COMPRESSION);
}

int ZEXPORT compress(Bytef* dest, uLongf* destLen, const Bytef* source, uLong sourceLen) {
    return compress2(dest, destLen, source, sourceLen, Z_DEFAULT_COMPRESSION);
}

z_size_t ZEXPORT compressBound_z(z_size_t sourceLen) {
    z_size_t bound = sourceLen + (sourceLen >> 12) + (sourceLen >> 14) + (sourceLen >> 25) + 13;

    return bound < sourceLen ? (z_size_t)-1 : bound;
}

uLong ZEXPORT compressBound(uLong sourceLen) {
    return (uLong)compressBound_z(sourceLen);
}

int ZEXPORT uncompress2_z(Bytef* dest, z_size_t* destLen, const Bytef* source, z_size_t* sourceLen) {
    return ModLoader_Zlib_Uncompress(dest, destLen, source, sourceLen);
}

int ZEXPORT uncompress2(Bytef* dest, uLongf* destLen, const Bytef* source, uLong* sourceLen) {
    z_size_t produced = *destLen;
    z_size_t consumed = *sourceLen;
    int result = uncompress2_z(dest, &produced, source, &consumed);

    *destLen = (uLongf)produced;
    *sourceLen = (uLong)consumed;
    return result;
}

int ZEXPORT uncompress_z(Bytef* dest, z_size_t* destLen, const Bytef* source, z_size_t sourceLen) {
    return uncompress2_z(dest, destLen, source, &sourceLen);
}

int ZEXPORT uncompress(Bytef* dest, uLongf* destLen, const Bytef* source, uLong sourceLen) {
    return uncompress2(dest, destLen, source, &sourceLen);
}
