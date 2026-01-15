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

# =========================
# RedBoxServer (The TCP Service)
# =========================
add_executable(RedBoxServer server.cpp)

target_link_libraries(RedBoxServer
    PRIVATE
        RedBoxDbLib
)

# Only link Winsock on Windows
if(WIN32)
    target_link_libraries(RedBoxServer PRIVATE ws2_32)
endif()

set_project_warnings(RedBoxServer)