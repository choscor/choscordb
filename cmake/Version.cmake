# Cargo's workspace version is the single checked-in release version.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../Cargo.toml" choscordb_manifest)
string(REGEX MATCH "\\[workspace\\.package\\][^[]*" choscordb_package "${choscordb_manifest}")
string(REGEX MATCH "(^|\n)version[ \t]*=[ \t]*\"([^\"]+)\"" choscordb_version_line "${choscordb_package}")
set(CHOSCORDB_VERSION "${CMAKE_MATCH_2}")
if(NOT CHOSCORDB_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$")
  message(FATAL_ERROR "Cargo workspace.package.version must be a stable X.Y.Z version")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/../Cargo.toml")
