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

set_project_warnings(RedBoxDbLib)

# =========================
# RedBoxDb executable
# =========================
add_executable(RedBoxDb)

target_sources(RedBoxDb
    PRIVATE
        main.cpp
 )

target_link_libraries(RedBoxDb
    PRIVATE
        RedBoxDbLib
)

set_project_warnings(RedBoxDb)
