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

add_executable(timmy_audio_contracts_tests
    audio_contracts_test.cpp
)

target_link_libraries(timmy_audio_contracts_tests
    PRIVATE
        timmy_audio
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
