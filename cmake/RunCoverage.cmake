set(profile_dir "${CHOSCORDB_BUILD_DIR}/coverage-data/profiles")
set(profile_data "${CHOSCORDB_BUILD_DIR}/coverage-data/choscordb.profdata")
set(lcov_file "${CHOSCORDB_BUILD_DIR}/cpp.lcov")
file(REMOVE_RECURSE "${profile_dir}")
file(MAKE_DIRECTORY "${profile_dir}")

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "LLVM_PROFILE_FILE=${profile_dir}/%m-%p.profraw"
    QT_QPA_PLATFORM=offscreen
    "${CHOSCORDB_CTEST}" --test-dir "${CHOSCORDB_BUILD_DIR}" --output-on-failure
  RESULT_VARIABLE test_result
)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "CTest failed while collecting C++ coverage")
endif()

file(GLOB profiles "${profile_dir}/*.profraw")
if(NOT profiles)
  message(FATAL_ERROR "Coverage produced no LLVM raw profiles")
endif()
execute_process(COMMAND "${CHOSCORDB_LLVM_PROFDATA}" merge -sparse ${profiles} -o "${profile_data}" RESULT_VARIABLE merge_result)
if(NOT merge_result EQUAL 0)
  message(FATAL_ERROR "llvm-profdata failed to merge C++ coverage profiles")
endif()

file(STRINGS "${CHOSCORDB_COVERAGE_OBJECTS_FILE}" CHOSCORDB_COVERAGE_OBJECTS)
if(NOT CHOSCORDB_COVERAGE_OBJECTS)
  message(FATAL_ERROR "C++ coverage has no instrumented executable objects")
endif()
list(POP_FRONT CHOSCORDB_COVERAGE_OBJECTS primary_object)
set(object_arguments)
foreach(object IN LISTS CHOSCORDB_COVERAGE_OBJECTS)
  list(APPEND object_arguments -object "${object}")
endforeach()
execute_process(
  COMMAND "${CHOSCORDB_LLVM_COV}" export -format=lcov "-instr-profile=${profile_data}" "${primary_object}" ${object_arguments}
    "-ignore-filename-regex=(^|/)(build|third_party|_deps)/"
  OUTPUT_FILE "${lcov_file}"
  RESULT_VARIABLE export_result
)
if(NOT export_result EQUAL 0)
  message(FATAL_ERROR "llvm-cov failed to export the C++ LCOV report")
endif()
file(STRINGS "${lcov_file}" first_party_records REGEX "^SF:${CHOSCORDB_SOURCE_DIR}/(desktop|tests)/")
if(NOT first_party_records)
  message(FATAL_ERROR "C++ LCOV report contains no first-party source records")
endif()
message(STATUS "C++ LCOV report: ${lcov_file}")
