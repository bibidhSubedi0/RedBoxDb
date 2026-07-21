# =========================
# RedBoxDb core library
# =========================
add_library(RedBoxDbLib)

target_sources(RedBoxDbLib
    PRIVATE
        engine.cpp
 )

target_include_directories(RedBoxDbLib
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(RedBoxDbLib PRIVATE spdlog::spdlog Threads::Threads)

include(CheckCXXCompilerFlag)

if(MSVC)
    check_cxx_compiler_flag("/arch:AVX2" REDBOXDB_HAS_AVX2_FLAG)
    if(REDBOXDB_HAS_AVX2_FLAG)
        target_compile_options(RedBoxDbLib PRIVATE /arch:AVX2)
    endif()
else()
    check_cxx_compiler_flag("-mavx2 -mfma" REDBOXDB_HAS_AVX2_FLAG)
    if(REDBOXDB_HAS_AVX2_FLAG)
        target_compile_options(RedBoxDbLib PRIVATE -mavx2 -mfma)
    endif()
endif()
# On non-x86 targets (ARM, RISC-V, ...) these flags are invalid and the
# compiler-flag check above fails, so the library falls back to
# Distance::l2_scalar() at runtime via Platform::has_avx2() returning false.

set_project_warnings(RedBoxDbLib)

# =========================
# RedBoxDb executable
# =========================
add_executable(RedBoxDb)

target_sources(RedBoxDb
    PRIVATE
        "main.cpp"
 )

target_link_libraries(RedBoxDb
    PRIVATE
        RedBoxDbLib
)

set_project_warnings(RedBoxDb)

# =========================
# RedBoxServer (The TCP Service)
# =========================
add_executable(RedBoxServer server.cpp)

target_link_libraries(RedBoxServer
    PRIVATE
        RedBoxDbLib
        spdlog::spdlog
        Threads::Threads
)

# Only link Winsock on Windows
if(WIN32)
    target_link_libraries(RedBoxServer PRIVATE ws2_32)
endif()

set_project_warnings(RedBoxServer)