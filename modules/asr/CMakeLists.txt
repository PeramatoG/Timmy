include(FetchContent)

option(TIMMY_ENABLE_VULKAN "Build optional Whisper Vulkan acceleration" OFF)
if(TIMMY_ENABLE_VULKAN)
    find_package(Vulkan QUIET COMPONENTS glslc)
    if(NOT Vulkan_FOUND OR NOT Vulkan_GLSLC_EXECUTABLE)
        message(FATAL_ERROR "TIMMY_ENABLE_VULKAN=ON requires Vulkan development headers, loader library and glslc. Install the Vulkan SDK; CPU builds need no SDK.")
    endif()
    if(DEFINED ENV{VULKAN_SDK})
        list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}")
    endif()
    find_package(SPIRV-Headers CONFIG QUIET)
    if(NOT SPIRV-Headers_FOUND)
        message(FATAL_ERROR "TIMMY_ENABLE_VULKAN=ON requires SPIRV-Headers from the Vulkan SDK/development packages.")
    endif()
endif()

# whisper.cpp v1.9.4 (commit 927cfce34f31707e17f2bff35c349632fb9e2c3a).
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_IS_DEV OFF CACHE BOOL "" FORCE)
set(WHISPER_CURL OFF CACHE BOOL "" FORCE)
set(WHISPER_COREML OFF CACHE BOOL "" FORCE)
set(WHISPER_OPENVINO OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS ${TIMMY_ENABLE_VULKAN} CACHE BOOL "" FORCE)
set(GGML_CPU ON CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL OFF CACHE BOOL "" FORCE)
set(GGML_CPU_ALL_VARIANTS OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
foreach(backend CANN ET HIP MUSA RPC VIRTGPU VIRTGPU_BACKEND SYCL WEBGPU ZDNN OPENCL HEXAGON ZENDNN OPENVINO)
    set(GGML_${backend} OFF CACHE BOOL "" FORCE)
endforeach()

FetchContent_Declare(
    whisper_cpp
    # Use GitHub's archive service directly; the redirecting github.com URL
    # intermittently returns HTTP 504 in fresh CI builds.
    URL https://codeload.github.com/ggml-org/whisper.cpp/tar.gz/927cfce34f31707e17f2bff35c349632fb9e2c3a
    URL_HASH SHA256=41b664fee09e79176ac277b5237debec34f8d74af3c7d71f333f1ec67989ecde
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(whisper_cpp)

# The pin silently substitutes CPU on failed GPU state initialization and does not
# unwind a throwing state constructor. Apply only the focused ownership/strict-init
# overlay; no model, decoding or inference tuning changes.
include(${PROJECT_SOURCE_DIR}/cmake/WhisperTransactionalInit.cmake)
timmy_patch_whisper_init(${whisper_cpp_SOURCE_DIR}/src/whisper.cpp)

if(TIMMY_ENABLE_VULKAN)
    # The pinned Whisper references CPU symbols directly: full BACKEND_DL is not
    # linkable without CPU. Build the optional module in its own scope instead.
    function(timmy_add_vulkan_module)
        set(GGML_BACKEND_DL ON)
        set(GGML_BACKEND_DIR "")
        get_target_property(GGML_VERSION ggml VERSION)
        get_target_property(GGML_VERSION_MAJOR ggml SOVERSION)
        set(CMAKE_INSTALL_BINDIR bin)
        set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        add_subdirectory(${whisper_cpp_SOURCE_DIR}/ggml/src/ggml-vulkan
            ${whisper_cpp_BINARY_DIR}/ggml/src/ggml-vulkan)
    endfunction()
    timmy_add_vulkan_module()
endif()

add_library(timmy_asr STATIC
    src/asr_backend.cpp
    src/configurable_settings.cpp
    src/compute_selection.cpp
    src/whisper_compute_runtime.cpp
    src/whisper_configuration.cpp
    src/whisper_command_decoding.cpp
    src/whisper_endpoint_detector.cpp
    src/whisper_recognition_guidance.cpp
    src/whisper_asr_backend.cpp
    ${whisper_cpp_SOURCE_DIR}/examples/grammar-parser.cpp
)

target_compile_definitions(timmy_asr PRIVATE
    TIMMY_VULKAN_COMPILED=$<BOOL:${TIMMY_ENABLE_VULKAN}>)

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
