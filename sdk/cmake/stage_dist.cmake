cmake_minimum_required(VERSION 3.24)

file(REMOVE_RECURSE "${OUTPUT}/plugins")
file(MAKE_DIRECTORY "${OUTPUT}/plugins")
file(COPY ${PACKAGES} "${RUNTIME}" DESTINATION "${OUTPUT}/plugins")
