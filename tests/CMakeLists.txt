add_executable(RedBoxDbTests
    test_main.cpp
)

target_link_libraries(RedBoxDbTests
    PRIVATE
        gtest
        gtest_main
        RedBoxDbLib
)

set_project_warnings(RedBoxDbTests)


# Register tests with CTest (Standard CMake testing tool)
include(GoogleTest)
gtest_discover_tests(RedBoxDbTests)