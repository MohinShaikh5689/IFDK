# libgow3gpu.a: gow3_android's GPU library (native/runtime/gpu: shadPS4's video core, shader recompiler,
# GnmDriver/VideoOut and the gow3 shims) for iOS, built inside FEX's tree next to gow3core so that it
# shares FEX's fmt and xxhash. Differences from gpu/CMakeLists.txt (the PC/Android build):
# - no FSR 3/4 (ios/ffx_stub.c fails their calls; the upscaler falls back as on unsupported GPUs);
# - no FFmpeg yet: AvPlayer is a stub (ios/avplayer_stub.cpp), gow3_png decodes with ImageIO;
# - the window is the app's CAMetalLayer (ios/window_ios.mm) instead of shim/window.cpp; SDL3 is
#   linked as a library only (no SDL video), for the overlay's event types and later audio/pads;
# - no Zydis/xbyak: the x86-64 SRT walker JIT is not built on ARM64 (it interprets the walkers).
set(GOW3_GPU "${GOW3_NATIVE}/runtime/gpu")
find_program(GLSLANG_COMPILER NAMES glslang glslangValidator REQUIRED)
# OBJCXX, sirit and miniz: gow3gpu_deps.cmake (deferred code cannot enable languages or add directories).

# Host shaders -> headers (the same list and scheme as gpu/CMakeLists.txt).
set(GOW3_SHADER_SRC "${GOW3_GPU}/shadps4/video_core/host_shaders")
set(GOW3_SHADER_INCLUDE "${CMAKE_BINARY_DIR}/gow3gpu/include")
set(GOW3_SHADER_DIR "${GOW3_SHADER_INCLUDE}/video_core/host_shaders")
set(GOW3_SHADER_HEADERS "")
function(gow3_host_shader FILENAME SHADER_NAME)
    set(OUT "${GOW3_SHADER_DIR}/${SHADER_NAME}.h")
    string(TOUPPER ${SHADER_NAME} SYMBOL)
    set(DEFINES ${ARGN})
    list(TRANSFORM DEFINES PREPEND "-D")
    add_custom_command(OUTPUT "${OUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${GOW3_SHADER_DIR}"
        COMMAND ${GLSLANG_COMPILER} --target-env vulkan1.3 -o "${OUT}" --vn ${SYMBOL} ${DEFINES} "${GOW3_SHADER_SRC}/${FILENAME}"
        COMMAND ${CMAKE_COMMAND} "-DHEADER_FILE=${OUT}" -P "${GOW3_SHADER_SRC}/AddShaderHeaderInclude.cmake"
        DEPENDS "${GOW3_SHADER_SRC}/${FILENAME}" VERBATIM)
    set(GOW3_SHADER_HEADERS ${GOW3_SHADER_HEADERS} "${OUT}" PARENT_SCOPE)
endfunction()
gow3_host_shader(color_to_ms_depth.frag color_to_ms_depth_frag)
gow3_host_shader(color_to_ms_depth.frag color_to_ms_depth_fallback_frag SAMPLE_FALLBACK=1)
gow3_host_shader(ms_image_blit.frag ms_image_blit_frag)
gow3_host_shader(ms_image_blit.frag ms_image_blit_msaa_frag SRC_MSAA=1)
gow3_host_shader(fault_buffer_process.comp fault_buffer_process_comp)
gow3_host_shader(fs_tri.vert fs_tri_vert)
gow3_host_shader(fsr.comp fsr_easu_comp SAMPLE_EASU=1)
gow3_host_shader(fsr.comp fsr_rcas_comp SAMPLE_RCAS=1)
gow3_host_shader(post_process.frag post_process_frag)
gow3_host_shader(camera_motion.comp camera_motion_comp)
gow3_host_shader(upscale_merge.comp upscale_merge_comp)
gow3_host_shader(upscale_reactive.comp upscale_reactive_comp)
gow3_host_shader(taa.comp taa_comp)
gow3_host_shader(taa_sharpen.comp taa_sharpen_comp)
gow3_host_shader(taa_sharpen.comp taa_sharpen_ldr_comp LDR_OUTPUT=1)
gow3_host_shader(buffer_multi_copy.comp buffer_multi_copy_comp)
gow3_host_shader(depth_resample.frag depth_resample_frag)
gow3_host_shader(depth_resample.frag depth_stencil_resample_frag HAS_STENCIL=1)
gow3_host_shader(depth_resample.frag depth_stencil_bits_frag STENCIL_BITS=1)
gow3_host_shader(camera_motion.comp camera_motion_debug_comp DEBUG_OVERLAY=1)
foreach(BPP IN ITEMS 8 16 32 64 96 128)
    gow3_host_shader(tiling.comp tiling_micro_${BPP}_comp BITS_PER_PIXEL=${BPP} IS_MACRO_TILED=0)
    gow3_host_shader(tiling.comp tiling_macro_${BPP}_comp BITS_PER_PIXEL=${BPP} IS_MACRO_TILED=1)
