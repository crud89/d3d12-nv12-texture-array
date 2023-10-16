CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

# Check if the build dir does not match the source dir (disallow in-source builds).
IF("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")
   MESSAGE(SEND_ERROR "In-source builds are not allowed.")
ENDIF("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_BINARY_DIR}")

# Setup modules.
INCLUDE("${CMAKE_SOURCE_DIR}/cmake/modules.cmake")

# Declare top-level project.
PROJECT(mwe LANGUAGES CXX)

# Export all symbols automatically on Windows.
IF (WIN32)
    SET (CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
ENDIF()

SET(CMAKE_CXX_STANDARD 23)
SET(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/binaries/>)

ADD_SUBDIRECTORY(sample)