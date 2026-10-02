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

# ------------------------------------------------------------------------------
# Timmy audio module
# ------------------------------------------------------------------------------

add_library(timmy_audio STATIC
    src/audio_device_enumerator.cpp
)

target_include_directories(timmy_audio
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(timmy_audio
    PRIVATE
        timmy_miniaudio
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