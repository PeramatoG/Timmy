add_executable(timmy_audio_tests
    audio_pipeline_test.cpp
)

target_link_libraries(timmy_audio_tests
    PRIVATE
        timmy_audio
)

if(MSVC)
    target_compile_options(timmy_audio_tests PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_audio_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(
    NAME timmy.audio_pipeline
    COMMAND timmy_audio_tests
)

add_executable(timmy_audio_format_adapter_tests
    audio_format_adapter_test.cpp
)

target_link_libraries(timmy_audio_format_adapter_tests
    PRIVATE
        timmy_audio
)

if(MSVC)
    target_compile_options(timmy_audio_format_adapter_tests PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_audio_format_adapter_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(
    NAME timmy.audio_format_adapter
    COMMAND timmy_audio_format_adapter_tests
)

add_executable(timmy_audio_contracts_tests
    audio_contracts_test.cpp
)

target_link_libraries(timmy_audio_contracts_tests
    PRIVATE
        timmy_audio
)

target_include_directories(timmy_audio_contracts_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/modules/audio/src
        ${PROJECT_SOURCE_DIR}/third_party/miniaudio
)

if(MSVC)
    target_compile_options(timmy_audio_contracts_tests PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_audio_contracts_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(
    NAME timmy.audio_contracts
    COMMAND timmy_audio_contracts_tests
)

add_executable(timmy_asr_contracts_tests
    asr_contracts_test.cpp
)

target_link_libraries(timmy_asr_contracts_tests
    PRIVATE
        timmy_asr
)

if(MSVC)
    target_compile_options(timmy_asr_contracts_tests PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_asr_contracts_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(
    NAME timmy.asr_contracts
    COMMAND timmy_asr_contracts_tests
)

add_executable(timmy_whisper_asr_tests
    whisper_asr_backend_test.cpp
)

target_link_libraries(timmy_whisper_asr_tests
    PRIVATE
        timmy_asr
)

target_include_directories(timmy_whisper_asr_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/modules/asr/src
)

if(MSVC)
    target_compile_options(timmy_whisper_asr_tests PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_whisper_asr_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(
    NAME timmy.whisper_asr
    COMMAND timmy_whisper_asr_tests
)

add_executable(timmy_whisper_endpoint_tests
    whisper_endpoint_detector_test.cpp
)

target_include_directories(timmy_whisper_endpoint_tests
    PRIVATE
        ${PROJECT_SOURCE_DIR}/modules/asr/src
)

target_link_libraries(timmy_whisper_endpoint_tests
    PRIVATE
        timmy_asr
)

if(MSVC)
    target_compile_options(timmy_whisper_endpoint_tests PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_whisper_endpoint_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(
    NAME timmy.whisper_endpoint
    COMMAND timmy_whisper_endpoint_tests
)

add_executable(timmy_transcript_normalizer_tests
    transcript_normalizer_test.cpp
)

target_link_libraries(timmy_transcript_normalizer_tests
    PRIVATE
        timmy_command
)

if(MSVC)
    target_compile_options(timmy_transcript_normalizer_tests PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_transcript_normalizer_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(
    NAME timmy.transcript_normalizer
    COMMAND timmy_transcript_normalizer_tests
)

add_executable(timmy_command_assembler_tests
    command_assembler_test.cpp
)

target_link_libraries(timmy_command_assembler_tests
    PRIVATE
        timmy_command
)

if(MSVC)
    target_compile_options(timmy_command_assembler_tests PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_command_assembler_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(
    NAME timmy.command_assembler
    COMMAND timmy_command_assembler_tests
)

add_executable(timmy_semantic_command_parser_tests
    semantic_command_parser_test.cpp
)

target_link_libraries(timmy_semantic_command_parser_tests
    PRIVATE
        timmy_command
)

if(MSVC)
    target_compile_options(timmy_semantic_command_parser_tests PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(timmy_semantic_command_parser_tests PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

add_test(
    NAME timmy.semantic_command_parser
    COMMAND timmy_semantic_command_parser_tests
)

add_executable(timmy_recognition_pipeline_tests recognition_pipeline_test.cpp)
target_link_libraries(timmy_recognition_pipeline_tests PRIVATE timmy_core)
add_test(NAME timmy.recognition_pipeline COMMAND timmy_recognition_pipeline_tests)

add_executable(timmy_resource_path_resolver_tests resource_path_resolver_test.cpp)
target_link_libraries(timmy_resource_path_resolver_tests PRIVATE timmy_core)
add_test(NAME timmy.resource_path_resolver COMMAND timmy_resource_path_resolver_tests)

if(MSVC)
    target_compile_options(timmy_recognition_pipeline_tests PRIVATE /W4 /permissive-)
    target_compile_options(timmy_resource_path_resolver_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(timmy_recognition_pipeline_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(timmy_resource_path_resolver_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
