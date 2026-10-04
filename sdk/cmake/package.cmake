include_guard(GLOBAL)

option(MODLOADER_BUILD_DIST "Build drop-in plugin packages with the default target" OFF)

function(_modloader_json_string output value)
    string(REPLACE "\\" "\\\\" value "${value}")
    string(REPLACE "\"" "\\\"" value "${value}")
    string(REPLACE "\n" "\\n" value "${value}")
    string(REPLACE "\r" "\\r" value "${value}")
    string(REPLACE "\t" "\\t" value "${value}")
    set(${output} "\"${value}\"" PARENT_SCOPE)
endfunction()

function(_modloader_package name libraries)
    set(output "${CMAKE_BINARY_DIR}/modules/${name}.wasm")
    set(runtime "${CMAKE_BINARY_DIR}/modules/${MODLOADER_RUNTIME_NAME}")
    set(metadata "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${name}.json")
    set(packager "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/package_wasm.cmake")
    set_target_properties(${name} PROPERTIES
        PREFIX "" SUFFIX ".a" ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles"
        MODLOADER_MODULE_FILE "${output}")
    add_dependencies(${name} modloader_runtime_package)
    set(private_archives)
    foreach(library IN LISTS libraries)
        if(TARGET ${library})
            get_target_property(type ${library} TYPE)
            if(type STREQUAL "STATIC_LIBRARY")
                list(APPEND private_archives "$<TARGET_FILE:${library}>")
            endif()
        else()
            list(APPEND private_archives "${library}")
        endif()
    endforeach()
    if(private_archives)
        set(dependencies_file "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${name}_private_libraries.stamp")
        add_custom_command(OUTPUT "${dependencies_file}"
            COMMAND "${CMAKE_COMMAND}" -E touch "${dependencies_file}"
            DEPENDS ${private_archives}
            VERBATIM)
        set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${name}_component.cpp"
            APPEND PROPERTY OBJECT_DEPENDS "${dependencies_file}")
    endif()
    get_target_property(manifest ${name} MODLOADER_MODULE_MANIFEST_JSON)
    get_target_property(folder ${name} MODLOADER_MODULE_FOLDER)
    string(JSON plugin_version GET "${manifest}" version)
    _modloader_json_string(plugin_version "${plugin_version}")
    string(JSON metadata_json SET "${manifest}" pluginVersion "${plugin_version}")
    string(JSON metadata_json SET "${metadata_json}" version ${MODLOADER_METADATA_VERSION})
    string(JSON metadata_json SET "${metadata_json}" folder "\"${folder}\"")
    string(JSON metadata_json SET "${metadata_json}" runtime "\"${MODLOADER_RUNTIME_NAME}\"")
    file(GENERATE OUTPUT "${metadata}" CONTENT "${metadata_json}\n")
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${name}_component.cpp"
        APPEND PROPERTY OBJECT_DEPENDS "${runtime};${metadata};${packager}")
    add_custom_command(TARGET ${name} POST_BUILD
        BYPRODUCTS "${output}"
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUTS=$<TARGET_FILE:${name}>" "-DPRIVATE_LIBRARIES=${private_archives}"
            "-DMETADATA=${metadata}" "-DOUTPUT=${output}" "-DRUNTIME=${runtime}"
            "-DSECTION=modloader.module" "-DTARGET=${MODLOADER_TARGET}"
            "-DCLANG=${MODLOADER_TOOLCHAIN}/clang${MODLOADER_EXECUTABLE_SUFFIX}"
            "-DLINKER=${MODLOADER_TOOLCHAIN}/wasm-ld${MODLOADER_EXECUTABLE_SUFFIX}"
            -P "${packager}"
        VERBATIM)
endfunction()

