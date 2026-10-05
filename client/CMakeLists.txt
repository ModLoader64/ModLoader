set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

set(MODLOADER_SDK "$ENV{MODLOADER_SDK}" CACHE PATH "Installed ModLoader SDK/toolchain bundle")
cmake_path(ABSOLUTE_PATH MODLOADER_SDK BASE_DIRECTORY "${CMAKE_BINARY_DIR}" NORMALIZE)
set(MODLOADER_SDK "${MODLOADER_SDK}" CACHE PATH "Installed ModLoader SDK/toolchain bundle" FORCE)
set(client_tools
    "${MODLOADER_SDK}/bin/wamrc${CMAKE_EXECUTABLE_SUFFIX}"
    "${MODLOADER_SDK}/bin/wasm-ld${CMAKE_EXECUTABLE_SUFFIX}")

include(cmake/dependencies.cmake)

find_package(Threads REQUIRED)
file(GLOB_RECURSE client_sources CONFIGURE_DEPENDS src/*.cpp)
add_executable(modloader ${client_sources})
set_target_properties(modloader PROPERTIES OUTPUT_NAME ModLoader)
target_compile_features(modloader PRIVATE cxx_std_23)
target_include_directories(modloader PRIVATE src ${MODLOADER_ROOT}/sdk/include ${WAMR_ROOT_DIR}/core/iwasm/include ${MODLOADER_ROOT}/third_party/stb)
target_include_directories(modloader SYSTEM PRIVATE ${MODLOADER_ROOT}/third_party/sdl/src/video/khronos)
set_source_files_properties(src/library_natives.cpp PROPERTIES
    INCLUDE_DIRECTORIES ${MODLOADER_ROOT}/third_party/llvm-libc/libc
    COMPILE_DEFINITIONS "LIBC_NAMESPACE=__llvm_libc_client;LIBC_COPT_USE_C_ASSERT;NDEBUG"
)
target_link_libraries(modloader PRIVATE vmlib modloader_zip modloader_yyjson modloader_monocypher modloader_imgui RmlUi::RmlUi
    ModLoader::ABI ModLoader::N64Tools SDL3::SDL3-static Threads::Threads ${CMAKE_DL_LIBS})
if (WIN32)
    target_compile_definitions(modloader PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(modloader PRIVATE ws2_32 bcrypt)
endif ()

add_custom_target(modloader_tools
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:modloader>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${client_tools} "$<TARGET_FILE_DIR:modloader>/"
    VERBATIM)
add_dependencies(modloader modloader_tools)
foreach(tool IN LISTS client_tools)
    file(REAL_PATH "${tool}" tool_source)
    get_filename_component(tool_name "${tool}" NAME)
    install(PROGRAMS "${tool_source}" DESTINATION bin RENAME "${tool_name}" COMPONENT Client)
endforeach()
install(TARGETS modloader RUNTIME DESTINATION bin COMPONENT Client)

set(MODLOADER_ADAPTERS "gopher64" CACHE STRING "Adapters to build (semicolon separated; empty builds only the client)")
set(adapter_targets)
if("gopher64" IN_LIST MODLOADER_ADAPTERS)
    set(adapter_source "${MODLOADER_ROOT}/emulators/gopher64")
    find_program(MODLOADER_CARGO cargo REQUIRED)
    set(adapter_build "${CMAKE_BINARY_DIR}/adapters/gopher64")
    set(adapter_libraries "${adapter_build}/release/${CMAKE_SHARED_LIBRARY_PREFIX}gopher64${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(WIN32)
        list(APPEND adapter_libraries
            "${adapter_source}/rt64/src/contrib/dxc/bin/x64/dxcompiler.dll"
            "${adapter_source}/rt64/src/contrib/dxc/bin/x64/dxil.dll")
    endif()
    add_custom_target(modloader_adapter ALL
        COMMAND "${MODLOADER_CARGO}" build --locked --release --lib --no-default-features --features modloader --target-dir "${adapter_build}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:modloader>/adapters"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${adapter_libraries} "$<TARGET_FILE_DIR:modloader>/adapters/"
        WORKING_DIRECTORY "${adapter_source}"
        VERBATIM USES_TERMINAL)
    list(APPEND adapter_targets modloader_adapter)
    install(FILES ${adapter_libraries} DESTINATION bin/adapters COMPONENT Client)
endif()

include("${MODLOADER_ROOT}/cmake/distribution.cmake")
modloader_add_distribution(Client modloader ${adapter_targets})
