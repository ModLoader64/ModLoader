cmake_minimum_required(VERSION 3.24)

file(REMOVE_RECURSE "${STAGING}")
file(MAKE_DIRECTORY "${STAGING}/assets")
file(COPY_FILE "${MODULE}" "${STAGING}/module.wasm")
file(COPY_FILE "${MANIFEST}" "${STAGING}/manifest.json")
if(ASSETS)
    file(COPY "${ASSETS}/" DESTINATION "${STAGING}/assets")
endif()
get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${OUTPUT}.tmp" --format=zip manifest.json module.wasm assets
    WORKING_DIRECTORY "${STAGING}" COMMAND_ERROR_IS_FATAL ANY)
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
