include_guard(GLOBAL)

option(CHOSCORDB_ENABLE_STRICT_WARNINGS "Treat first-party C++ warnings as errors" ON)
option(CHOSCORDB_ENABLE_CLANG_TIDY "Add the blocking LLVM 23 clang-tidy target" OFF)
option(CHOSCORDB_ENABLE_SANITIZERS "Instrument first-party C++ with ASan and UBSan" OFF)
option(CHOSCORDB_ENABLE_COVERAGE "Instrument first-party C++ and add an LLVM LCOV target" OFF)
option(CHOSCORDB_ENABLE_IWYU "Add the advisory Include-What-You-Use target" OFF)

if(CHOSCORDB_ENABLE_SANITIZERS AND CHOSCORDB_ENABLE_COVERAGE)
  message(FATAL_ERROR "Sanitizer and coverage instrumentation require separate build trees")
endif()

function(_choscordb_require_llvm_23 variable description)
  set(candidates ${ARGN})
  find_program(${variable} NAMES ${candidates})
  if(NOT ${variable})
    list(GET candidates 0 preferred_name)
    message(FATAL_ERROR "${description} is required. Install LLVM 23 (${preferred_name}) and reconfigure.")
  endif()
  execute_process(COMMAND "${${variable}}" --version OUTPUT_VARIABLE version_output ERROR_VARIABLE version_error RESULT_VARIABLE version_result)
  if(NOT version_result EQUAL 0 OR NOT "${version_output}${version_error}" MATCHES "version 23\\.")
    message(FATAL_ERROR "${description} must be LLVM major version 23; found: ${version_output}${version_error}")
  endif()
endfunction()

if(CHOSCORDB_ENABLE_SANITIZERS)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "CHOSCORDB_ENABLE_SANITIZERS requires Clang 23 on Linux")
  endif()
  if(NOT CMAKE_CXX_COMPILER_VERSION MATCHES "^23\\.")
    message(FATAL_ERROR "Sanitizer builds require Clang major version 23")
  endif()
endif()

if(CHOSCORDB_ENABLE_COVERAGE)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR MSVC OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR NOT CMAKE_CXX_COMPILER_VERSION MATCHES "^23\\.")
    message(FATAL_ERROR "CHOSCORDB_ENABLE_COVERAGE requires Clang major version 23 on Linux")
  endif()
  _choscordb_require_llvm_23(CHOSCORDB_LLVM_PROFDATA "C++ coverage profile merger" llvm-profdata-23 llvm-profdata)
  _choscordb_require_llvm_23(CHOSCORDB_LLVM_COV "C++ coverage exporter" llvm-cov-23 llvm-cov)
endif()

if(CHOSCORDB_ENABLE_CLANG_TIDY)
  _choscordb_require_llvm_23(CHOSCORDB_CLANG_TIDY "clang-tidy analysis" clang-tidy-23 clang-tidy)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export commands for first-party analysis" FORCE)
endif()

if(CHOSCORDB_ENABLE_IWYU)
  find_program(CHOSCORDB_IWYU_TOOL NAMES iwyu_tool.py iwyu_tool)
  if(NOT CHOSCORDB_IWYU_TOOL)
    message(FATAL_ERROR "Include-What-You-Use is required. Install IWYU and ensure iwyu_tool.py is on PATH.")
  endif()
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export commands for first-party analysis" FORCE)
endif()

