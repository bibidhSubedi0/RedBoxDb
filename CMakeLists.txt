cmake_minimum_required(VERSION 3.23)

project(
    RedBoxDb
    VERSION 0.1.0
    LANGUAGES CXX
)

# Prevent in-source builds
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed")
endif()

include(cmake/StandardProjectSettings.cmake)

add_subdirectory(src)

include(GNUInstallDirs)