endforeach()
add_custom_target(gow3gpu_host_shaders DEPENDS ${GOW3_SHADER_HEADERS})

file(GLOB_RECURSE GOW3_VIDEO_CORE CONFIGURE_DEPENDS "${GOW3_GPU}/shadps4/video_core/*.cpp")
file(GLOB_RECURSE GOW3_RECOMPILER CONFIGURE_DEPENDS "${GOW3_GPU}/shadps4/shader_recompiler/*.cpp")
list(FILTER GOW3_VIDEO_CORE EXCLUDE REGEX "renderdoc\\.cpp$")
file(GLOB GOW3_GPU_SHIM CONFIGURE_DEPENDS "${GOW3_GPU}/shim/*.cpp" "${GOW3_GPU}/shim/*/*.cpp")
list(FILTER GOW3_GPU_SHIM EXCLUDE REGEX "/shim/window\\.cpp$")
add_library(gow3gpu STATIC
    ${GOW3_VIDEO_CORE} ${GOW3_RECOMPILER} ${GOW3_GPU_SHIM}
    "${GOW3_GPU}/shadps4/common/error.cpp"
    "${GOW3_GPU}/shadps4/common/io_file.cpp"
    "${GOW3_GPU}/shadps4/common/native_clock.cpp"
    "${GOW3_GPU}/shadps4/common/number_utils.cpp"
    "${GOW3_GPU}/shadps4/common/path_util.cpp"
    "${GOW3_GPU}/shadps4/common/rdtsc.cpp"
    "${GOW3_GPU}/shadps4/common/spin_lock.cpp"
    "${GOW3_GPU}/shadps4/common/recursive_lock.cpp"
    "${GOW3_GPU}/shadps4/common/string_util.cpp"
    "${GOW3_GPU}/shadps4/common/thread.cpp"
    "${GOW3_GPU}/shadps4/common/signal_context.cpp"
    "${GOW3_GPU}/shadps4/core/libraries/gnmdriver/gnmdriver.cpp"
    "${GOW3_GPU}/shadps4/core/libraries/videoout/driver.cpp"
    "${GOW3_GPU}/shadps4/core/libraries/videoout/video_out.cpp"
    "${GOW3_GPU}/shadps4/core/libraries/kernel/equeue.cpp"
    "${GOW3_GPU}/third_party/imgui/imgui.cpp"
    "${GOW3_GPU}/third_party/imgui/imgui_draw.cpp"
    "${GOW3_GPU}/third_party/imgui/imgui_tables.cpp"
    "${GOW3_GPU}/third_party/imgui/imgui_widgets.cpp"
    "${GOW3_GPU}/third_party/imgui/backends/imgui_impl_vulkan.cpp"
    "${GOW3_GPU}/ios/ffx_stub.c"
    "${GOW3_GPU}/ios/avplayer_stub.cpp"
    "${GOW3_GPU}/ios/window_ios.mm")
add_dependencies(gow3gpu gow3gpu_host_shaders)
set_target_properties(gow3gpu PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON)
target_include_directories(gow3gpu PRIVATE
    "${GOW3_GPU}/shim" "${GOW3_GPU}/shadps4" "${GOW3_SHADER_INCLUDE}"
    "${GOW3_GPU}/third_party/gcn/include" "${GOW3_GPU}/third_party/half/include"
    "${GOW3_GPU}/third_party/imgui" "${GOW3_GPU}/third_party/imgui/backends"
    "${GOW3_GPU}/third_party/magic_enum/include" "${GOW3_GPU}/third_party/magic_enum/include/magic_enum"
    "${GOW3_GPU}/ios/ffx"
    "${GOW3_NATIVE}/include"
    "${GOW3_NATIVE}/third_party/VulkanMemoryAllocator/include"
    "${GOW3_NATIVE}/third_party/robin-map/include"
    "${GOW3_NATIVE}/third_party/Vulkan-Headers/include" # v1.4.344 as in gow3_android; FEX's is older
    "${GOW3_BOOST_INCLUDE}"
    "${GOW3_SDL3_INCLUDE}")
target_compile_definitions(gow3gpu PRIVATE BOOST_ASIO_STANDALONE NDEBUG IMGUI_IMPL_VULKAN_NO_PROTOTYPES
    GOW3_FONT_PATH="${GOW3_GPU}/third_party/fonts/DejaVuSans.ttf")
set_source_files_properties("${GOW3_GPU}/shim/gow3_overlay.cpp" PROPERTIES
    OBJECT_DEPENDS "${GOW3_GPU}/third_party/fonts/DejaVuSans.ttf")
target_compile_options(gow3gpu PRIVATE -fno-omit-frame-pointer -Wno-deprecated-declarations
    -Wno-missing-field-initializers $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
target_link_libraries(gow3gpu PRIVATE fmt::fmt xxHash::xxhash miniz sirit)
