cmake_minimum_required(VERSION 3.24)

file(READ "${METADATA}" metadata)
if(DEFINED RUNTIME)
    file(SHA1 "${RUNTIME}" runtime_hash)
    string(JSON metadata SET "${metadata}" runtimeHash "\"${runtime_hash}\"")
endif()

string(REPLACE "\\" "\\\\" assembly "${metadata}")
string(REPLACE "\"" "\\\"" assembly "${assembly}")
string(REPLACE "\n" "\\n" assembly "${assembly}")
string(REPLACE "\r" "\\r" assembly "${assembly}")
file(WRITE "${METADATA}.s" ".section .custom_section.${SECTION},\"\",@\n.ascii \"${assembly}\"\n")
execute_process(COMMAND "${CLANG}" "--target=${TARGET}" -c -x assembler
    "${METADATA}.s" -o "${METADATA}.o"
    COMMAND_ERROR_IS_FATAL ANY)

get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")
execute_process(COMMAND "${LINKER}" -m wasm64 -r
    --whole-archive ${INPUTS} --no-whole-archive ${PRIVATE_LIBRARIES}
    "${METADATA}.o" -o "${OUTPUT}.tmp"
    COMMAND_ERROR_IS_FATAL ANY)
file(RENAME "${OUTPUT}.tmp" "${OUTPUT}")
