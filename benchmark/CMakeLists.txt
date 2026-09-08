add_executable(nmc_benchmark benchmark.cpp)
target_compile_features(nmc_benchmark PRIVATE cxx_std_20)
target_compile_options(nmc_benchmark PRIVATE /W4 /WX /EHsc /permissive-)
target_link_libraries(nmc_benchmark PRIVATE near_memory_compute)
