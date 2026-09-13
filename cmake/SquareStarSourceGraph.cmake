# First-party source ownership is directory based. New files are picked up by
# CMake automatically, so adding a .cpp/.hpp does not require editing this file.

get_filename_component(SQUARESTAR_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

file(GLOB SQUARESTAR_SHELL_SOURCES CONFIGURE_DEPENDS
    "${SQUARESTAR_ROOT}/src/modules/*.cpp"
)
list(APPEND SQUARESTAR_SHELL_SOURCES
    "${SQUARESTAR_ROOT}/src/benchmark_runner.cpp"
)

file(GLOB SQUARESTAR_SHELL_HEADERS CONFIGURE_DEPENDS
    "${SQUARESTAR_ROOT}/src/modules/*.hpp"
)
list(APPEND SQUARESTAR_SHELL_HEADERS
    "${SQUARESTAR_ROOT}/src/benchmark_runner.hpp"
)

file(GLOB_RECURSE SQUARESTAR_COMPILED_SOURCES CONFIGURE_DEPENDS
    "${SQUARESTAR_ROOT}/src/application/*.cpp"
    "${SQUARESTAR_ROOT}/src/domain/*.cpp"
    "${SQUARESTAR_ROOT}/src/platform/*.cpp"
    "${SQUARESTAR_ROOT}/src/presentation/*.cpp"
    "${SQUARESTAR_ROOT}/src/services/*.cpp"
)

file(GLOB_RECURSE SQUARESTAR_COMPONENT_HEADERS CONFIGURE_DEPENDS
    "${SQUARESTAR_ROOT}/src/application/*.hpp"
    "${SQUARESTAR_ROOT}/src/platform/*.hpp"
    "${SQUARESTAR_ROOT}/src/platform/*.h"
    "${SQUARESTAR_ROOT}/src/presentation/*.hpp"
    "${SQUARESTAR_ROOT}/src/services/*.hpp"
)

file(GLOB_RECURSE SQUARESTAR_DOMAIN_HEADERS CONFIGURE_DEPENDS
    "${SQUARESTAR_ROOT}/src/domain/*.hpp"
)

# Windows/GLFW/curl host code is not part of the portable test library.
set(SQUARESTAR_PORTABLE_SOURCE_EXCLUSIONS
    "${SQUARESTAR_ROOT}/src/application/key_bindings.cpp"
    "${SQUARESTAR_ROOT}/src/platform/frame_pacer.cpp"
    "${SQUARESTAR_ROOT}/src/platform/glfw_runtime.cpp"
    "${SQUARESTAR_ROOT}/src/platform/gui_window_layout.cpp"
    "${SQUARESTAR_ROOT}/src/presentation/gui_renderer_context.cpp"
    "${SQUARESTAR_ROOT}/src/services/http_client.cpp"
    "${SQUARESTAR_ROOT}/src/services/network_runtime.cpp"
)
set(SQUARESTAR_PORTABLE_COMPILED_SOURCES ${SQUARESTAR_COMPILED_SOURCES})
list(REMOVE_ITEM SQUARESTAR_PORTABLE_COMPILED_SOURCES
    ${SQUARESTAR_PORTABLE_SOURCE_EXCLUSIONS}
)

list(SORT SQUARESTAR_SHELL_SOURCES)
list(SORT SQUARESTAR_SHELL_HEADERS)
list(SORT SQUARESTAR_COMPILED_SOURCES)
list(SORT SQUARESTAR_COMPONENT_HEADERS)
list(SORT SQUARESTAR_DOMAIN_HEADERS)
list(SORT SQUARESTAR_PORTABLE_COMPILED_SOURCES)
