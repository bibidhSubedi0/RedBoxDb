add_executable(RedBoxBenchmark benchmark.cpp)
target_link_libraries(RedBoxBenchmark PRIVATE RedBoxDbLib)
set_project_warnings(RedBoxBenchmark)

add_executable(InsertMicro insert_micro.cpp)
target_link_libraries(InsertMicro PRIVATE RedBoxDbLib)
target_compile_options(InsertMicro PRIVATE -mavx2 -mfma)
set_project_warnings(InsertMicro)