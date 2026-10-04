include_guard(GLOBAL)

get_filename_component(MODLOADER_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(MODLOADER_SDK_DIR "${MODLOADER_SDK_DIR}" CACHE INTERNAL "Selected ModLoader SDK" FORCE)
set(metadata_header "${MODLOADER_SDK_DIR}/include/modloader/detail/metadata.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${metadata_header}")
file(STRINGS "${metadata_header}" metadata_version REGEX "^#define MODLOADER_METADATA_VERSION ")
string(REGEX MATCH "[0-9]+" MODLOADER_METADATA_VERSION "${metadata_version}")
get_filename_component(MODLOADER_ABI_ROOT "${MODLOADER_SDK_DIR}/../ModLoader-ABI" ABSOLUTE)
include("${CMAKE_CURRENT_LIST_DIR}/platforms/${MODLOADER_PLATFORM}.cmake")

function(_modloader_sdk_interface name)
    add_library(${name} INTERFACE IMPORTED GLOBAL)
    target_include_directories(${name} INTERFACE
        "${MODLOADER_SDK_DIR}/include"
        "${MODLOADER_ABI_ROOT}/include"
        "${MODLOADER_SDK_DIR}/../third_party/imgui")
    if(EXISTS "${MODLOADER_ABI_ROOT}/include/${MODLOADER_PLATFORM}")
        target_include_directories(${name} INTERFACE "${MODLOADER_ABI_ROOT}/include/${MODLOADER_PLATFORM}")
    endif()
    string(TOUPPER "${MODLOADER_PLATFORM}" platform_macro)
    target_compile_definitions(${name} INTERFACE
        IMGUI_USER_CONFIG="modloader_imgui_config.h"
        MODLOADER_PLATFORM_SPACES_HEADER="modloader/platforms/${MODLOADER_PLATFORM}/spaces.h"
        MODLOADER_PLATFORM_HEADER="modloader/platforms/${MODLOADER_PLATFORM}/platform.h"
        MODLOADER_PLATFORM_${platform_macro})
    target_compile_features(${name} INTERFACE cxx_std_23)
endfunction()

function(_modloader_load_runtime)
    set(MODLOADER_RUNTIME "${MODLOADER_SDK_DIR}/lib/${MODLOADER_PLATFORM}/modloader-runtime.wasm")
    set(MODLOADER_RUNTIME_NAME "modloader-runtime-${MODLOADER_PLATFORM}.wasm" CACHE INTERNAL "Packaged SDK runtime filename" FORCE)
    _modloader_sdk_interface(modloader_runtime)
    add_library(ModLoader::Runtime ALIAS modloader_runtime)
    set(output "${CMAKE_BINARY_DIR}/modules/${MODLOADER_RUNTIME_NAME}")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/modules"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MODLOADER_RUNTIME}" "${output}"
        DEPENDS "${MODLOADER_RUNTIME}"
        VERBATIM)
    add_custom_target(modloader_runtime_package DEPENDS "${output}")
endfunction()
