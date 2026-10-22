# The parts of the GPU library's build (gow3gpu.cmake) that CMake does not allow in deferred code:
# languages and subdirectories. Included by fex_project.cmake right after FEX's project().
set(GOW3_GPU "${CMAKE_CURRENT_LIST_DIR}/../runtime/gpu")
enable_language(OBJCXX)
set(SIRIT_USE_SYSTEM_SPIRV_HEADERS OFF CACHE BOOL "" FORCE)
add_subdirectory("${GOW3_GPU}/third_party/sirit" "${CMAKE_BINARY_DIR}/gow3gpu/sirit" EXCLUDE_FROM_ALL)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory("${GOW3_GPU}/third_party/miniz" "${CMAKE_BINARY_DIR}/gow3gpu/miniz" EXCLUDE_FROM_ALL)
unset(CMAKE_POLICY_VERSION_MINIMUM)
