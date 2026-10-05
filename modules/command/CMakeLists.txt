add_library(timmy_command STATIC
    src/command_assembler.cpp
    src/transcript_normalizer.cpp
)

target_include_directories(timmy_command
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(timmy_command PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_command PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()
