add_library(timmy_core STATIC
    src/asr_engine_factory.cpp
    src/recognition_pipeline.cpp
    src/resource_path_resolver.cpp
)

target_include_directories(timmy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(timmy_core PUBLIC timmy_audio timmy_asr timmy_command)

if(MSVC)
    target_compile_options(timmy_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(timmy_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
