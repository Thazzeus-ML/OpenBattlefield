# Benchmark harness: a single executable with named workloads (see Docs/benchmarking.md).
add_executable(ob_bench ObBench.cpp ObBenchSupport.h)
target_link_libraries(ob_bench PRIVATE OpenBattlefieldCore ob_warnings)
target_include_directories(ob_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ob_bench PRIVATE
  OB_GIT_COMMIT="${OB_GIT_COMMIT}"
  OB_BUILD_TYPE="$<CONFIG>"
  OB_CXX_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
  OB_CXX_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
  OB_CXX_STANDARD="${OB_CXX_STANDARD}")
if(WIN32)
  target_link_libraries(ob_bench PRIVATE psapi)
endif()

# Smoke run used by CI: tiny workloads, verifies the harness itself and the correctness checks.
add_test(NAME ob_bench_smoke
  COMMAND ob_bench --workload all --preset smoke --out ${CMAKE_BINARY_DIR}/bench-smoke.json)