function(_modloader_stage_assets name assets)
    if(NOT assets)
        return()
    endif()
    file(GLOB_RECURSE asset_files CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE "${assets}/*")
    set(asset_list "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${name}_assets.txt")
    file(GENERATE OUTPUT "${asset_list}" CONTENT "${asset_files}\n")
    set(stamp "${CMAKE_CURRENT_BINARY_DIR}/CMakeFiles/${name}_assets.stamp")
    set(script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/stage_assets.cmake")
    add_custom_command(OUTPUT "${stamp}"
        COMMAND "${CMAKE_COMMAND}" "-DASSETS=${assets}"
            "-DOUTPUT=${CMAKE_BINARY_DIR}/modules/${name}.assets" "-DSTAMP=${stamp}" -P "${script}"
        DEPENDS ${asset_files} "${asset_list}" "${script}"
        VERBATIM)
    add_custom_target(${name}_assets DEPENDS "${stamp}")
    add_dependencies(${name} ${name}_assets)
    set_target_properties(${name} PROPERTIES
        MODLOADER_MODULE_ASSETS "${assets}" MODLOADER_MODULE_ASSET_STAMP "${stamp}")
endfunction()

function(_modloader_create_package name)
    if(TARGET ${name}_package)
        return()
    endif()
    get_target_property(package ${name} MODLOADER_MODULE_PACKAGE)
    if(NOT package)
        get_target_property(module_file ${name} MODLOADER_MODULE_FILE)
        get_target_property(manifest ${name} MODLOADER_MODULE_MANIFEST)
        get_property(assets TARGET ${name} PROPERTY MODLOADER_MODULE_ASSETS)
        get_property(asset_stamp TARGET ${name} PROPERTY MODLOADER_MODULE_ASSET_STAMP)
        set(package "${CMAKE_BINARY_DIR}/modules/${name}.pak")
        set(script "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/package_archive.cmake")
        add_custom_command(OUTPUT "${package}"
            COMMAND "${CMAKE_COMMAND}" "-DMODULE=${module_file}" "-DMANIFEST=${manifest}"
                "-DASSETS=${assets}" "-DOUTPUT=${package}"
                "-DSTAGING=${CMAKE_BINARY_DIR}/CMakeFiles/modloader-packages/${name}" -P "${script}"
            DEPENDS ${name} "${module_file}" "${manifest}" ${asset_stamp} "${script}"
            VERBATIM)
        set_property(TARGET ${name} PROPERTY MODLOADER_MODULE_PACKAGE "${package}")
    endif()
    add_custom_target(${name}_package DEPENDS "${package}")
    add_dependencies(${name}_package ${name})
endfunction()

function(modloader_package_module name)
    if(NOT TARGET dist)
        add_custom_target(dist
            COMMAND "${CMAKE_COMMAND}" "-DPACKAGES=$<TARGET_PROPERTY:dist,MODLOADER_DIST_PACKAGES>"
                "-DRUNTIME=${CMAKE_BINARY_DIR}/modules/${MODLOADER_RUNTIME_NAME}" "-DOUTPUT=${CMAKE_BINARY_DIR}/dist"
                -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/stage_dist.cmake"
            DEPENDS modloader_runtime_package
            VERBATIM)
        if(MODLOADER_BUILD_DIST)
            set_property(TARGET dist PROPERTY EXCLUDE_FROM_ALL FALSE)
        endif()
    endif()
    _modloader_create_package(${name})
    get_target_property(package ${name} MODLOADER_MODULE_PACKAGE)
    get_property(packaged TARGET dist PROPERTY MODLOADER_DIST_PACKAGES)
    if(package IN_LIST packaged)
        return()
    endif()
    set_property(TARGET dist APPEND PROPERTY MODLOADER_DIST_PACKAGES "${package}")
    add_dependencies(dist ${name}_package)
    get_target_property(dependencies ${name} MODLOADER_MODULE_DEPENDENCIES)
    foreach(dependency IN LISTS dependencies)
        modloader_package_module(${dependency})
    endforeach()
endfunction()
