add_library(timmy_command STATIC
    src/command_profile.cpp
    src/command_processor.cpp
    src/hydra2_command_renderer.cpp
    src/command_assembler.cpp
    src/command_lexicon.cpp
    src/command_lexical_repair.cpp
    src/semantic_command.cpp
    src/semantic_command_parser.cpp
    src/spoken_number_normalizer.cpp
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
