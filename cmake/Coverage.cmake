# Coverage.cmake
#
# Adds gcov/llvm-cov instrumentation when configured with
# -DCMAKE_BUILD_TYPE=Coverage. No effect on any other build type.
#
# Usage:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Coverage -DCMAKE_CXX_COMPILER=g++
#   cmake --build build
#   ctest --test-dir build
#   lcov --directory build --capture --output-file coverage.info

if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    if(NOT (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        message(WARNING "Coverage build type requires GCC or Clang; got ${CMAKE_CXX_COMPILER_ID}. Coverage flags were not added.")
    else()
        message(STATUS "Coverage instrumentation enabled (--coverage)")
        add_compile_options(-O0 -g --coverage)
        add_link_options(--coverage)
    endif()
endif()
