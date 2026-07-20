add_executable(RedBoxDbTests
    test_main.cpp
    test_hnsw.cpp
    test_extended.cpp
)

target_link_libraries(RedBoxDbTests
    PRIVATE
        gtest
        gtest_main
        RedBoxDbLib
        spdlog::spdlog
)

set_project_warnings(RedBoxDbTests)

if(NOT MSVC)
    target_compile_options(RedBoxDbTests PRIVATE -mavx2 -mfma)
endif()

add_executable(VerifyHnsw verify_hnsw.cpp)
target_link_libraries(VerifyHnsw PRIVATE RedBoxDbLib spdlog::spdlog)
set_project_warnings(VerifyHnsw)


# Register tests with CTest (Standard CMake testing tool)
include(GoogleTest)
gtest_discover_tests(RedBoxDbTests)