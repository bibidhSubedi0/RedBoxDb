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
add_subdirectory(tests)

include(GNUInstallDirs)

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)


# Prevent gtest from overriding your compiler flags
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)


