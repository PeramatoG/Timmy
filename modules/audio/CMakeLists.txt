find_package(Threads REQUIRED)

# ------------------------------------------------------------------------------
# miniaudio
# ------------------------------------------------------------------------------

add_library(timmy_miniaudio STATIC
    ${PROJECT_SOURCE_DIR}/third_party/miniaudio/miniaudio.c
)

target_include_directories(timmy_miniaudio
    PUBLIC
        ${PROJECT_SOURCE_DIR}/third_party/miniaudio
)

target_link_libraries(timmy_miniaudio
    PUBLIC
        Threads::Threads
)

if(UNIX AND NOT APPLE)
    target_link_libraries(timmy_miniaudio
        PUBLIC
            m
            ${CMAKE_DL_LIBS}
    )
endif()

# ------------------------------------------------------------------------------
# Timmy audio module
# ------------------------------------------------------------------------------

add_library(timmy_audio STATIC
    src/audio_device_enumerator.cpp
    src/audio_engine.cpp
    src/miniaudio_device_identity.cpp
    src/audio_meter.cpp
    src/audio_channel_selector.cpp
    src/audio_format_adapter.cpp
)

target_include_directories(timmy_audio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(timmy_audio
    PRIVATE
        timmy_miniaudio
        Threads::Threads
)

if(MSVC)
    target_compile_options(timmy_audio PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_audio PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()