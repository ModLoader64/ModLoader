cmake_minimum_required(VERSION 3.24)
project(ModLoaderSysroot LANGUAGES C CXX)

get_filename_component(MODLOADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(libc_source "${MODLOADER_ROOT}/sdk/libc")
add_subdirectory("${MODLOADER_ROOT}/ModLoader-ABI" modloader-abi EXCLUDE_FROM_ALL)
set(CMAKE_C_STANDARD 11)
include_directories("${libc_source}/include")

file(GLOB_RECURSE libc_sources CONFIGURE_DEPENDS "${libc_source}/src/*.cpp")
add_library(c STATIC ${libc_sources})
target_include_directories(c PRIVATE
    "${libc_source}/src" ../include "${MODLOADER_ROOT}/third_party/llvm-libc/libc")
target_link_libraries(c PRIVATE ModLoader::ABI)
target_compile_features(c PRIVATE cxx_std_23)
target_compile_definitions(c PRIVATE
    LIBC_FULL_BUILD
    LIBC_NAMESPACE=__llvm_libc_modloader
    LIBC_ERRNO_MODE=LIBC_ERRNO_MODE_SYSTEM_INLINE
    LIBC_COPT_USE_C_ASSERT
    LIBC_COPT_PRINTF_DISABLE_STRERROR
    LIBC_COPT_PRINTF_MODULAR
    NDEBUG)

target_compile_options(c PRIVATE -O2 -fno-builtin)

set(zlib_source "${MODLOADER_ROOT}/third_party/zlib")
add_library(z STATIC
    ${zlib_source}/adler32.c
    ${zlib_source}/crc32.c
    ${zlib_source}/deflate.c
    ${zlib_source}/infback.c
    ${zlib_source}/inffast.c
    ${zlib_source}/inflate.c
    ${zlib_source}/inftrees.c
    ${zlib_source}/trees.c
    ${zlib_source}/zutil.c
    zlib/zlib_native.c)
target_include_directories(z PRIVATE "${zlib_source}")

set(yyjson_source "${MODLOADER_ROOT}/third_party/yyjson/src")
add_library(yyjson STATIC "${yyjson_source}/yyjson.c")

install(TARGETS c z yyjson ARCHIVE DESTINATION lib)
install(DIRECTORY "${libc_source}/include/" DESTINATION include)
install(FILES ${zlib_source}/zlib.h ${zlib_source}/zconf.h ${yyjson_source}/yyjson.h DESTINATION include)
