cmake_minimum_required(VERSION 3.20)

project(
    Timmy
    VERSION 0.1.0
    DESCRIPTION "Offline-first voice command assistant for lighting consoles and OSC-controlled systems"
    LANGUAGES C CXX
)

# ------------------------------------------------------------------------------
# Global project settings
# ------------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

# ------------------------------------------------------------------------------
# Modules
# ------------------------------------------------------------------------------

add_subdirectory(modules/audio)

# ------------------------------------------------------------------------------
# Timmy CLI
# ------------------------------------------------------------------------------

add_executable(timmy_cli
    apps/cli/main.cpp
)

set_target_properties(timmy_cli PROPERTIES
    OUTPUT_NAME "timmy-cli"
)

target_link_libraries(timmy_cli
    PRIVATE
        timmy_audio
)

# ------------------------------------------------------------------------------
# Compiler warnings
# ------------------------------------------------------------------------------

if(MSVC)
    target_compile_options(timmy_cli PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_cli PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()