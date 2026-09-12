# Optional explicit unsigned staging target; ordinary builds never deploy or sign.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CHOSCORDB_STAGE_OUTPUT "${CMAKE_BINARY_DIR}/unsigned-stage" CACHE PATH "New staging output directory")
set(CHOSCORDB_STAGE_QT_BIN "" CACHE PATH "Qt bin directory containing macdeployqt")
set(CHOSCORDB_STAGE_QSCINTILLA_PREFIX "" CACHE PATH "Verified QScintilla installation prefix")
set(CHOSCORDB_STAGE_SOURCE_MANIFEST "" CACHE FILEPATH "Verified source-candidate manifest")
add_custom_target(stage-application
  COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/release/stage.py"
    --build "${CMAKE_BINARY_DIR}" --output "${CHOSCORDB_STAGE_OUTPUT}"
    --qt-bin "${CHOSCORDB_STAGE_QT_BIN}"
    --qscintilla-prefix "${CHOSCORDB_STAGE_QSCINTILLA_PREFIX}"
    --source-candidate-manifest "${CHOSCORDB_STAGE_SOURCE_MANIFEST}"
  DEPENDS choscordb
  USES_TERMINAL VERBATIM)
