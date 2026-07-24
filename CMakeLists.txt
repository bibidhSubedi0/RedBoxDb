cmake_minimum_required(VERSION 3.23)

project(
    RedBoxDb
    VERSION 0.1.0
    LANGUAGES CXX
)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed")
endif()

include(cmake/StandardProjectSettings.cmake)
include(cmake/Coverage.cmake)
include(GNUInstallDirs)
include(FetchContent)

find_package(Threads REQUIRED)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        f355b3d58f7067eee1706ff3c801c2361011f3d5 # v1.15.1 -- pinned by commit hash, tags are mutable
)
FetchContent_MakeAvailable(spdlog)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

option(REDBOX_ENABLE_PG "Enable PostgreSQL metadata store" ON)

if(REDBOX_ENABLE_PG)
    find_package(PostgreSQL)
    if(PostgreSQL_FOUND)
        FetchContent_Declare(
            libpqxx
            GIT_REPOSITORY https://github.com/jtv/libpqxx.git
            GIT_TAG        8.0.2
        )
        set(PQXX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(PQXX_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(libpqxx)
        add_compile_definitions(REDBOX_PG_ENABLED)
        message(STATUS "PostgreSQL support: ENABLED")
    else()
        set(REDBOX_ENABLE_PG OFF)
        message(STATUS "PostgreSQL not found: metadata store DISABLED")
    endif()
else()
    message(STATUS "PostgreSQL support: DISABLED (REDBOX_ENABLE_PG=OFF)")
endif()

add_subdirectory(src)
add_subdirectory(tests)
add_subdirectory(benchmark)
