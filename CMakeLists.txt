cmake_minimum_required(VERSION 3.24)
include(cmake/native.cmake)
project(ModLoader VERSION 1 LANGUAGES C CXX)

set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(MODLOADER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
add_subdirectory(ModLoader-ABI modloader-abi)
add_subdirectory(client)
