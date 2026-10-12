# GUI-only resources. Run staging on every build so changes to artwork are
# copied even when the executable itself does not need relinking.
configure_file("${PROJECT_SOURCE_DIR}/apps/gui/timmy_gui_version.h.in"
    "${PROJECT_BINARY_DIR}/generated/timmy_gui_version.h" @ONLY)
target_include_directories(timmy_gui PRIVATE "${PROJECT_BINARY_DIR}/generated")
if(TARGET timmy_gui_recognition_controls_tests)
    target_compile_definitions(timmy_gui_recognition_controls_tests PRIVATE
        TIMMY_EXPECTED_PROJECT_VERSION="${PROJECT_VERSION}")
    target_include_directories(timmy_gui_recognition_controls_tests PRIVATE
        "${PROJECT_BINARY_DIR}/generated")
endif()
if(WIN32)
    enable_language(RC)
    configure_file("${PROJECT_SOURCE_DIR}/apps/gui/timmy.rc.in"
        "${PROJECT_BINARY_DIR}/generated/timmy.rc" @ONLY)
    target_sources(timmy_gui PRIVATE "${PROJECT_BINARY_DIR}/generated/timmy.rc")
    set_property(SOURCE "${PROJECT_BINARY_DIR}/generated/timmy.rc" PROPERTY
        OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/resources/icons/timmy.ico")
endif()
set(timmy_branding_files
    "${PROJECT_SOURCE_DIR}/resources/branding/timmy_logo.png"
    "${PROJECT_SOURCE_DIR}/resources/branding/timmy_wordmark.png"
    "${PROJECT_SOURCE_DIR}/resources/branding/timmy_mark.png")
add_custom_target(timmy_gui_resources ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:timmy_gui>/resources/branding"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${timmy_branding_files}
        "$<TARGET_FILE_DIR:timmy_gui>/resources/branding"
    DEPENDS ${timmy_branding_files}
    VERBATIM)
add_dependencies(timmy_gui timmy_gui_resources)
# Place resources beside the installed executable, matching runtime lookup.
install(TARGETS timmy_gui RUNTIME DESTINATION bin)
install(FILES ${timmy_branding_files} DESTINATION bin/resources/branding)
