cmake_minimum_required(VERSION 3.24)

set(staging "${BUILD_DIRECTORY}/dist-staging")
file(REMOVE_RECURSE "${staging}")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIRECTORY}"
    --prefix "${staging}" --component "${COMPONENT}" --config "${CONFIGURATION}"
    COMMAND_ERROR_IS_FATAL ANY)
file(REMOVE_RECURSE "${BUILD_DIRECTORY}/dist")
file(RENAME "${staging}" "${BUILD_DIRECTORY}/dist")
message(STATUS "${COMPONENT} distribution: ${BUILD_DIRECTORY}/dist")
