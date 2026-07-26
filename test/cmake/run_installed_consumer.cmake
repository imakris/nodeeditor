cmake_minimum_required(VERSION 3.21)

foreach(required_variable IN ITEMS
    QT_NODES_SOURCE_DIR
    QT_NODES_TOP_BINARY_DIR
    QT_NODES_TEST_CONFIG
    QT_NODES_TEST_GENERATOR
    QT_NODES_TEST_CXX_COMPILER
    QT_NODES_TEST_QT6_DIR
    QT_NODES_TEST_PROVIDER_SOURCE_DIR
    QT_NODES_TEST_LIBRARY_MODE)
  if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
    message(FATAL_ERROR "${required_variable} is required.")
  endif()
endforeach()

if(QT_NODES_TEST_LIBRARY_MODE STREQUAL "static")
  set(build_shared_libs OFF)
  set(expect_provider TRUE)
elseif(QT_NODES_TEST_LIBRARY_MODE STREQUAL "shared")
  set(build_shared_libs ON)
  set(expect_provider FALSE)
else()
  message(FATAL_ERROR
    "Unknown QT_NODES_TEST_LIBRARY_MODE: ${QT_NODES_TEST_LIBRARY_MODE}")
endif()

set(test_root
  "${QT_NODES_TOP_BINARY_DIR}/installed-consumer-${QT_NODES_TEST_LIBRARY_MODE}")
set(producer_binary_dir "${test_root}/producer")
set(install_prefix "${test_root}/install")
set(consumer_binary_dir "${test_root}/consumer")
file(REMOVE_RECURSE "${test_root}")

set(producer_configure_command
  "${CMAKE_COMMAND}"
  -S "${QT_NODES_SOURCE_DIR}"
  -B "${producer_binary_dir}"
  -G "${QT_NODES_TEST_GENERATOR}"
  "-DBUILD_SHARED_LIBS=${build_shared_libs}"
  "-DBUILD_TESTING=OFF"
  "-DBUILD_EXAMPLES=OFF"
  "-DBUILD_DOCS=OFF"
  "-DVNM_QT_DISPATCH_SOURCE_DIR=${QT_NODES_TEST_PROVIDER_SOURCE_DIR}"
  "-DQt6_DIR=${QT_NODES_TEST_QT6_DIR}"
  "-DCMAKE_CXX_COMPILER=${QT_NODES_TEST_CXX_COMPILER}"
  "-DCMAKE_BUILD_TYPE=${QT_NODES_TEST_CONFIG}")
if(QT_NODES_TEST_GENERATOR_PLATFORM)
  list(APPEND producer_configure_command
    -A "${QT_NODES_TEST_GENERATOR_PLATFORM}")
endif()
if(QT_NODES_TEST_GENERATOR_TOOLSET)
  list(APPEND producer_configure_command
    -T "${QT_NODES_TEST_GENERATOR_TOOLSET}")
endif()
if(QT_NODES_TEST_MAKE_PROGRAM)
  list(APPEND producer_configure_command
    "-DCMAKE_MAKE_PROGRAM=${QT_NODES_TEST_MAKE_PROGRAM}")
endif()

execute_process(
  COMMAND ${producer_configure_command}
  RESULT_VARIABLE producer_configure_result
  OUTPUT_VARIABLE producer_configure_output
  ERROR_VARIABLE producer_configure_error)
if(NOT producer_configure_result EQUAL 0)
  message(FATAL_ERROR
    "QtNodes ${QT_NODES_TEST_LIBRARY_MODE} producer configure failed.\n"
    "${producer_configure_output}\n${producer_configure_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${producer_binary_dir}"
    --config "${QT_NODES_TEST_CONFIG}"
  RESULT_VARIABLE producer_build_result
  OUTPUT_VARIABLE producer_build_output
  ERROR_VARIABLE producer_build_error)
if(NOT producer_build_result EQUAL 0)
  message(FATAL_ERROR
    "QtNodes ${QT_NODES_TEST_LIBRARY_MODE} producer build failed.\n"
    "${producer_build_output}\n${producer_build_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --install "${producer_binary_dir}"
    --prefix "${install_prefix}"
    --config "${QT_NODES_TEST_CONFIG}"
  RESULT_VARIABLE producer_install_result
  OUTPUT_VARIABLE producer_install_output
  ERROR_VARIABLE producer_install_error)
if(NOT producer_install_result EQUAL 0)
  message(FATAL_ERROR
    "QtNodes ${QT_NODES_TEST_LIBRARY_MODE} producer install failed.\n"
    "${producer_install_output}\n${producer_install_error}")
endif()

file(GLOB_RECURSE qt_nodes_config_paths
  LIST_DIRECTORIES FALSE
  "${install_prefix}/QtNodesConfig.cmake")
list(LENGTH qt_nodes_config_paths qt_nodes_config_count)
if(NOT qt_nodes_config_count EQUAL 1)
  message(FATAL_ERROR
    "Expected one staged QtNodesConfig.cmake, found "
    "${qt_nodes_config_count}.")
