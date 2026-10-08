find_package(Threads REQUIRED)

add_library(timmy_core STATIC
    src/asr_engine_factory.cpp
    src/command_dispatcher.cpp
    src/command_recognition_profile.cpp
    src/recognition_command_executor.cpp
    src/recognition_pipeline.cpp
    src/recognition_benchmark.cpp
    src/resource_path_resolver.cpp
)

target_include_directories(timmy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(timmy_core PUBLIC
    timmy_audio
    timmy_asr
    timmy_command
    timmy_transport
    Threads::Threads
)

if(MSVC)
    target_compile_options(timmy_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(timmy_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
