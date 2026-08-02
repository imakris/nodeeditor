find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

add_executable(test_nodes
  test_main.cpp
  src/TestAbstractGraphModel.cpp
  src/TestAbstractGraphModelSignals.cpp
  src/TestDataFlowGraphModel.cpp
  src/TestNodeDelegateModelRegistry.cpp
  src/TestConnectionId.cpp
  src/TestDynamicPortsSerialization.cpp
  src/TestSerialization.cpp
  src/TestUndoCommands.cpp
  src/TestBasicGraphicsScene.cpp
  src/TestNodeGroup.cpp
  src/TestUIInteraction.cpp
  src/TestDataFlow.cpp
  src/TestNodeValidation.cpp
  src/TestCustomPainters.cpp
  src/TestPortHitTesting.cpp
  src/TestNodeStyleResolution.cpp
  src/TestCopyPaste.cpp
  src/TestZoomFeatures.cpp
  src/TestLoopDetection.cpp
  include/ApplicationSetup.hpp
  include/TestGraphModel.hpp
  include/UITestHelper.hpp
  include/TestDataFlowNodes.hpp
  ../examples/dynamic_ports/DynamicPortsModel.cpp
  ../examples/dynamic_ports/DynamicPortsModel.hpp
  ../examples/dynamic_ports/DynamicPortsActions.cpp
  ../examples/dynamic_ports/DynamicPortsActions.hpp
  ../examples/dynamic_ports/PortAddRemoveWidget.cpp
  ../examples/dynamic_ports/PortAddRemoveWidget.hpp
)

target_include_directories(test_nodes
  PRIVATE
    include
    ../examples/dynamic_ports
)

target_link_libraries(test_nodes
  PRIVATE
    QtNodes::QtNodes
    Catch2::Catch2
    Qt6::Test
)

add_test(
  NAME test_nodes
  COMMAND
    $<TARGET_FILE:test_nodes>
    $<$<BOOL:${QT_NODES_FORCE_TEST_COLOR}>:--use-colour=yes>
)

# The suite runs in a few seconds. The budget only exists so a wedged run, or a
# launch that blocks on a Windows missing-DLL hard error, fails instead of
# hanging the whole gate.
set_tests_properties(test_nodes PROPERTIES TIMEOUT 300)

if(WIN32)
  add_test(
    NAME qt_nodes_test_runtime_path
    COMMAND
      "${CMAKE_COMMAND}"
      "-DQT_NODES_EXPECTED_RUNTIME_DIR=$<SHELL_PATH:$<TARGET_FILE_DIR:Qt6::Core>>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_test_runtime_path.cmake")

  # Windows has no RPATH and the Qt runtime does not sit next to the test binary,
  # so launching with the wrong PATH does not fail: the loader stops on the
  # missing-DLL hard error, which produces a test that emits nothing and then
  # trips its budget. That reads as a hung suite and has already been reported as
  # one. Point the tests at the Qt this tree was configured against so the
  # environment cannot fake a product defect.
  set(qt_nodes_windows_runtime_tests
    test_nodes
    qt_nodes_test_runtime_path)
  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.22)
    set_tests_properties(${qt_nodes_windows_runtime_tests} PROPERTIES
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<SHELL_PATH:$<TARGET_FILE_DIR:Qt6::Core>>")
  else()
    set(qt_nodes_test_path
      "$<SHELL_PATH:$<TARGET_FILE_DIR:Qt6::Core>>;$ENV{PATH}")
    string(REPLACE ";" "\\;" qt_nodes_test_path "${qt_nodes_test_path}")
    set_tests_properties(${qt_nodes_windows_runtime_tests} PROPERTIES
      ENVIRONMENT "PATH=${qt_nodes_test_path}")
  endif()
endif()

set(qt_nodes_installed_consumer_arguments
  "-DQT_NODES_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
  "-DQT_NODES_TOP_BINARY_DIR=${PROJECT_BINARY_DIR}"
  "-DQT_NODES_TEST_CONFIG=$<CONFIG>"
  "-DQT_NODES_TEST_GENERATOR=${CMAKE_GENERATOR}"
  "-DQT_NODES_TEST_GENERATOR_PLATFORM=${CMAKE_GENERATOR_PLATFORM}"
  "-DQT_NODES_TEST_GENERATOR_TOOLSET=${CMAKE_GENERATOR_TOOLSET}"
  "-DQT_NODES_TEST_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}"
  "-DQT_NODES_TEST_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
  "-DQT_NODES_TEST_QT6_DIR=${Qt6_DIR}"
  "-DQT_NODES_TEST_PROVIDER_SOURCE_DIR=${qt_nodes_qt_dispatch_SOURCE_DIR}"
)
if(WIN32)
  list(APPEND qt_nodes_installed_consumer_arguments
    "-DQT_NODES_TEST_QT_RUNTIME_DIR=$<TARGET_FILE_DIR:Qt6::Core>")
endif()

foreach(qt_nodes_library_mode IN ITEMS static shared)
  add_test(
    NAME qt_nodes_installed_consumer_${qt_nodes_library_mode}
    COMMAND
      "${CMAKE_COMMAND}"
      ${qt_nodes_installed_consumer_arguments}
      "-DQT_NODES_TEST_LIBRARY_MODE=${qt_nodes_library_mode}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_installed_consumer.cmake")
  # Each mode configures, builds and installs the whole library from scratch and
  # then configures and builds a consumer against the staged package. That took
  # 295 s of the previous 300 s budget on a developer workstation, so the gate
  # failed on timing noise rather than on a defect.
  set_tests_properties(
    qt_nodes_installed_consumer_${qt_nodes_library_mode}
    PROPERTIES
      TIMEOUT 1800
      RUN_SERIAL TRUE)
endforeach()
