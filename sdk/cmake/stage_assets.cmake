cmake_minimum_required(VERSION 3.24)

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")
file(COPY "${ASSETS}/" DESTINATION "${OUTPUT}")
file(TOUCH "${STAMP}")
