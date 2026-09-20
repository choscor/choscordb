# Release diagnostics keep useful source paths without recording the builder's
# account, checkout, dependency cache, or temporary build directories.
set(CHOSCORDB_PRIVACY_MAPPINGS)
macro(choscordb_privacy_map original replacement)
  if(IS_ABSOLUTE "${original}" AND NOT "${original}" STREQUAL "/")
    string(REGEX REPLACE "/+$" "" choscordb_prefix "${original}")
    list(APPEND CHOSCORDB_PRIVACY_MAPPINGS "${choscordb_prefix}=${replacement}")
    file(REAL_PATH "${choscordb_prefix}" choscordb_real_prefix)
    if(NOT choscordb_real_prefix STREQUAL "${choscordb_prefix}")
      list(APPEND CHOSCORDB_PRIVACY_MAPPINGS "${choscordb_real_prefix}=${replacement}")
    endif()
  endif()
endmacro()

if(CHOSCORDB_PRODUCTION_RELEASE OR CHOSCORDB_UPDATE_REHEARSAL)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "^(AppleClang|Clang|GNU)$")
    message(FATAL_ERROR "Release path privacy requires a compiler with -ffile-prefix-map")
  endif()
  if(APPLE)
    # Apple ld can copy N_OSO archive/object paths from prebuilt Rust std into
    # its debug map even after source remapping. Omit debug information at link
    # time; keep normal symbols/backtraces and remapped runtime diagnostics.
    add_link_options("LINKER:-S")
  endif()
  # The last matching mapping wins. More specific roots follow broader ones.
  choscordb_privacy_map("$ENV{HOME}" "/builder-home")
  choscordb_privacy_map("$ENV{TMPDIR}" "/temporary")
  choscordb_privacy_map("$ENV{HOME}/.cargo" "/cargo")
  choscordb_privacy_map("$ENV{HOME}/.rustup" "/rustup")
  choscordb_privacy_map("$ENV{CARGO_HOME}" "/cargo")
  choscordb_privacy_map("$ENV{RUSTUP_HOME}" "/rustup")
  get_filename_component(choscordb_compiler_binary "${CMAKE_CXX_COMPILER}" REALPATH)
  get_filename_component(choscordb_compiler_bin "${choscordb_compiler_binary}" DIRECTORY)
  get_filename_component(choscordb_compiler_root "${choscordb_compiler_bin}" DIRECTORY)
  choscordb_privacy_map("${choscordb_compiler_root}" "/native-toolchain")
  choscordb_privacy_map("${CMAKE_SYSROOT}" "/sdk")
  choscordb_privacy_map("${CMAKE_OSX_SYSROOT}" "/sdk")
  choscordb_privacy_map("${PROJECT_SOURCE_DIR}" "/choscordb")
  choscordb_privacy_map("${PROJECT_BINARY_DIR}" "/build")
  foreach(mapping IN LISTS CHOSCORDB_PRIVACY_MAPPINGS)
    add_compile_options("-ffile-prefix-map=${mapping}")
  endforeach()
endif()

function(choscordb_rust_privacy_environment output rustc)
  if(NOT CHOSCORDB_PRODUCTION_RELEASE AND NOT CHOSCORDB_UPDATE_REHEARSAL)
    set(${output} "" PARENT_SCOPE)
    return()
  endif()
  execute_process(COMMAND "${rustc}" --print sysroot
    OUTPUT_VARIABLE choscordb_rust_sysroot OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
  choscordb_privacy_map("${choscordb_rust_sysroot}" "/rust-toolchain")
  string(ASCII 31 separator)
  set(encoded "$ENV{CARGO_ENCODED_RUSTFLAGS}")
  if(encoded STREQUAL "" AND NOT "$ENV{RUSTFLAGS}" STREQUAL "")
    # Cargo's unencoded RUSTFLAGS itself uses whitespace-delimited arguments.
    string(STRIP "$ENV{RUSTFLAGS}" unencoded)
    string(REGEX REPLACE "[ \t\r\n]+" "${separator}" encoded "${unencoded}")
  endif()
  set(native_flags "")
  foreach(mapping IN LISTS CHOSCORDB_PRIVACY_MAPPINGS)
    if(NOT encoded STREQUAL "")
      string(APPEND encoded "${separator}")
    endif()
    string(APPEND encoded "--remap-path-prefix=${mapping}")
    # cc-rs parses these as shell words when CC_SHELL_ESCAPED_FLAGS is enabled.
    # Quote each whole flag so spaces in user/source/build paths remain intact.
    string(REPLACE "\\" "\\\\" quoted "-ffile-prefix-map=${mapping}")
    string(REPLACE "\"" "\\\"" quoted "${quoted}")
    string(APPEND native_flags " \"${quoted}\"")
  endforeach()
  set(${output}
    "CARGO_ENCODED_RUSTFLAGS=${encoded}"
    "CC_SHELL_ESCAPED_FLAGS=1"
    "CFLAGS=$ENV{CFLAGS}${native_flags}"
    "CXXFLAGS=$ENV{CXXFLAGS}${native_flags}"
    PARENT_SCOPE)
endfunction()
