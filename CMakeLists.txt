cmake_minimum_required(VERSION 3.24)
project(NearMemoryCompute VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(NMC_BUILD_TESTS "Build tests" ON)
option(NMC_BUILD_EXAMPLES "Build examples" ON)
option(NMC_BUILD_BENCHMARKS "Build benchmarks" ON)
option(NMC_BUILD_CUDA "Build the optional CUDA backend" ON)
option(NMC_ASAN "Instrument with AddressSanitizer (Win32 only)" OFF)
option(NMC_BUILD_CLI "Build the inspection CLI" ON)

if(NOT MSVC)
  message(FATAL_ERROR "Near-Memory Compute currently builds only on MSVC/Windows.")
endif()

set(NMC_WARNINGS /W4 /WX /EHsc /permissive-)

add_library(near_memory_compute STATIC
  src/backend.cpp
  src/coordinator.cpp
  src/net.cpp
  src/persistence.cpp
  src/system_discovery.cpp
)
add_library(nmc::nmc ALIAS near_memory_compute)

target_include_directories(near_memory_compute PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_compile_features(near_memory_compute PUBLIC cxx_std_20)
target_compile_options(near_memory_compute PRIVATE ${NMC_WARNINGS})
target_link_libraries(near_memory_compute PUBLIC ws2_32 dxgi)

if(NMC_ASAN)
  target_compile_options(near_memory_compute PRIVATE /fsanitize=address /Zi)
  target_link_options(near_memory_compute PRIVATE /fsanitize=address)
endif()

if(NMC_BUILD_CUDA)
  enable_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    message(STATUS "CUDA compiler: ${CMAKE_CUDA_COMPILER}")
    add_library(near_memory_compute_cuda STATIC src/cuda_backend.cu)
    target_include_directories(near_memory_compute_cuda PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(near_memory_compute_cuda PUBLIC cxx_std_20)
    target_compile_options(near_memory_compute_cuda PRIVATE -Xcompiler=/W4 -Xcompiler=/WX -Xcompiler=/EHsc)
    target_link_libraries(near_memory_compute_cuda PUBLIC cudart)
  endif()
endif()

if(NMC_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(NMC_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(NMC_BUILD_BENCHMARKS)
  add_subdirectory(benchmark)
endif()
if(NMC_BUILD_CLI)
  add_executable(nmc-cli apps/nmc_cli.cpp)
  target_compile_features(nmc-cli PRIVATE cxx_std_20)
  target_compile_options(nmc-cli PRIVATE ${NMC_WARNINGS})
  target_link_libraries(nmc-cli PRIVATE near_memory_compute)
endif()

add_executable(nmc-coordinator apps/nmc_coordinator.cpp)
target_compile_features(nmc-coordinator PRIVATE cxx_std_20)
target_compile_options(nmc-coordinator PRIVATE ${NMC_WARNINGS})
target_link_libraries(nmc-coordinator PRIVATE near_memory_compute)


add_executable(nmc-worker apps/nmc_worker.cpp)
target_compile_features(nmc-worker PRIVATE cxx_std_20)
target_compile_options(nmc-worker PRIVATE ${NMC_WARNINGS})
target_link_libraries(nmc-worker PRIVATE near_memory_compute)

# ---- Installation / package (find_package(NearMemoryCompute CONFIG REQUIRED)). ----
set_target_properties(near_memory_compute PROPERTIES EXPORT_NAME nmc)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS near_memory_compute EXPORT NearMemoryComputeTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT NearMemoryComputeTargets
        FILE NearMemoryComputeTargets.cmake
        NAMESPACE NearMemoryCompute::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NearMemoryCompute)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/NearMemoryComputeConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/NearMemoryComputeConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NearMemoryCompute)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/NearMemoryComputeConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/NearMemoryComputeConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/NearMemoryComputeConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/NearMemoryCompute)

