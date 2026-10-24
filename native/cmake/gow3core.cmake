# libgow3core.a: the JIT pool, guest_cpu.h on FEXCore and the self-test, built inside FEX's tree.
set(GOW3_NATIVE "${CMAKE_CURRENT_LIST_DIR}/..")
add_library(gow3core STATIC
  "${GOW3_NATIVE}/jit/jit_pool.c"
  "${GOW3_NATIVE}/cpu/guest_cpu_fex_darwin.cpp"
  "${GOW3_NATIVE}/cpu/host_features_darwin.cpp"
  "${GOW3_NATIVE}/cpu/host_call_arm64.S"
  "${GOW3_NATIVE}/selftest/selftest.cpp"
  "${GOW3_NATIVE}/gpu/screen_test.c")
target_include_directories(gow3core PRIVATE "${GOW3_NATIVE}/include" "${GOW3_NATIVE}/cpu" "${CMAKE_BINARY_DIR}/generated")
# MoltenVK's Vulkan headers (scripts/build-ios.sh downloads MoltenVK and passes the path).
set_source_files_properties("${GOW3_NATIVE}/gpu/screen_test.c" PROPERTIES
  INCLUDE_DIRECTORIES "${GOW3_MOLTENVK_INCLUDE}")

# The gow3 runtime and loader (native/runtime, C) with SDL3 (audio, pads) and FFmpeg (video, MP3) from
# build-ios.sh; the GPU library is gow3gpu (gow3gpu.cmake). gow3_start (ios/gow3_start.c) runs the loader on a thread.
set(GOW3_RUNTIME "${GOW3_NATIVE}/runtime")
file(GLOB GOW3_ATRAC9 "${GOW3_RUNTIME}/third_party/LibAtrac9/C/src/*.c")
add_library(gow3runtime STATIC
  "${GOW3_RUNTIME}/src/probe.c"
  "${GOW3_RUNTIME}/src/runtime.c"
  "${GOW3_RUNTIME}/src/runtime_ajm.c"
  "${GOW3_RUNTIME}/src/runtime_content.c"
  "${GOW3_RUNTIME}/src/runtime_cpu.c"
  "${GOW3_RUNTIME}/src/runtime_file.c"
  "${GOW3_RUNTIME}/src/runtime_kernel.c"
  "${GOW3_RUNTIME}/src/runtime_memory.c"
  "${GOW3_RUNTIME}/src/runtime_mutex.c"
  "${GOW3_RUNTIME}/src/runtime_audio.c"
  "${GOW3_RUNTIME}/src/runtime_pad.c"
  "${GOW3_RUNTIME}/src/runtime_mp3.c"
  "${GOW3_RUNTIME}/src/runtime_videodec.c"
  "${GOW3_RUNTIME}/src/runtime_rtc.c"
  "${GOW3_RUNTIME}/src/runtime_rwlock.c"
  "${GOW3_RUNTIME}/src/runtime_savedata.c"
  "${GOW3_RUNTIME}/src/runtime_sema.c"
  "${GOW3_RUNTIME}/src/runtime_services.c"
  "${GOW3_RUNTIME}/src/runtime_tilecheck.c"
  "${GOW3_RUNTIME}/src/runtime_thread.c"
  "${GOW3_RUNTIME}/src/runtime_time.c"
  "${GOW3_RUNTIME}/src/compat_darwin.c"
  "${GOW3_RUNTIME}/src/cpu_check.c"
  "${GOW3_RUNTIME}/src/isa_rewrite.c"
  "${GOW3_RUNTIME}/src/isa_emulate.c"
  "${GOW3_RUNTIME}/ios/gow3_start.c"
  "${GOW3_RUNTIME}/src/vulkan_smoke.c"
  ${GOW3_ATRAC9})
target_include_directories(gow3runtime PRIVATE "${GOW3_RUNTIME}" "${GOW3_RUNTIME}/src" "${GOW3_SDL3_INCLUDE}"
  "${GOW3_FFMPEG_INCLUDE}")
set_source_files_properties(${GOW3_ATRAC9} PROPERTIES COMPILE_OPTIONS "-w")
set_source_files_properties("${GOW3_RUNTIME}/src/vulkan_smoke.c" PROPERTIES INCLUDE_DIRECTORIES "${GOW3_MOLTENVK_INCLUDE}")
set_target_properties(gow3runtime PROPERTIES C_STANDARD 11)
# JemallocLibs holds FEXCore::Allocator::malloc/free/... (Utils/AllocatorHooks.cpp; libc-backed on Apple).
target_link_libraries(gow3core PRIVATE FEXCore FEXCore_Base JemallocLibs fmt::fmt)
target_compile_options(gow3core PRIVATE ${FEX_TUNE_COMPILE_FLAGS})
set_target_properties(gow3core PROPERTIES
  CXX_STANDARD 20
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN TRUE)

# The GPU library (native/runtime/gpu) for iOS.
include("${CMAKE_CURRENT_LIST_DIR}/gow3gpu.cmake")
