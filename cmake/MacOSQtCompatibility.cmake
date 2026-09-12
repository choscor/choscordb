# Qt 6.8's macOS package links AGL through WrapOpenGL even when the selected
# SDK no longer ships that deprecated framework. Keep the workaround local to
# that imported compatibility target and only apply it when AGL is unavailable.
function(choscordb_remove_unavailable_agl)
  if(NOT APPLE OR NOT TARGET WrapOpenGL::WrapOpenGL)
    return()
  endif()
  set(choscordb_macos_sdk "${CMAKE_OSX_SYSROOT}")
  if(NOT IS_ABSOLUTE "${choscordb_macos_sdk}")
    if(NOT choscordb_macos_sdk)
      set(choscordb_macos_sdk macosx)
    endif()
    execute_process(COMMAND xcrun --sdk "${choscordb_macos_sdk}" --show-sdk-path
      OUTPUT_VARIABLE choscordb_macos_sdk
      OUTPUT_STRIP_TRAILING_WHITESPACE
      RESULT_VARIABLE choscordb_xcrun_result)
    if(NOT choscordb_xcrun_result EQUAL 0)
      message(FATAL_ERROR "Unable to locate the selected macOS SDK")
    endif()
  endif()
  if(EXISTS "${choscordb_macos_sdk}/System/Library/Frameworks/AGL.framework")
    return()
  endif()
  get_target_property(choscordb_wrap_opengl_links WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
  if(NOT choscordb_wrap_opengl_links)
    return()
  endif()
  list(FILTER choscordb_wrap_opengl_links EXCLUDE REGEX "(^-framework[ ]+AGL$|/AGL\\.framework$)")
  set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY INTERFACE_LINK_LIBRARIES "${choscordb_wrap_opengl_links}")
endfunction()
