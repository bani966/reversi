cmake_minimum_required(VERSION 3.24)

project(reversi VERSION 0.1.0 LANGUAGES CXX)

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

if(REVERSI_BUILD_APP)
    add_subdirectory(app)
endif()

if(REVERSI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(REVERSI_BUILD_TOOLS)
    add_subdirectory(tools)
endif()
