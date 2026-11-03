cmake_minimum_required(VERSION 3.20)

# ── macOS universal binary (arm64 + x86_64) ───────────────────────────────────
# Deployment target 10.15 covers XP11 current user base and is XP12's minimum.
# Must come before project(): the target architectures are fixed there, and a
# value set later is ignored (CI built an arm64-only .xpl).
if(CMAKE_HOST_APPLE)
    set(CMAKE_OSX_ARCHITECTURES   "arm64;x86_64" CACHE STRING "")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15"        CACHE STRING "")
endif()

project(XP-GSI
    VERSION 0.9.0
    DESCRIPTION "Gate Static Injector for X-Plane"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Windows: static MSVC runtime ──────────────────────────────────────────────
# Link the CRT/STL statically (/MT) so the .xpl does not depend on
# MSVCP140.dll / VCRUNTIME140*.dll. Users without the matching VC++
# redistributable would otherwise get a silent load failure in X-Plane.
# Must be set before any target is created (CMP0091, CMake ≥ 3.15).
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    # Sources are UTF-8 without BOM (em dashes in comments and strings).
    add_compile_options(/utf-8)
endif()

# ── X-Plane SDK ───────────────────────────────────────────────────────────────
# Set XPSDK_DIR to the extracted SDK root (contains CHeaders/, Libraries/).
# Default: vendor/XPSDK/ next to this file's parent directory.
if(NOT DEFINED XPSDK_DIR)
    set(XPSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/XPSDK")
endif()
get_filename_component(XPSDK_DIR "${XPSDK_DIR}" ABSOLUTE)

if(NOT EXISTS "${XPSDK_DIR}/CHeaders/XPLM/XPLMPlugin.h")
    message(FATAL_ERROR
        "X-Plane SDK not found at '${XPSDK_DIR}'.\n"
        "  1. Download SDK 4.x from https://developer.x-plane.com/sdk/\n"
        "  2. Extract so vendor/XPSDK/CHeaders/XPLM/XPLMPlugin.h exists.\n"
        "  Or pass -DXPSDK_DIR=<path> to CMake.")
endif()

# ── Dependencies ──────────────────────────────────────────────────────────────
include(FetchContent)

# nlohmann/json — header-only via FetchContent
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# Dear ImGui (MIT) — in-sim window. No CMakeLists upstream, so fetch the
# sources only (SOURCE_SUBDIR points at a folder that does not exist) and
# build the core files here. Pinned to v1.91.9, the last 1.91 release: 1.92
# reworked the font/texture backend API our XPLM renderer is written against.
# No backends are used: ui/ImGuiWindow.cpp is our own XPLM integration.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  do-not-build)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp")
target_include_directories(imgui PUBLIC "${imgui_SOURCE_DIR}")
# The window has no text input, clipboard or links: drop ImGui's default
# Win32 clipboard/IME and shell helpers (and their USER32/IMM32/SHELL32 imports).
target_compile_definitions(imgui PUBLIC
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    IMGUI_DISABLE_WIN32_FUNCTIONS
    IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS)
set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(APPLE OR UNIX)
    target_compile_options(imgui PRIVATE -fvisibility=hidden)
endif()

find_package(OpenGL REQUIRED)   # fixed-function GL for the ImGui renderer

# doctest — download single header + CMake discovery module directly.
# We avoid FetchContent for doctest because its CMakeLists.txt uses
# cmake_minimum_required(VERSION 2.8), which CMake 4.x rejects.
set(DOCTEST_VERSION  "v2.4.11")
set(DOCTEST_BASE_URL "https://raw.githubusercontent.com/doctest/doctest/${DOCTEST_VERSION}")
set(DOCTEST_INC_DIR  "${CMAKE_BINARY_DIR}/doctest_dist")

file(MAKE_DIRECTORY "${DOCTEST_INC_DIR}/doctest")
if(NOT EXISTS "${DOCTEST_INC_DIR}/doctest/doctest.h")
    message(STATUS "Downloading doctest.h ${DOCTEST_VERSION}…")
    file(DOWNLOAD "${DOCTEST_BASE_URL}/doctest/doctest.h"
                  "${DOCTEST_INC_DIR}/doctest/doctest.h"
                  STATUS _dl TLS_VERIFY ON)
    list(GET _dl 0 _err)
    if(_err)
        message(FATAL_ERROR "Could not download doctest.h: ${_dl}")
    endif()
endif()
file(MAKE_DIRECTORY "${DOCTEST_INC_DIR}/cmake")
if(NOT EXISTS "${DOCTEST_INC_DIR}/cmake/doctest.cmake")
    file(DOWNLOAD "${DOCTEST_BASE_URL}/scripts/cmake/doctest.cmake"
                  "${DOCTEST_INC_DIR}/cmake/doctest.cmake"
                  STATUS _dl2 TLS_VERIFY ON)
endif()
# doctest_discover_tests references doctestAddTests.cmake in the same directory.
if(NOT EXISTS "${DOCTEST_INC_DIR}/cmake/doctestAddTests.cmake")
    file(DOWNLOAD "${DOCTEST_BASE_URL}/scripts/cmake/doctestAddTests.cmake"
                  "${DOCTEST_INC_DIR}/cmake/doctestAddTests.cmake"
                  STATUS _dl3 TLS_VERIFY ON)
endif()

add_library(doctest_hdr INTERFACE)
target_include_directories(doctest_hdr INTERFACE "${DOCTEST_INC_DIR}")
add_library(doctest::doctest ALIAS doctest_hdr)
# Expose for tests/CMakeLists.txt include() call
set(doctest_SOURCE_DIR "${DOCTEST_INC_DIR}" CACHE INTERNAL "")

# ── Source files ──────────────────────────────────────────────────────────────
set(XPGSI_SOURCES
    src/main.cpp
    src/core/Log.cpp
    src/core/Paths.cpp
    src/core/WorkQueue.cpp
    src/scenery/SceneryPacks.cpp
    src/scenery/Fingerprint.cpp
    src/scenery/AptDat.cpp
    src/renderer/StaticRenderer.cpp
    src/renderer/CslDatarefs.cpp
    src/csl/CslScanner.cpp
    src/csl/ModelSelector.cpp
    src/csl/ObjGeometry.cpp
    src/csl/ObjLightPatch.cpp
    src/data/AirportData.cpp
    src/data/AirportTypes.cpp
    src/data/WikiFallback.cpp
    src/data/RegionalBrands.cpp
    src/data/StandOverrides.cpp
    src/csl/RelatedTypes.cpp
    src/engine/TerminalZones.cpp
    src/engine/AutoStart.cpp
    src/engine/ConflictEngine.cpp
    src/engine/Density.cpp
    src/engine/GateOccupancy.cpp
    src/engine/Planner.cpp
    src/engine/StandLayout.cpp
    src/engine/StandEvidence.cpp
    src/engine/Reservations.cpp
    src/engine/GateSuggest.cpp
    src/core/Settings.cpp
    src/core/CduAdapters.cpp
    src/app/ArrivalGate.cpp
    src/ui/ImGuiWindow.cpp
    src/ui/MainWindow.cpp
    src/ui/Theme.cpp
    src/ui/Toast.cpp
    src/ui/GateWindow.cpp
    src/ui/UiText.cpp
)

add_library(XP-GSI SHARED ${XPGSI_SOURCES})

target_include_directories(XP-GSI PRIVATE
    src
    "${XPSDK_DIR}/CHeaders/XPLM"
    "${XPSDK_DIR}/CHeaders/Wrappers"
    "${XPSDK_DIR}/CHeaders/XPWidgets"
)

find_package(Threads REQUIRED)

# ── Warnings, LTO, symbols ────────────────────────────────────────────────────
# Our own code at a high warning level (not the fetched libraries).
if(MSVC)
    target_compile_options(XP-GSI PRIVATE /W4)
else()
    target_compile_options(XP-GSI PRIVATE -Wall -Wextra)
endif()
include(CheckIPOSupported)
check_ipo_supported(RESULT _ipo OUTPUT _ipo_msg LANGUAGES CXX)
if(_ipo)
    set_property(TARGET XP-GSI PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
endif()
if(MSVC)
    # Release: a separate PDB kept privately for crash analysis; the .xpl
    # names it without the build machine's path (/PDBALTPATH).
    target_compile_options(XP-GSI PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(XP-GSI PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF /PDBALTPATH:%_PDB%>)
endif()

target_link_libraries(XP-GSI PRIVATE
    nlohmann_json::nlohmann_json
    Threads::Threads  # std::thread / std::mutex in WorkQueue
    imgui
    OpenGL::GL        # OPENGL32.dll on Windows (system DLL)
)

# ── Version string injected at compile time ───────────────────────────────────
set(XPGSI_VERSION_SUFFIX "-beta")
target_compile_definitions(XP-GSI PRIVATE
    XPGSI_VERSION_STR="${PROJECT_VERSION}${XPGSI_VERSION_SUFFIX}"
    XPGSI_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    XPGSI_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    XPGSI_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# ── Platform-specific settings ────────────────────────────────────────────────

if(WIN32)
    # IBM=1 is required by the X-Plane SDK headers (XPLMDefs.h)
    target_compile_definitions(XP-GSI PRIVATE
        IBM=1
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1
        _CRT_SECURE_NO_WARNINGS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    # Windows needs import libraries; the .xpl is the RUNTIME output (DLL)
    if(NOT EXISTS "${XPSDK_DIR}/Libraries/Win/XPLM_64.lib")
        message(FATAL_ERROR "XPLM_64.lib not found in ${XPSDK_DIR}/Libraries/Win/")
    endif()
    target_link_libraries(XP-GSI PRIVATE "${XPSDK_DIR}/Libraries/Win/XPLM_64.lib")
    set_target_properties(XP-GSI PROPERTIES
        SUFFIX ".xpl"
        PREFIX ""
        # Multi-config generators (VS) and single-config (Ninja) both covered
        RUNTIME_OUTPUT_DIRECTORY         "${CMAKE_BINARY_DIR}/XP-GSI/win_x64"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/XP-GSI/win_x64"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/XP-GSI/win_x64"
    )

elseif(APPLE)
    # APL=1 is required by the X-Plane SDK headers
    target_compile_definitions(XP-GSI PRIVATE
        APL=1
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1
    )
    # XPLM symbols are provided by X-Plane at load time; don't link against stubs
    target_link_options(XP-GSI PRIVATE -undefined dynamic_lookup)
    target_compile_options(XP-GSI PRIVATE -fvisibility=hidden)
    set_target_properties(XP-GSI PROPERTIES
        SUFFIX ".xpl"
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/XP-GSI/mac_x64"
    )

else() # Linux
    # LIN=1 is required by the X-Plane SDK headers
    target_compile_definitions(XP-GSI PRIVATE
        LIN=1
        XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1
    )
    target_compile_options(XP-GSI PRIVATE -fvisibility=hidden)
    set_target_properties(XP-GSI PROPERTIES
        SUFFIX ".xpl"
        PREFIX ""
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/XP-GSI/lin_x64"
    )
endif()

# ── Unit tests ────────────────────────────────────────────────────────────────
option(XPGSI_BUILD_TESTS "Build unit tests" ON)
if(XPGSI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Copy data/ into plugin bundle at build time ───────────────────────────────
# Ensures static_stub.obj is present next to the .xpl without needing cmake --install.
add_custom_command(TARGET XP-GSI POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/data"
        "${CMAKE_BINARY_DIR}/XP-GSI/data"
    COMMENT "Copying plugin data directory")

# ── Install / package ─────────────────────────────────────────────────────────
# Copies the plugin output and data directories into build/XP-GSI/ so the CI
# artifact upload can zip the whole thing.
# Binaries come from the build tree; data/ always from the source tree, because
# the POST_BUILD copy of data/ only refreshes when the plugin relinks.
install(DIRECTORY "${CMAKE_BINARY_DIR}/XP-GSI/" DESTINATION .
        PATTERN "data" EXCLUDE)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/"
        DESTINATION "data")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSES/"
        DESTINATION "LICENSES")
