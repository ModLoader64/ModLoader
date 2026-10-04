cmake_minimum_required(VERSION 3.24)
set(MODLOADER_NATIVE_CLANG_CL ON)
include(../cmake/native.cmake)
project(ModLoaderSDK VERSION 1 LANGUAGES C CXX)

include(ExternalProject)
include(ProcessorCount)
get_filename_component(MODLOADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(MODLOADER_ABI_ROOT "${MODLOADER_ROOT}/ModLoader-ABI")
set(MODLOADER_LLVM_SOURCE "${MODLOADER_ROOT}/third_party/llvm")
set(MODLOADER_LLVM_BUILD "${CMAKE_BINARY_DIR}/llvm")
set(MODLOADER_SDK_TOOLCHAIN "${CMAKE_BINARY_DIR}/toolchain")
set(MODLOADER_SYSROOT "${CMAKE_BINARY_DIR}/sysroot")
set(MODLOADER_SDK_RUNTIME_ROOT "${CMAKE_BINARY_DIR}/runtime")
set(MODLOADER_STACK_SIZE 262144 CACHE STRING "Module C stack bytes (multiple of 16)")
set(MODLOADER_MAX_MEMORY 8589934592 CACHE STRING "Runtime shared memory budget in bytes (multiple of 65536)")

ProcessorCount(build_jobs)
if(NOT build_jobs)
    set(build_jobs 1)
endif()
set(MODLOADER_BUILD_JOBS "${build_jobs}" CACHE STRING "Parallel jobs per toolchain build")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(MODLOADER_NINJA ninja REQUIRED)

set(native_arguments
    "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}"
    "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    "-DCMAKE_INSTALL_PREFIX=${MODLOADER_SDK_TOOLCHAIN}"
    "-DCMAKE_MAKE_PROGRAM=${MODLOADER_NINJA}")
if(CMAKE_LINKER)
    list(APPEND native_arguments "-DCMAKE_LINKER=${CMAKE_LINKER}")
endif()
if(WIN32)
    get_filename_component(compiler_directory "${CMAKE_C_COMPILER}" DIRECTORY)
    find_program(modloader_masm NAMES ml64 llvm-ml HINTS "${compiler_directory}" REQUIRED)
    list(APPEND native_arguments "-DCMAKE_ASM_MASM_COMPILER=${modloader_masm}")
    if(modloader_masm MATCHES "llvm-ml")
        list(APPEND native_arguments "-DCMAKE_ASM_MASM_FLAGS=--m64")
    endif()
endif()
ExternalProject_Add(llvm
    SOURCE_DIR "${MODLOADER_LLVM_SOURCE}/llvm"
    BINARY_DIR "${MODLOADER_LLVM_BUILD}"
    CMAKE_GENERATOR Ninja
    LIST_SEPARATOR |
    CMAKE_ARGS ${native_arguments}
        "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}"
        "-DLLVM_ENABLE_PROJECTS=clang|lld|clang-tools-extra"
        "-DLLVM_TARGETS_TO_BUILD=X86|WebAssembly"
        "-DLLVM_DISTRIBUTION_COMPONENTS=clang|clang-resource-headers|clangd|lld|llvm-ar|llvm-ranlib"
        -DLLVM_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF
        -DLLVM_ENABLE_ZLIB=OFF -DLLVM_ENABLE_ZSTD=OFF -DLLVM_ENABLE_LIBXML2=OFF
        -DLLVM_ENABLE_LIBEDIT=OFF -DLLVM_ENABLE_CURL=OFF
        -DLLVM_BUILD_LLVM_DYLIB=OFF -DLLVM_LINK_LLVM_DYLIB=OFF
        -DLLVM_PARALLEL_LINK_JOBS=1
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target distribution llvm-libraries
        --parallel "${MODLOADER_BUILD_JOBS}"
    INSTALL_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --target install-distribution
        --parallel "${MODLOADER_BUILD_JOBS}"
    BUILD_ALWAYS ON)

ExternalProject_Add(wamrc
    SOURCE_DIR "${MODLOADER_ROOT}/third_party/wamr/wamr-compiler"
    BINARY_DIR "${CMAKE_BINARY_DIR}/wamrc"
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS ${native_arguments}
        "-DLLVM_DIR=${MODLOADER_LLVM_BUILD}/lib/cmake/llvm"
        -DWAMR_BUILD_WITH_CUSTOM_LLVM=1
        -DWAMR_BUILD_LIBC_WASI=0 -DWAMR_BUILD_LIB_PTHREAD=0 -DWAMR_BUILD_LIB_WASI_THREADS=0
        -DWAMR_BUILD_INVOKE_NATIVE_GENERAL=1
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${MODLOADER_BUILD_JOBS}"
    INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR> --config Release
    BUILD_ALWAYS ON
    DEPENDS llvm)

set(wasm_project_arguments
    BUILD_ALWAYS ON
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel "${MODLOADER_BUILD_JOBS}"
    CMAKE_GENERATOR Ninja
    CMAKE_ARGS
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchain.cmake"
        "-DMODLOADER_TOOLCHAIN=${MODLOADER_SDK_TOOLCHAIN}/bin"
        "-DMODLOADER_SYSROOT=${MODLOADER_SYSROOT}"
        "-DCMAKE_BUILD_TYPE=Release"
        "-DCMAKE_INSTALL_PREFIX=${MODLOADER_SYSROOT}"
        "-DCMAKE_MAKE_PROGRAM=${MODLOADER_NINJA}")

ExternalProject_Add(sysroot
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtimes"
    BINARY_DIR "${CMAKE_BINARY_DIR}/runtimes"
    ${wasm_project_arguments}
    DEPENDS llvm)

ExternalProject_Add(builtins
    SOURCE_DIR "${MODLOADER_LLVM_SOURCE}/compiler-rt/lib/builtins"
    BINARY_DIR "${CMAKE_BINARY_DIR}/builtins"
    ${wasm_project_arguments}
        "-DLLVM_CMAKE_DIR=${MODLOADER_LLVM_BUILD}/lib/cmake/llvm"
        -DCOMPILER_RT_BAREMETAL_BUILD=ON
        -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON
        -DCOMPILER_RT_BUILD_CRT=OFF
        -DCOMPILER_RT_HAS_FPIC_FLAG=OFF
        -DCOMPILER_RT_INCLUDE_TESTS=OFF
        "-DCOMPILER_RT_INSTALL_LIBRARY_DIR=${MODLOADER_SYSROOT}/lib"
    DEPENDS sysroot)

ExternalProject_Add(cxx
    SOURCE_DIR "${MODLOADER_LLVM_SOURCE}/runtimes"
    BINARY_DIR "${CMAKE_BINARY_DIR}/cxx"
    LIST_SEPARATOR |
    ${wasm_project_arguments}
        -DMODLOADER_CXX_LANGUAGE_FLAGS=
        "-DLLVM_ENABLE_RUNTIMES=libcxx|libcxxabi"
        -DLLVM_ENABLE_PER_TARGET_RUNTIME_DIR=OFF
        -DLLVM_INCLUDE_TESTS=OFF
        -DLLVM_INCLUDE_DOCS=OFF
        -DUNIX=ON
        -DLIBCXX_ENABLE_SHARED=OFF
        -DLIBCXX_ENABLE_STATIC=ON
        -DLIBCXX_ENABLE_EXCEPTIONS=OFF
        -DLIBCXX_ENABLE_RTTI=OFF
        -DLIBCXX_ENABLE_THREADS=ON
        -DLIBCXX_HAS_PTHREAD_API=ON
        -DLIBCXX_ENABLE_MONOTONIC_CLOCK=ON
        -DLIBCXX_ENABLE_FILESYSTEM=OFF
        -DLIBCXX_ENABLE_LOCALIZATION=OFF
        -DLIBCXX_ENABLE_WIDE_CHARACTERS=OFF
        -DLIBCXX_ENABLE_RANDOM_DEVICE=OFF
        -DLIBCXX_ENABLE_TIME_ZONE_DATABASE=OFF
        -DLIBCXX_CXX_ABI=libcxxabi
        -DLIBCXX_USE_COMPILER_RT=ON
        -DLIBCXX_INCLUDE_BENCHMARKS=OFF
        -DLIBCXXABI_ENABLE_SHARED=OFF
        -DLIBCXXABI_ENABLE_STATIC=ON
        -DLIBCXXABI_ENABLE_EXCEPTIONS=OFF
        -DLIBCXXABI_ENABLE_THREADS=ON
        -DLIBCXXABI_HAS_PTHREAD_API=ON
        -DLIBCXXABI_USE_LLVM_UNWINDER=OFF
        -DLIBCXXABI_USE_COMPILER_RT=ON
        -DLIBCXXABI_SILENT_TERMINATE=ON
    DEPENDS builtins)

set(sdk_destination share/modloader/sdk)
set(runtime_targets)
file(GLOB platform_presets CONFIGURE_DEPENDS cmake/platforms/*.cmake)
foreach(preset IN LISTS platform_presets)
    get_filename_component(platform "${preset}" NAME_WE)
    ExternalProject_Add(runtime_${platform}
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
        BINARY_DIR "${MODLOADER_SDK_RUNTIME_ROOT}/${platform}"
        INSTALL_COMMAND ""
        ${wasm_project_arguments}
            "-DMODLOADER_PLATFORM=${platform}"
            "-DMODLOADER_STACK_SIZE=${MODLOADER_STACK_SIZE}"
            "-DMODLOADER_MAX_MEMORY=${MODLOADER_MAX_MEMORY}"
        DEPENDS cxx)
    list(APPEND runtime_targets runtime_${platform})
    install(FILES
        "${MODLOADER_SDK_RUNTIME_ROOT}/${platform}/modules/modloader-runtime.wasm"
        DESTINATION ${sdk_destination}/lib/${platform} COMPONENT SDK)
endforeach()

install(DIRECTORY "${MODLOADER_SDK_TOOLCHAIN}/" DESTINATION . COMPONENT SDK USE_SOURCE_PERMISSIONS)
install(DIRECTORY include cmake module DESTINATION ${sdk_destination} COMPONENT SDK)
install(DIRECTORY "${MODLOADER_ABI_ROOT}/include" DESTINATION share/modloader/ModLoader-ABI COMPONENT SDK)
install(FILES "${MODLOADER_ROOT}/third_party/imgui/imgui.h" "${MODLOADER_ROOT}/third_party/imgui/imconfig.h"
    DESTINATION share/modloader/third_party/imgui COMPONENT SDK)
install(DIRECTORY "${MODLOADER_SYSROOT}/include/" DESTINATION ${sdk_destination}/sysroot/include COMPONENT SDK)
foreach(library libc++.a libc++abi.a libz.a libyyjson.a libc.a libclang_rt.builtins-wasm64.a)
    install(FILES "${MODLOADER_SYSROOT}/lib/${library}" DESTINATION ${sdk_destination}/sysroot/lib COMPONENT SDK)
endforeach()
install(FILES cmake/ModLoaderConfig.cmake DESTINATION lib/cmake/ModLoader COMPONENT SDK)

include("${MODLOADER_ROOT}/cmake/distribution.cmake")
modloader_add_distribution(SDK wamrc ${runtime_targets})
