add_library(timmy_core STATIC
    src/recognition_pipeline.cpp
)

target_include_directories(timmy_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(timmy_core
    PUBLIC
        timmy_asr
        timmy_audio
        timmy_command
)

if(MSVC)
    target_compile_options(timmy_core PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
