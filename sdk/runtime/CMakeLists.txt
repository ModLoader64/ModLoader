cmake_minimum_required(VERSION 3.24)
project(ModLoaderSDKRuntime LANGUAGES CXX)

include(../cmake/sdk.cmake)
if(MODLOADER_PLATFORM_LIBRARIES)
    add_subdirectory("${MODLOADER_ABI_ROOT}" modloader-abi EXCLUDE_FROM_ALL)
endif()
_modloader_sdk_interface(modloader_sdk_headers)

file(GLOB_RECURSE runtime_sources CONFIGURE_DEPENDS "${MODLOADER_SDK_DIR}/src/*.cpp")
list(FILTER runtime_sources EXCLUDE REGEX "/platforms/")
file(GLOB_RECURSE platform_sources CONFIGURE_DEPENDS
    "${MODLOADER_SDK_DIR}/src/platforms/${MODLOADER_PLATFORM}/*.cpp")
add_library(modloader_runtime STATIC ${runtime_sources} ${platform_sources} ${MODLOADER_PLATFORM_RUNTIME})
target_include_directories(modloader_runtime PRIVATE "${MODLOADER_SDK_DIR}/src")
target_compile_definitions(modloader_runtime PRIVATE MODLOADER_MODULE_NAME="modloader-runtime")
target_link_libraries(modloader_runtime PUBLIC modloader_sdk_headers ${MODLOADER_PLATFORM_LIBRARIES})

set(dependencies modloader_runtime ${MODLOADER_PLATFORM_LIBRARIES})
set(archives)
foreach(library IN LISTS dependencies)
    list(APPEND archives "$<TARGET_FILE:${library}>")
endforeach()
set(sysroot_libraries libc++.a libc++abi.a libz.a libyyjson.a libc.a libclang_rt.builtins-wasm64.a)
list(TRANSFORM sysroot_libraries PREPEND "${MODLOADER_SYSROOT}/lib/")
list(APPEND archives ${sysroot_libraries})
set(runtime_file "${CMAKE_BINARY_DIR}/modules/modloader-runtime.wasm")
set(metadata "${CMAKE_BINARY_DIR}/CMakeFiles/modloader/runtime.json")
file(GENERATE OUTPUT "${metadata}" CONTENT
"{\n  \"version\": ${MODLOADER_METADATA_VERSION},\n  \"platform\": \"${MODLOADER_PLATFORM}\",\n  \"target\": \"wasm64\",\n  \"stackSize\": ${MODLOADER_STACK_SIZE},\n  \"maxMemory\": ${MODLOADER_MAX_MEMORY}\n}\n")
add_custom_command(OUTPUT "${runtime_file}"
    COMMAND "${CMAKE_COMMAND}"
        "-DINPUTS=${archives}" "-DMETADATA=${metadata}" "-DOUTPUT=${runtime_file}"
        "-DSECTION=modloader.runtime" "-DTARGET=${MODLOADER_TARGET}"
        "-DCLANG=${MODLOADER_TOOLCHAIN}/clang${MODLOADER_EXECUTABLE_SUFFIX}"
        "-DLINKER=${MODLOADER_TOOLCHAIN}/wasm-ld${MODLOADER_EXECUTABLE_SUFFIX}"
        -P "${MODLOADER_SDK_DIR}/cmake/package_wasm.cmake"
    DEPENDS ${dependencies} ${sysroot_libraries} "${metadata}"
        "${MODLOADER_SDK_DIR}/cmake/package_wasm.cmake"
    VERBATIM)
add_custom_target(modloader_sdk ALL DEPENDS "${runtime_file}")

