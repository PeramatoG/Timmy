add_library(timmy_asr STATIC
    src/asr_backend.cpp
)

target_include_directories(timmy_asr
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(timmy_asr
    PUBLIC
        timmy_audio
)

if(MSVC)
    target_compile_options(timmy_asr PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_asr PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
