include_guard(GLOBAL)

function(modloader_add_distribution component)
    if(CMAKE_CXX_SIMULATE_ID STREQUAL "MSVC" AND NOT MSVC)
        set(MSVC TRUE)
        if(MSVC_VERSION GREATER_EQUAL 1930)
            set(MSVC_TOOLSET_VERSION 143)
        elseif(MSVC_VERSION GREATER_EQUAL 1920)
            set(MSVC_TOOLSET_VERSION 142)
        elseif(MSVC_VERSION GREATER_EQUAL 1910)
            set(MSVC_TOOLSET_VERSION 141)
        elseif(MSVC_VERSION EQUAL 1900)
            set(MSVC_TOOLSET_VERSION 140)
        endif()
    endif()
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT "${component}")
    include(InstallRequiredSystemLibraries)

    add_custom_target(dist ALL
        COMMAND "${CMAKE_COMMAND}" "-DBUILD_DIRECTORY=${CMAKE_BINARY_DIR}"
            "-DCOMPONENT=${component}" "-DCONFIGURATION=$<CONFIG>"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/stage_distribution.cmake"
        DEPENDS ${ARGN}
        VERBATIM USES_TERMINAL)
endfunction()
