cmake_minimum_required(VERSION 3.21)

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

option(
    TIMMY_BUILD_GUI
    "Build the wxWidgets GUI frontend"
    ON
)

# ------------------------------------------------------------------------------
# Modules
# ------------------------------------------------------------------------------

add_subdirectory(modules/audio)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------------------
# Timmy CLI
# ------------------------------------------------------------------------------

add_executable(timmy_cli
    apps/cli/main.cpp
    apps/cli/cli_application.cpp
    apps/cli/audio_commands.cpp
    apps/cli/audio_monitor_command.cpp
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

# ------------------------------------------------------------------------------
# Timmy GUI
# ------------------------------------------------------------------------------

if(TIMMY_BUILD_GUI)

    if(WIN32)
        find_package(
            wxWidgets
            CONFIG
            REQUIRED
            COMPONENTS
                core
                base
        )

        set(TIMMY_WX_LIBRARIES
            wx::core
            wx::base
        )
    else()
        find_package(
            wxWidgets
            REQUIRED
            COMPONENTS
                core
                base
        )

        include(${wxWidgets_USE_FILE})

        set(TIMMY_WX_LIBRARIES
            ${wxWidgets_LIBRARIES}
        )
    endif()

    add_executable(timmy_gui WIN32
        apps/gui/timmy_app.cpp
        apps/gui/main_frame.cpp
    )

    set_target_properties(timmy_gui PROPERTIES
        OUTPUT_NAME "timmy-gui"
    )

    target_link_libraries(timmy_gui
        PRIVATE
            timmy_audio
            ${TIMMY_WX_LIBRARIES}
    )

    if(MSVC)
        target_compile_options(timmy_gui PRIVATE
            /W4
            /permissive-
        )
    else()
        target_compile_options(timmy_gui PRIVATE
            -Wall
            -Wextra
            -Wpedantic
        )
    endif()

endif()