endif()
list(GET qt_nodes_config_paths 0 qt_nodes_config_path)
get_filename_component(qt_nodes_package_dir "${qt_nodes_config_path}" DIRECTORY)

set(consumer_configure_command
  "${CMAKE_COMMAND}"
  -S "${CMAKE_CURRENT_LIST_DIR}/installed_consumer"
  -B "${consumer_binary_dir}"
  -G "${QT_NODES_TEST_GENERATOR}"
  "-DQtNodes_DIR:PATH=${qt_nodes_package_dir}"
  "-DQT_NODES_EXPECTED_DIR:PATH=${qt_nodes_package_dir}"
  "-DQT_NODES_EXPECT_PROVIDER=${expect_provider}"
  "-DQt6_DIR=${QT_NODES_TEST_QT6_DIR}"
  "-DCMAKE_CXX_COMPILER=${QT_NODES_TEST_CXX_COMPILER}"
  "-DCMAKE_FIND_USE_PACKAGE_ROOT_PATH=FALSE"
  "-DCMAKE_FIND_USE_PACKAGE_REGISTRY=FALSE"
  "-DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=FALSE"
  "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=TRUE"
  "-DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=TRUE"
  "-DCMAKE_BUILD_TYPE=${QT_NODES_TEST_CONFIG}")
if(expect_provider)
  file(GLOB_RECURSE provider_config_paths
    LIST_DIRECTORIES FALSE
    "${install_prefix}/vnm_qt_dispatchConfig.cmake")
  list(LENGTH provider_config_paths provider_config_count)
  if(NOT provider_config_count EQUAL 1)
    message(FATAL_ERROR
      "Expected one staged vnm_qt_dispatchConfig.cmake, found "
      "${provider_config_count}.")
  endif()
  list(GET provider_config_paths 0 provider_config_path)
  get_filename_component(provider_package_dir "${provider_config_path}" DIRECTORY)
  list(APPEND consumer_configure_command
    "-Dvnm_qt_dispatch_DIR:PATH=${provider_package_dir}")
else()
  list(APPEND consumer_configure_command
    "-DCMAKE_DISABLE_FIND_PACKAGE_vnm_qt_dispatch=TRUE")
endif()
if(QT_NODES_TEST_GENERATOR_PLATFORM)
  list(APPEND consumer_configure_command
    -A "${QT_NODES_TEST_GENERATOR_PLATFORM}")
endif()
if(QT_NODES_TEST_GENERATOR_TOOLSET)
  list(APPEND consumer_configure_command
    -T "${QT_NODES_TEST_GENERATOR_TOOLSET}")
endif()
if(QT_NODES_TEST_MAKE_PROGRAM)
  list(APPEND consumer_configure_command
    "-DCMAKE_MAKE_PROGRAM=${QT_NODES_TEST_MAKE_PROGRAM}")
endif()

execute_process(
  COMMAND ${consumer_configure_command}
  RESULT_VARIABLE consumer_configure_result
  OUTPUT_VARIABLE consumer_configure_output
  ERROR_VARIABLE consumer_configure_error)
if(NOT consumer_configure_result EQUAL 0)
  message(FATAL_ERROR
    "QtNodes ${QT_NODES_TEST_LIBRARY_MODE} consumer configure failed.\n"
    "${consumer_configure_output}\n${consumer_configure_error}")
endif()

if(expect_provider)
  file(STRINGS "${consumer_binary_dir}/CMakeCache.txt" provider_cache_entries
    REGEX "^vnm_qt_dispatch_DIR:PATH=")
  list(LENGTH provider_cache_entries provider_cache_entry_count)
  if(NOT provider_cache_entry_count EQUAL 1)
    message(FATAL_ERROR
      "The static consumer cache does not contain exactly one "
      "vnm_qt_dispatch_DIR entry.")
  endif()
  list(GET provider_cache_entries 0 provider_cache_entry)
  string(REGEX REPLACE "^[^=]*=" "" resolved_provider_dir
    "${provider_cache_entry}")
  file(REAL_PATH "${resolved_provider_dir}" resolved_provider_dir)
  file(REAL_PATH "${provider_package_dir}" provider_package_dir)
  if(NOT resolved_provider_dir STREQUAL provider_package_dir)
    message(FATAL_ERROR
      "The static consumer resolved vnm_qt_dispatch outside the staged "
      "prefix:\n"
      "  expected=${provider_package_dir}\n"
      "  actual=${resolved_provider_dir}")
  endif()
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" --build "${consumer_binary_dir}"
    --config "${QT_NODES_TEST_CONFIG}"
  RESULT_VARIABLE consumer_build_result
  OUTPUT_VARIABLE consumer_build_output
  ERROR_VARIABLE consumer_build_error)
if(NOT consumer_build_result EQUAL 0)
  message(FATAL_ERROR
    "QtNodes ${QT_NODES_TEST_LIBRARY_MODE} consumer build failed.\n"
    "${consumer_build_output}\n${consumer_build_error}")
endif()
