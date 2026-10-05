add_library(timmy_transport STATIC
    src/command_emitter.cpp
)

target_include_directories(timmy_transport
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(timmy_transport PUBLIC timmy_command)

if(MSVC)
    target_compile_options(timmy_transport PRIVATE /W4 /permissive-)
else()
    target_compile_options(timmy_transport PRIVATE -Wall -Wextra -Wpedantic)
endif()
