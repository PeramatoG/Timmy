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
