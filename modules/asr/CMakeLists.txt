include(FetchContent)

# whisper.cpp v1.9.4 (commit 927cfce34f31707e17f2bff35c349632fb9e2c3a).
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_IS_DEV OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)
set(WHISPER_COREML OFF CACHE BOOL "" FORCE)
set(WHISPER_OPENVINO OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    whisper_cpp
    URL https://github.com/ggml-org/whisper.cpp/archive/927cfce34f31707e17f2bff35c349632fb9e2c3a.tar.gz
    URL_HASH SHA256=41b664fee09e79176ac277b5237debec34f8d74af3c7d71f333f1ec67989ecde
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(whisper_cpp)

add_library(timmy_asr STATIC
    src/asr_backend.cpp
    src/whisper_command_decoding.cpp
    src/whisper_endpoint_detector.cpp
    src/whisper_recognition_guidance.cpp
    src/whisper_asr_backend.cpp
    ${whisper_cpp_SOURCE_DIR}/examples/grammar-parser.cpp
)

target_include_directories(timmy_asr
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${whisper_cpp_SOURCE_DIR}/examples
)

target_link_libraries(timmy_asr
    PUBLIC
        timmy_audio
    PRIVATE
        whisper
)

if(MSVC)
    # Keep upstream numeric-conversion warnings confined to whisper.
    target_compile_options(whisper PRIVATE /wd4244)
    # Only the upstream grammar parser has an unused internal helper.
    set_property(SOURCE ${whisper_cpp_SOURCE_DIR}/examples/grammar-parser.cpp
        APPEND PROPERTY COMPILE_OPTIONS /wd4505)
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
