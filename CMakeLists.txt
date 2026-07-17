cmake_minimum_required(VERSION 3.24)

project(reversi VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(REVERSI_BUILD_APP "Build the Qt GUI application" ON)
option(REVERSI_BUILD_CLI "Build the command-line harness" ON)
option(REVERSI_BUILD_TESTS "Build unit tests" ON)
option(REVERSI_BUILD_TOOLS "Build the WTHOR training pipeline tools" OFF)
option(REVERSI_WERROR "Treat warnings as errors" OFF)

# Shared warning flags, consumed privately by every target in this repo.
add_library(reversi_warnings INTERFACE)
if(MSVC)
    target_compile_options(reversi_warnings INTERFACE /W4 /permissive-)
    if(REVERSI_WERROR)
        target_compile_options(reversi_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(reversi_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(REVERSI_WERROR)
        target_compile_options(reversi_warnings INTERFACE -Werror)
    endif()
endif()

add_subdirectory(engine)

if(REVERSI_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(REVERSI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Added after the REVERSI_BUILD_TESTS block (M9 phase 2): app/CMakeLists.txt defines its own
# app_tests target (gtest_discover_tests) when REVERSI_BUILD_TESTS is also ON, mirroring the
# tools/ subdirectory's already-working position below - enable_testing() must have already run
# in this (ancestor) scope for that to register reliably, rather than relying on unspecified
# ordering behavior.
if(REVERSI_BUILD_APP)
    add_subdirectory(app)
endif()

if(REVERSI_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

# M10 phase 5: render the Inno Setup template so @PROJECT_VERSION@ never needs a manual bump at
# release time - it already tracks this file's own project() version, the same single source of
# truth cli/'s --version output uses. Windows-only (Inno Setup itself is Windows-only); harmless to
# skip elsewhere since packaging/windows/reversi.iss is never referenced by any other target.
if(WIN32 AND REVERSI_BUILD_APP)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/windows/reversi.iss.in"
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/windows/reversi.iss"
        @ONLY
    )
endif()
