add_executable(RedBoxDbTests
    test_main.cpp
    test_hnsw.cpp
    test_extended.cpp
    test_metadata_store.cpp
)

target_compile_definitions(RedBoxDbTests PRIVATE REDBOX_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

target_link_libraries(RedBoxDbTests
    PRIVATE
        gtest
        gtest_main
        RedBoxDbLib
        spdlog::spdlog
        pqxx
        PostgreSQL::PostgreSQL
)

set_project_warnings(RedBoxDbTests)

include(CheckCXXCompilerFlag)
if(MSVC)
    check_cxx_compiler_flag("/arch:AVX2" REDBOXDB_HAS_AVX2_FLAG)
    if(REDBOXDB_HAS_AVX2_FLAG)
        target_compile_options(RedBoxDbTests PRIVATE /arch:AVX2)
    endif()
else()
    check_cxx_compiler_flag("-mavx2 -mfma" REDBOXDB_HAS_AVX2_FLAG)
    if(REDBOXDB_HAS_AVX2_FLAG)
        target_compile_options(RedBoxDbTests PRIVATE -mavx2 -mfma)
    endif()
endif()

add_executable(VerifyHnsw verify_hnsw.cpp)
target_link_libraries(VerifyHnsw PRIVATE RedBoxDbLib spdlog::spdlog)
set_project_warnings(VerifyHnsw)


# Register tests with CTest (Standard CMake testing tool)
include(GoogleTest)
gtest_discover_tests(RedBoxDbTests)