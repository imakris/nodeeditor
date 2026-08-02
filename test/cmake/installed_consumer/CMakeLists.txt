cmake_minimum_required(VERSION 3.21)

project(qt_nodes_installed_consumer LANGUAGES CXX)

foreach(required_variable IN ITEMS
    QT_NODES_EXPECTED_DIR
    QT_NODES_EXPECT_PROVIDER)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required.")
  endif()
endforeach()

find_package(
  QtNodes
  CONFIG
  REQUIRED
  PATHS "${QT_NODES_EXPECTED_DIR}"
  NO_DEFAULT_PATH)

file(REAL_PATH "${QtNodes_DIR}" resolved_qt_nodes_dir)
file(REAL_PATH "${QT_NODES_EXPECTED_DIR}" expected_qt_nodes_dir)
if(NOT resolved_qt_nodes_dir STREQUAL expected_qt_nodes_dir)
  message(FATAL_ERROR
    "QtNodes resolved outside the staged prefix:\n"
    "  expected=${expected_qt_nodes_dir}\n"
    "  actual=${resolved_qt_nodes_dir}")
endif()

if(QT_NODES_EXPECT_PROVIDER)
  if(NOT TARGET vnm_qt_dispatch::vnm_qt_dispatch)
    message(FATAL_ERROR
      "The static QtNodes package did not resolve vnm_qt_dispatch.")
  endif()
else()
  if(TARGET vnm_qt_dispatch::vnm_qt_dispatch)
    message(FATAL_ERROR
      "The shared QtNodes package resolved an unnecessary "
      "vnm_qt_dispatch dependency.")
  endif()
endif()

add_executable(qt_nodes_installed_consumer main.cpp)
target_link_libraries(
  qt_nodes_installed_consumer
  PRIVATE
    QtNodes::QtNodes)

get_target_property(qt_nodes_library_type QtNodes::QtNodes TYPE)
if(qt_nodes_library_type STREQUAL "SHARED_LIBRARY")
  add_custom_command(
    TARGET qt_nodes_installed_consumer
    POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:QtNodes::QtNodes>"
      "$<TARGET_FILE_DIR:qt_nodes_installed_consumer>")
endif()