function(_choscordb_register_first_party_target target)
  set(sources)
  foreach(candidate IN LISTS ARGN)
    if(candidate MATCHES "\\.(c|cc|cpp|cxx)$")
      list(APPEND sources "${candidate}")
    endif()
  endforeach()

  if(CHOSCORDB_ENABLE_STRICT_WARNINGS)
    if(MSVC)
      set_property(SOURCE ${sources} APPEND PROPERTY COMPILE_OPTIONS /W4 /WX)
    else()
      set_property(SOURCE ${sources} APPEND PROPERTY COMPILE_OPTIONS -Wall -Wextra -Wpedantic -Werror)
    endif()
  endif()
  if(CHOSCORDB_ENABLE_SANITIZERS)
    set_property(SOURCE ${sources} APPEND PROPERTY COMPILE_OPTIONS -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  endif()
  if(CHOSCORDB_ENABLE_COVERAGE)
    set_property(SOURCE ${sources} APPEND PROPERTY COMPILE_OPTIONS -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
  endif()

  set_property(GLOBAL APPEND PROPERTY CHOSCORDB_FIRST_PARTY_SOURCES ${sources})
  set_property(GLOBAL APPEND PROPERTY CHOSCORDB_FIRST_PARTY_TARGETS ${target})
endfunction()

function(choscordb_add_library target)
  add_library(${ARGV})
  _choscordb_register_first_party_target(${target} ${ARGN})
endfunction()

function(choscordb_add_executable target)
  add_executable(${ARGV})
  _choscordb_register_first_party_target(${target} ${ARGN})
endfunction()

function(choscordb_add_quality_targets usage_target)
  get_property(first_party_sources GLOBAL PROPERTY CHOSCORDB_FIRST_PARTY_SOURCES)
  list(REMOVE_DUPLICATES first_party_sources)
  list(TRANSFORM first_party_sources PREPEND "${PROJECT_SOURCE_DIR}/")

  file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS
    RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/desktop/*.h"
    "${PROJECT_SOURCE_DIR}/desktop/*.hpp"
    "${PROJECT_SOURCE_DIR}/tests/*.h"
    "${PROJECT_SOURCE_DIR}/tests/*.hpp"
  )
  set(header_units)
  foreach(header IN LISTS public_headers)
    string(MAKE_C_IDENTIFIER "${header}" unit_name)
    set(unit "${CMAKE_CURRENT_BINARY_DIR}/header-check/${unit_name}.cpp")
    file(CONFIGURE OUTPUT "${unit}" CONTENT "#include \"${header}\"\n" NEWLINE_STYLE UNIX)
    list(APPEND header_units "${unit}")
  endforeach()
  add_library(choscordb-header-check-objects OBJECT EXCLUDE_FROM_ALL ${header_units})
  target_include_directories(choscordb-header-check-objects PRIVATE "${PROJECT_SOURCE_DIR}")
  target_link_libraries(choscordb-header-check-objects PRIVATE ${usage_target})
  if(CHOSCORDB_ENABLE_STRICT_WARNINGS)
    if(MSVC)
      set_property(SOURCE ${header_units} APPEND PROPERTY COMPILE_OPTIONS /W4 /WX)
    else()
      set_property(SOURCE ${header_units} APPEND PROPERTY COMPILE_OPTIONS -Wall -Wextra -Wpedantic -Werror)
    endif()
  endif()
  add_custom_target(choscordb-header-check DEPENDS choscordb-header-check-objects)

  if(CHOSCORDB_ENABLE_CLANG_TIDY)
    add_custom_target(choscordb-clang-tidy
      COMMAND "${CHOSCORDB_CLANG_TIDY}" --config-file="${PROJECT_SOURCE_DIR}/.clang-tidy" --list-checks -p="${CMAKE_BINARY_DIR}" ${first_party_sources}
      COMMAND "${CHOSCORDB_CLANG_TIDY}" --config-file="${PROJECT_SOURCE_DIR}/.clang-tidy" --warnings-as-errors=* -p="${CMAKE_BINARY_DIR}" ${first_party_sources}
      DEPENDS ${usage_target} ${first_party_sources}
      COMMENT "Running blocking LLVM 23 clang-tidy over hand-written first-party sources"
      COMMAND_EXPAND_LISTS VERBATIM
    )
  endif()

  if(CHOSCORDB_ENABLE_IWYU)
    add_custom_target(choscordb-iwyu
      COMMAND "${CHOSCORDB_IWYU_TOOL}" -p "${CMAKE_BINARY_DIR}" ${first_party_sources}
      DEPENDS ${usage_target} ${first_party_sources}
      COMMENT "Running advisory Include-What-You-Use over hand-written first-party sources"
      COMMAND_EXPAND_LISTS VERBATIM
    )
  endif()

  if(CHOSCORDB_ENABLE_COVERAGE)
    get_property(first_party_targets GLOBAL PROPERTY CHOSCORDB_FIRST_PARTY_TARGETS)
    set(coverage_objects)
    foreach(target IN LISTS first_party_targets)
      get_target_property(target_type ${target} TYPE)
      if(target_type STREQUAL "EXECUTABLE")
        list(APPEND coverage_objects "$<TARGET_FILE:${target}>")
      endif()
    endforeach()
    file(GENERATE
      OUTPUT "${CMAKE_BINARY_DIR}/coverage-objects.txt"
      CONTENT "$<JOIN:${coverage_objects},\n>\n"
    )
    add_custom_target(choscordb-cpp-coverage
      COMMAND "${CMAKE_COMMAND}"
        "-DCHOSCORDB_CTEST=${CMAKE_CTEST_COMMAND}"
        "-DCHOSCORDB_BUILD_DIR=${CMAKE_BINARY_DIR}"
        "-DCHOSCORDB_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DCHOSCORDB_LLVM_PROFDATA=${CHOSCORDB_LLVM_PROFDATA}"
        "-DCHOSCORDB_LLVM_COV=${CHOSCORDB_LLVM_COV}"
        "-DCHOSCORDB_COVERAGE_OBJECTS_FILE=${CMAKE_BINARY_DIR}/coverage-objects.txt"
        -P "${PROJECT_SOURCE_DIR}/cmake/RunCoverage.cmake"
      DEPENDS ${first_party_targets}
      COMMENT "Running CTest and exporting first-party C++ LCOV coverage"
      COMMAND_EXPAND_LISTS VERBATIM
    )
  endif()
endfunction()
