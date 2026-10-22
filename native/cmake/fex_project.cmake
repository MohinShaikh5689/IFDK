# Included by FEX's project() (CMAKE_PROJECT_FEX_INCLUDE, see scripts/build-ios.sh): FEX's CMake
# files expect to be the top-level project, so gow3core is added to FEX's build once all of its
# targets exist (the same approach as gow3_android's cpu/fex_project.cmake).
cmake_language(EVAL CODE "cmake_language(DEFER DIRECTORY \"${CMAKE_SOURCE_DIR}\" CALL include \"${CMAKE_CURRENT_LIST_DIR}/gow3core.cmake\")")

# Not deferred: what the GPU library needs that deferred code cannot do.
include("${CMAKE_CURRENT_LIST_DIR}/gow3gpu_deps.cmake")
