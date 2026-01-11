add_executable(RedBoxDb)

target_sources(RedBoxDb
    PRIVATE
        main.cpp
 "engine.cpp")

target_include_directories(RedBoxDb
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

set_project_warnings(RedBoxDb)

install(TARGETS RedBoxDb
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
