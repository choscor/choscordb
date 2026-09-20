# Production packaging reuses app-mark.svg without adding generated artwork to Git.
if(APPLE AND (CHOSCORDB_PRODUCTION_RELEASE OR CHOSCORDB_UPDATE_REHEARSAL))
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(icon "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icns")
  add_custom_command(OUTPUT "${icon}"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/release/render_icon.py"
      --source "${PROJECT_SOURCE_DIR}/desktop/resources/icons/app-mark.svg"
      --output "${icon}"
    DEPENDS scripts/release/render_icon.py desktop/resources/icons/app-mark.svg
    VERBATIM)
  target_sources(choscordb PRIVATE "${icon}")
  set_source_files_properties("${icon}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(choscordb PROPERTIES MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
endif()
