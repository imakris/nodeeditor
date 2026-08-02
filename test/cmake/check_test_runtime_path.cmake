file(TO_CMAKE_PATH "${QT_NODES_EXPECTED_RUNTIME_DIR}" expected_runtime_path)
file(TO_CMAKE_PATH "$ENV{PATH}" runtime_path)
list(GET runtime_path 0 actual_runtime_path)

if(NOT actual_runtime_path STREQUAL expected_runtime_path)
  message(FATAL_ERROR
    "Expected Qt runtime directory '${expected_runtime_path}' at the front of "
    "PATH, got '${actual_runtime_path}'.")
endif()

message(STATUS "Qt runtime PATH prefix: ${actual_runtime_path}")
