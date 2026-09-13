option(SQUARESTAR_PORTABLE_ONLY
    "Configure only portable component tests on non-Windows hosts" OFF)
option(SQUARESTAR_BUILD_TESTS
    "Build and register SquareStar's portable domain/component tests" OFF)
if(SQUARESTAR_PORTABLE_ONLY)
    enable_testing()
    add_subdirectory(tests)
    return()
endif()

if(NOT WIN32)
    message(FATAL_ERROR "SquareStar currently supports Windows only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
set(CMAKE_MSVC_RUNTIME_LIBRARY
    "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING "Use the static MSVC runtime for a portable executable" FORCE)

# MinGW release builds let the linker discard unused functions/data. LTO stays
# optional on every toolchain; some MinGW distributions (including some
# w64devkit builds) ship GCC without the linker plugin required by -flto.
option(SQUARESTAR_ENABLE_LTO
    "Enable link-time optimization when the compiler supports it" OFF)
option(SQUARESTAR_BALANCED_SIZE
    "Reduce MinGW Release COFF metadata by discarding local symbols while retaining global symbols" OFF)
option(SQUARESTAR_STRIP_DEBUG
    "Strip only debug information from MinGW Release executables" OFF)
option(SQUARESTAR_STRIP_BINARY
    "Explicitly select full MinGW Release stripping (full strip is also the default unless another metadata mode is selected)" OFF)
set(SQUARESTAR_METADATA_MODE_COUNT 0)
foreach(SQUARESTAR_METADATA_MODE
        SQUARESTAR_BALANCED_SIZE SQUARESTAR_STRIP_DEBUG SQUARESTAR_STRIP_BINARY)
    if(${SQUARESTAR_METADATA_MODE})
        math(EXPR SQUARESTAR_METADATA_MODE_COUNT "${SQUARESTAR_METADATA_MODE_COUNT} + 1")
    endif()
endforeach()
if(SQUARESTAR_METADATA_MODE_COUNT GREATER 1)
    message(FATAL_ERROR
        "SQUARESTAR_BALANCED_SIZE, SQUARESTAR_STRIP_DEBUG, and SQUARESTAR_STRIP_BINARY are mutually exclusive.")
endif()
set(SQUARESTAR_OPTIMIZATION "O2" CACHE STRING
    "Release optimization level: O1, O2, O3, or Os")
set_property(CACHE SQUARESTAR_OPTIMIZATION PROPERTY STRINGS O1 O2 O3 Os)
string(TOUPPER "${SQUARESTAR_OPTIMIZATION}" SQUARESTAR_OPTIMIZATION_NORMALIZED)
if(NOT SQUARESTAR_OPTIMIZATION_NORMALIZED MATCHES "^(O1|O2|O3|OS)$")
    message(FATAL_ERROR
        "SQUARESTAR_OPTIMIZATION must be O1, O2, O3, or Os "
        "(received '${SQUARESTAR_OPTIMIZATION}').")
endif()
if(SQUARESTAR_BALANCED_SIZE AND NOT SQUARESTAR_OPTIMIZATION_NORMALIZED STREQUAL "OS")
    message(FATAL_ERROR
        "SQUARESTAR_BALANCED_SIZE requires SQUARESTAR_OPTIMIZATION=Os.")
endif()
option(SQUARESTAR_STRICT_WARNINGS
    "Enable capability-checked warnings-as-errors for first-party sources" OFF)
option(SQUARESTAR_USE_PCH
    "Use precompiled headers for first-party Windows targets" ON)
set(SQUARESTAR_DEPENDENCY_CACHE_DIR "" CACHE PATH
    "Optional persistent FetchContent cache shared by SquareStar source copies")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SquareStarWarnings)

if(MINGW)
    add_compile_options(
        $<$<CONFIG:Release>:-pipe>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
    )
    add_link_options(
        $<$<CONFIG:Release>:-Wl,--gc-sections>
    )
    if(SQUARESTAR_BALANCED_SIZE)
        # Apply -Os at directory scope so fetched static dependencies inherit the
        # size-oriented optimization too. --discard-all removes local COFF
        # symbols while preserving global symbols; unlike -s, it does not erase
        # the symbol table wholesale.
        add_compile_options(
            $<$<CONFIG:Release>:-Os>
        )
        add_link_options(
            $<$<CONFIG:Release>:-Wl,--discard-all>
        )
        message(STATUS
            "MinGW balanced size enabled (-Os + --discard-all; global COFF symbols retained)")
    elseif(SQUARESTAR_STRIP_DEBUG)
        add_link_options(
            $<$<CONFIG:Release>:-Wl,--strip-debug>
        )
        message(STATUS
            "MinGW debug-only stripping enabled (keeps ordinary linker/symbol metadata)")
    else()
        # Produce the release executable without unnecessary debug/symbol
        # metadata instead of creating an unstripped intermediate first.
        add_link_options(
            $<$<CONFIG:Release>:-s>
        )
        if(SQUARESTAR_STRIP_BINARY)
            message(STATUS "MinGW full executable stripping enabled explicitly (-s)")
        else()
            message(STATUS "MinGW full executable stripping enabled by default (-s)")
        endif()
    endif()
endif()

if(SQUARESTAR_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(
        RESULT SQUARESTAR_LTO_SUPPORTED
        OUTPUT SQUARESTAR_LTO_ERROR
        LANGUAGES C CXX
    )
    if(SQUARESTAR_LTO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        message(STATUS "LTO enabled for Release targets")
    else()
        message(FATAL_ERROR
            "LTO was explicitly requested, but this compiler/linker does not support it. "
            "Details: ${SQUARESTAR_LTO_ERROR}")
    endif()
else()
    message(STATUS "LTO disabled for faster, broadly compatible builds")
endif()

if(SQUARESTAR_DEPENDENCY_CACHE_DIR)
    file(MAKE_DIRECTORY "${SQUARESTAR_DEPENDENCY_CACHE_DIR}")
    set(FETCHCONTENT_BASE_DIR "${SQUARESTAR_DEPENDENCY_CACHE_DIR}" CACHE PATH
        "SquareStar persistent FetchContent cache" FORCE)
    message(STATUS "SquareStar dependency cache: ${FETCHCONTENT_BASE_DIR}")
endif()
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
    "Pinned SquareStar dependencies do not need update checks" FORCE)
include(FetchContent)

# Third-party headers must not participate in SquareStar's -Werror policy.
# Newer GCC versions can diagnose source formatting inside dependency headers.
function(squarestar_mark_dependency_system target_name)
    if(NOT TARGET "${target_name}")
        return()
    endif()

    get_target_property(_squarestar_aliased_target
        "${target_name}" ALIASED_TARGET)
    if(_squarestar_aliased_target)
        set(_squarestar_real_target "${_squarestar_aliased_target}")
    else()
        set(_squarestar_real_target "${target_name}")
    endif()

    if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.25")
        set_property(TARGET "${_squarestar_real_target}" PROPERTY SYSTEM TRUE)
    else()
        get_target_property(_squarestar_dependency_includes
            "${_squarestar_real_target}" INTERFACE_INCLUDE_DIRECTORIES)
        if(_squarestar_dependency_includes)
            set_property(TARGET "${_squarestar_real_target}" APPEND PROPERTY
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
                "${_squarestar_dependency_includes}")
        endif()
    endif()
endfunction()


# GLFW 3.5.1. MinGW users may point SQUARESTAR_GLFW_BIN_DIR at the root of
# GLFW's official 64-bit Windows binary archive. Other builds fetch source.
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(SQUARESTAR_GLFW_BIN_DIR "" CACHE PATH
    "Root of the official glfw-3.5.1.bin.WIN64 archive for an offline MinGW build")
if(MINGW AND SQUARESTAR_GLFW_BIN_DIR)
    file(TO_CMAKE_PATH "${SQUARESTAR_GLFW_BIN_DIR}" SQUARESTAR_GLFW_BIN_DIR_CMAKE)
    set(SQUARESTAR_GLFW_INCLUDE
        "${SQUARESTAR_GLFW_BIN_DIR_CMAKE}/include")
    set(SQUARESTAR_GLFW_LIBRARY
        "${SQUARESTAR_GLFW_BIN_DIR_CMAKE}/lib-mingw-w64/libglfw3.a")
    if(NOT EXISTS "${SQUARESTAR_GLFW_INCLUDE}/GLFW/glfw3.h")
        message(FATAL_ERROR
            "SQUARESTAR_GLFW_BIN_DIR does not contain include/GLFW/glfw3.h")
    endif()
    if(NOT EXISTS "${SQUARESTAR_GLFW_LIBRARY}")
        message(FATAL_ERROR
            "SQUARESTAR_GLFW_BIN_DIR does not contain lib-mingw-w64/libglfw3.a")
    endif()
    add_library(glfw STATIC IMPORTED GLOBAL)
    set_target_properties(glfw PROPERTIES
        IMPORTED_LOCATION "${SQUARESTAR_GLFW_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${SQUARESTAR_GLFW_INCLUDE}"
    )
else()
    # Download the pinned GLFW release archive over HTTPS instead of cloning
    # the repository. This keeps a normal build independent of git.exe.
    FetchContent_Declare(
        glfw
        URL https://github.com/glfw/glfw/archive/3.5.1.tar.gz
        URL_HASH SHA256=5234f4f29473e9a06bc7847d8371858dd135d38466eeeaa652fdc9f8f9ff0c20
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(glfw)
endif()

squarestar_mark_dependency_system(glfw)

# curl 8.22.0, built statically with the Windows Schannel TLS backend.
# Fast MinGW builds can point at the exact directory containing CURLConfig.cmake
# so a clean SquareStar build tree does not need to rebuild libcurl.
set(SQUARESTAR_CURL_PACKAGE_DIR "" CACHE PATH
    "Optional directory containing CURLConfig.cmake")
if(SQUARESTAR_CURL_PACKAGE_DIR)
    if(NOT EXISTS "${SQUARESTAR_CURL_PACKAGE_DIR}/CURLConfig.cmake"
       AND NOT EXISTS "${SQUARESTAR_CURL_PACKAGE_DIR}/curl-config.cmake")
        message(FATAL_ERROR
            "SQUARESTAR_CURL_PACKAGE_DIR does not contain a curl CMake package: "
            "${SQUARESTAR_CURL_PACKAGE_DIR}")
    endif()
    set(CURL_DIR "${SQUARESTAR_CURL_PACKAGE_DIR}" CACHE PATH
        "Directory containing the cached curl CMake package" FORCE)
    find_package(CURL CONFIG REQUIRED)
    message(STATUS
        "Using cached curl package: ${SQUARESTAR_CURL_PACKAGE_DIR}")
else()
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE BOOL "" FORCE)
    set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    # Use curl's official release source archive over HTTPS. CMake verifies the
    # pinned SHA-256, so git.exe is not required for dependency population.
    FetchContent_Declare(
        curl
        URL https://curl.se/download/curl-8.22.0.tar.gz
        URL_HASH SHA256=d54dd598bf05927a726deb38df31c6a255ba83ff1de57c5d1464dac3ed8f44a1
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(curl)
endif()

squarestar_mark_dependency_system(CURL::libcurl)



set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")
set(IMPLOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/implot")
set(YYJSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/yyjson")

add_library(squarestar_imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
    "${IMPLOT_DIR}/implot.cpp"
    "${IMPLOT_DIR}/implot_items.cpp"
)
target_include_directories(squarestar_imgui SYSTEM PUBLIC
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    "${IMPLOT_DIR}"
)
target_compile_definitions(squarestar_imgui PRIVATE
    IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
)
target_link_libraries(squarestar_imgui PUBLIC glfw d3d11 dxgi)

add_library(squarestar_yyjson STATIC
    "${YYJSON_DIR}/yyjson.c"
)
target_include_directories(squarestar_yyjson SYSTEM PUBLIC "${YYJSON_DIR}")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(SQUARESTAR_ASSET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(SQUARESTAR_LICENSE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/licenses")
set(SQUARESTAR_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/assets/squarestar.ico")
set(SQUARESTAR_MANIFEST_PATH "${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest")
set(SQUARESTAR_PROJECT_LICENSE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(SQUARESTAR_THIRD_PARTY_NOTICES_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
set(SQUARESTAR_ASSET_CREDITS_PATH "${CMAKE_CURRENT_SOURCE_DIR}/ASSET_CREDITS.md")
set(SQUARESTAR_PRIVACY_NOTICE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/PRIVACY.md")
set(SQUARESTAR_DATA_PROVIDER_NOTICE_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/DATA_PROVIDER_NOTICE.md")

# MSVC's linker generates and embeds its own application manifest by default.
# Embedding app.manifest in app.rc as RT_MANIFEST/1 as well creates two manifest
# resources and fails in CVTRES with CVT1100.  Keep the RC manifest for MinGW,
# but feed it to link.exe as a manifest input on MSVC so mt.exe merges it once.
if(MSVC)
    set(SQUARESTAR_MANIFEST_RESOURCE "")
else()
    set(SQUARESTAR_MANIFEST_RESOURCE
        "1 24 \"${SQUARESTAR_MANIFEST_PATH}\"")
endif()

function(squarestar_require_wav_sound stem output_var)
    set(_asset "${SQUARESTAR_ASSET_DIR}/${stem}.wav")
    if(NOT EXISTS "${_asset}")
        message(FATAL_ERROR "Missing sound asset: ${stem}.wav")
    endif()
    set(${output_var} "${_asset}" PARENT_SCOPE)
endfunction()

squarestar_require_wav_sound(key SQUARESTAR_SOUND_KEY)
squarestar_require_wav_sound(click SQUARESTAR_SOUND_CLICK)
squarestar_require_wav_sound(decline SQUARESTAR_SOUND_DECLINE)
squarestar_require_wav_sound(error SQUARESTAR_SOUND_ERROR)
squarestar_require_wav_sound(gain SQUARESTAR_SOUND_GAIN)
squarestar_require_wav_sound(launch SQUARESTAR_SOUND_LAUNCH)
squarestar_require_wav_sound(loading SQUARESTAR_SOUND_LOADING)
squarestar_require_wav_sound(loss SQUARESTAR_SOUND_LOSS)
squarestar_require_wav_sound(marketopenclose SQUARESTAR_SOUND_MARKET_OPEN_CLOSE)
squarestar_require_wav_sound(off SQUARESTAR_SOUND_OFF)
squarestar_require_wav_sound(on SQUARESTAR_SOUND_ON)
squarestar_require_wav_sound(transition SQUARESTAR_SOUND_TRANSITION)

set(SQUARESTAR_EMBEDDED_ASSETS
    "${SQUARESTAR_SOUND_KEY}"
    "${SQUARESTAR_SOUND_CLICK}"
    "${SQUARESTAR_SOUND_DECLINE}"
    "${SQUARESTAR_SOUND_ERROR}"
    "${SQUARESTAR_SOUND_GAIN}"
    "${SQUARESTAR_SOUND_LAUNCH}"
    "${SQUARESTAR_SOUND_LOADING}"
    "${SQUARESTAR_SOUND_LOSS}"
    "${SQUARESTAR_SOUND_MARKET_OPEN_CLOSE}"
    "${SQUARESTAR_SOUND_OFF}"
    "${SQUARESTAR_SOUND_ON}"
    "${SQUARESTAR_SOUND_TRANSITION}"
    "${SQUARESTAR_ASSET_DIR}/Outfit-Bold.ttf"
    "${SQUARESTAR_ASSET_DIR}/Outfit-Medium.ttf"
    "${SQUARESTAR_ASSET_DIR}/Outfit-Regular.ttf"
    "${SQUARESTAR_ASSET_DIR}/Outfit-SemiBold.ttf"
)
set(SQUARESTAR_EMBEDDED_DOCUMENTS
    "${SQUARESTAR_PROJECT_LICENSE_PATH}"
    "${SQUARESTAR_THIRD_PARTY_NOTICES_PATH}"
    "${SQUARESTAR_ASSET_CREDITS_PATH}"
    "${SQUARESTAR_PRIVACY_NOTICE_PATH}"
    "${SQUARESTAR_DATA_PROVIDER_NOTICE_PATH}"
    "${SQUARESTAR_LICENSE_DIR}/Dear-ImGui.txt"
    "${SQUARESTAR_LICENSE_DIR}/ImPlot.txt"
    "${SQUARESTAR_LICENSE_DIR}/yyjson.txt"
    "${SQUARESTAR_LICENSE_DIR}/GLFW.txt"
    "${SQUARESTAR_LICENSE_DIR}/curl.txt"
    "${SQUARESTAR_LICENSE_DIR}/Outfit-OFL-1.1.txt"
    "${SQUARESTAR_LICENSE_DIR}/CC-BY-4.0.txt"
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc"
    @ONLY
)
set_source_files_properties(
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc"
    PROPERTIES OBJECT_DEPENDS
        "${SQUARESTAR_EMBEDDED_ASSETS};${SQUARESTAR_EMBEDDED_DOCUMENTS};${CMAKE_CURRENT_SOURCE_DIR}/src/platform/embedded_resource_ids.h"
)

include(SquareStarSourceGraph)
set_source_files_properties(
    ${SQUARESTAR_SHELL_HEADERS}
    ${SQUARESTAR_COMPONENT_HEADERS}
    ${SQUARESTAR_DOMAIN_HEADERS}
    PROPERTIES HEADER_FILE_ONLY TRUE
)

add_library(squarestar_components STATIC
    ${SQUARESTAR_COMPILED_SOURCES}
    ${SQUARESTAR_COMPONENT_HEADERS}
    ${SQUARESTAR_DOMAIN_HEADERS}
)
target_include_directories(squarestar_components PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(squarestar_components PRIVATE
    squarestar_imgui
    squarestar_yyjson
    CURL::libcurl
    glfw
    d3d11
    dxgi
    advapi32
    comdlg32
    crypt32
    dwmapi
    gdiplus
    gdi32
    ole32
    shell32
    user32
    uuid
    winmm
    ws2_32
)

add_executable(SquareStar
    "src/SquareStar.cpp"
    "src/squarestar_pch.hpp"
    "src/squarestar_shell_pch.hpp"
    ${SQUARESTAR_SHELL_SOURCES}
    ${SQUARESTAR_SHELL_HEADERS}
    "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc"
)

if(MSVC)
    target_link_options(SquareStar PRIVATE
        /MANIFEST:EMBED
        "/MANIFESTINPUT:${SQUARESTAR_MANIFEST_PATH}"
    )
    set_property(TARGET SquareStar APPEND PROPERTY
        LINK_DEPENDS "${SQUARESTAR_MANIFEST_PATH}")
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}"
    PREFIX "Source"
    FILES
        "src/SquareStar.cpp"
        "src/squarestar_pch.hpp"
        "src/squarestar_shell_pch.hpp"
        ${SQUARESTAR_COMPILED_SOURCES}
        ${SQUARESTAR_SHELL_SOURCES}
        ${SQUARESTAR_SHELL_HEADERS}
        ${SQUARESTAR_COMPONENT_HEADERS}
        ${SQUARESTAR_DOMAIN_HEADERS}
)
source_group("Generated" FILES "${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc")
set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT SquareStar)
target_include_directories(SquareStar PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")

# The independently compiled shell controllers share a broader PCH because they
# all adapt the same stable Win32/GLFW/ImGui surface.
# Standalone components use the generic platform/library PCH and keep project
# headers out of it, preserving useful dependency granularity.
if(SQUARESTAR_USE_PCH)
    target_precompile_headers(SquareStar PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/squarestar_shell_pch.hpp>"
    )
    target_precompile_headers(squarestar_components PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src/squarestar_pch.hpp>"
    )
else()
    message(STATUS "SquareStar precompiled headers disabled for dependency verification")
endif()
target_link_libraries(SquareStar PRIVATE
    squarestar_components
    squarestar_imgui
    squarestar_yyjson
    CURL::libcurl
    glfw
    d3d11
    dxgi
    advapi32
    comdlg32
    crypt32
    dwmapi
    gdiplus
    gdi32
    ole32
    psapi
    shell32
    user32
    uuid
    winmm
    ws2_32
)
target_compile_definitions(SquareStar PRIVATE
    SQUARESTAR_BUILD_OPTIMIZATION="${SQUARESTAR_OPTIMIZATION_NORMALIZED}"
)
foreach(SQUARESTAR_FIRST_PARTY_TARGET SquareStar squarestar_components)
    target_compile_definitions(${SQUARESTAR_FIRST_PARTY_TARGET} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        UNICODE
        _UNICODE
        _WIN32_WINNT=0x0A00
    )
endforeach()

set(SQUARESTAR_RELEASE_OPTIMIZATION_FLAG "")
if(MSVC)
    if(SQUARESTAR_OPTIMIZATION_NORMALIZED STREQUAL "O1"
       OR SQUARESTAR_OPTIMIZATION_NORMALIZED STREQUAL "OS")
        set(SQUARESTAR_RELEASE_OPTIMIZATION_FLAG "/O1")
    else()
        # MSVC has no /O3. Map O3 to the compiler's maximum
        # general release optimization level, /O2.
        set(SQUARESTAR_RELEASE_OPTIMIZATION_FLAG "/O2")
    endif()
elseif(MINGW)
    if(SQUARESTAR_OPTIMIZATION_NORMALIZED STREQUAL "OS")
        set(SQUARESTAR_RELEASE_OPTIMIZATION_FLAG "-Os")
    else()
        set(SQUARESTAR_RELEASE_OPTIMIZATION_FLAG "-${SQUARESTAR_OPTIMIZATION_NORMALIZED}")
    endif()
endif()
if(SQUARESTAR_RELEASE_OPTIMIZATION_FLAG)
    foreach(SQUARESTAR_OPT_TARGET
            SquareStar squarestar_components squarestar_imgui squarestar_yyjson)
        target_compile_options(${SQUARESTAR_OPT_TARGET} PRIVATE
            "$<$<CONFIG:Release>:${SQUARESTAR_RELEASE_OPTIMIZATION_FLAG}>")
    endforeach()
    message(STATUS
        "SquareStar Release optimization: ${SQUARESTAR_OPTIMIZATION} "
        "(${SQUARESTAR_RELEASE_OPTIMIZATION_FLAG})")
endif()


if(MSVC)
    # MSBuild parallelizes projects; /MP also parallelizes the independent
    # vendored ImGui/ImPlot source files inside their static-library project.
    foreach(SQUARESTAR_MSVC_TARGET
            SquareStar squarestar_components squarestar_imgui squarestar_yyjson)
        target_compile_options(${SQUARESTAR_MSVC_TARGET} PRIVATE /MP)
    endforeach()
    target_compile_options(SquareStar PRIVATE
        /bigobj
        /Zc:__cplusplus
    )
    target_link_options(SquareStar PRIVATE
        /SUBSYSTEM:WINDOWS
        /ENTRY:mainCRTStartup
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>
    )
else()
    if(MINGW)
        target_compile_options(SquareStar PRIVATE -Wa,-mbig-obj)
        target_link_options(SquareStar PRIVATE
            -mwindows
            -static
        )
    endif()
endif()

squarestar_enable_cxx_warnings(squarestar_components)
squarestar_enable_cxx_warnings(SquareStar)

install(TARGETS SquareStar RUNTIME DESTINATION .)

if(SQUARESTAR_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
