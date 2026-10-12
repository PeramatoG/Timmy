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
add_subdirectory(modules/asr)
add_subdirectory(modules/command)
add_subdirectory(modules/transport)
add_subdirectory(modules/core)

# wx-free frontend configuration/lifecycle code is tested in headless builds too.
if(TIMMY_BUILD_GUI OR BUILD_TESTING)
    add_library(timmy_gui_output STATIC apps/gui/command_output_session.cpp)
    target_include_directories(timmy_gui_output PUBLIC ${PROJECT_SOURCE_DIR}/apps/gui)
    target_link_libraries(timmy_gui_output PUBLIC timmy_core)
    if(MSVC)
        target_compile_options(timmy_gui_output PRIVATE /W4 /permissive-)
    else()
        target_compile_options(timmy_gui_output PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

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
    apps/cli/asr_commands.cpp
    apps/cli/asr_listen_command.cpp
    apps/cli/osc_commands.cpp
)

set_target_properties(timmy_cli PROPERTIES
    OUTPUT_NAME "timmy-cli"
)

target_link_libraries(timmy_cli
    PRIVATE
        timmy_audio
        timmy_core
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
        apps/gui/benchmark_dialog.cpp
    )

    set_target_properties(timmy_gui PROPERTIES
        OUTPUT_NAME "timmy-gui"
    )

    target_link_libraries(timmy_gui
        PRIVATE
            timmy_audio
            timmy_core
            timmy_gui_output
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

    if(BUILD_TESTING)
        add_executable(timmy_gui_execution_events_tests tests/gui_execution_events_test.cpp)
        target_link_libraries(timmy_gui_execution_events_tests PRIVATE
            timmy_gui_output ${TIMMY_WX_LIBRARIES})
        add_test(NAME timmy.gui_execution_events COMMAND timmy_gui_execution_events_tests)
        set_tests_properties(timmy.gui_execution_events PROPERTIES TIMEOUT 20)
        if(MSVC)
            target_compile_options(timmy_gui_execution_events_tests PRIVATE /W4 /permissive-)
        else()
            target_compile_options(timmy_gui_execution_events_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()

        add_executable(timmy_gui_recognition_controls_tests
            tests/gui_recognition_controls_test.cpp
            apps/gui/main_frame.cpp
            apps/gui/benchmark_dialog.cpp)
        target_include_directories(timmy_gui_recognition_controls_tests PRIVATE
            ${PROJECT_SOURCE_DIR}/apps/gui)
        target_link_libraries(timmy_gui_recognition_controls_tests PRIVATE
            timmy_audio timmy_core timmy_gui_output ${TIMMY_WX_LIBRARIES})
        add_test(NAME timmy.gui_recognition_controls COMMAND timmy_gui_recognition_controls_tests)
        set_tests_properties(timmy.gui_recognition_controls PROPERTIES TIMEOUT 30)
        if(MSVC)
            target_compile_options(timmy_gui_recognition_controls_tests PRIVATE /W4 /permissive-)
        else()
            target_compile_options(timmy_gui_recognition_controls_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()

endif()

include(cmake/ComputeRuntime.cmake)
timmy_stage_directory("${PROJECT_SOURCE_DIR}")
