# libgow3core.a: the JIT pool, guest_cpu.h on FEXCore and the self-test, built inside FEX's tree.
set(GOW3_NATIVE "${CMAKE_CURRENT_LIST_DIR}/..")
add_library(gow3core STATIC
  "${GOW3_NATIVE}/jit/jit_pool.c"
  "${GOW3_NATIVE}/cpu/guest_cpu_fex_darwin.cpp"
  "${GOW3_NATIVE}/cpu/host_features_darwin.cpp"
  "${GOW3_NATIVE}/cpu/host_call_arm64.S"
  "${GOW3_NATIVE}/selftest/selftest.cpp")
target_include_directories(gow3core PRIVATE "${GOW3_NATIVE}/include" "${GOW3_NATIVE}/cpu" "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(gow3core PRIVATE FEXCore FEXCore_Base fmt::fmt)
target_compile_options(gow3core PRIVATE ${FEX_TUNE_COMPILE_FLAGS})
set_target_properties(gow3core PROPERTIES
  CXX_STANDARD 20
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN TRUE)
