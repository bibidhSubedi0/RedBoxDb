# =========================
# RedBoxDb core library
# =========================
add_library(RedBoxDbLib)

target_sources(RedBoxDbLib
    PRIVATE
        engine.cpp
 )

if(REDBOX_ENABLE_PG)
    target_sources(RedBoxDbLib PRIVATE metadata_store.cpp)
endif()

target_include_directories(RedBoxDbLib
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# spdlog is PUBLIC, not PRIVATE: include/redboxdb/logger.hpp (a public
# header) includes <spdlog/spdlog.h> directly, so anything that includes
# our headers needs spdlog's include paths too. This also matters for the
# installed package config (cmake/RedBoxDbConfig.cmake.in), which declares
# spdlog as a find_dependency() that consumers must have available.
target_link_libraries(RedBoxDbLib PUBLIC spdlog::spdlog PRIVATE Threads::Threads)

install(
    TARGETS RedBoxDbLib
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(REDBOX_ENABLE_PG)
    target_link_libraries(RedBoxDbLib PRIVATE pqxx PostgreSQL::PostgreSQL)
endif()

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

install(TARGETS RedBoxDb RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

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

if(REDBOX_ENABLE_PG)
    target_link_libraries(RedBoxServer PRIVATE pqxx PostgreSQL::PostgreSQL)
endif()

# Only link Winsock on Windows
if(WIN32)
    target_link_libraries(RedBoxServer PRIVATE ws2_32)
endif()

set_project_warnings(RedBoxServer)

install(TARGETS RedBoxServer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
