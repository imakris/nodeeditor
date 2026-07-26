find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

add_executable(test_nodes
  test_main.cpp
  src/TestAbstractGraphModel.cpp
  src/TestAbstractGraphModelSignals.cpp
  src/TestDataFlowGraphModel.cpp
  src/TestNodeDelegateModelRegistry.cpp
  src/TestConnectionId.cpp
  src/TestSerialization.cpp
  src/TestUndoCommands.cpp
  src/TestBasicGraphicsScene.cpp
  src/TestNodeGroup.cpp
  src/TestUIInteraction.cpp
  src/TestDataFlow.cpp
  src/TestNodeValidation.cpp
  src/TestCustomPainters.cpp
  src/TestCopyPaste.cpp
  src/TestZoomFeatures.cpp
  src/TestLoopDetection.cpp
  include/ApplicationSetup.hpp
  include/TestGraphModel.hpp
  include/UITestHelper.hpp
  include/TestDataFlowNodes.hpp
)

target_include_directories(test_nodes
  PRIVATE
    include
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

foreach(qt_nodes_library_mode IN ITEMS static shared)
  add_test(
    NAME qt_nodes_installed_consumer_${qt_nodes_library_mode}
    COMMAND
      "${CMAKE_COMMAND}"
      ${qt_nodes_installed_consumer_arguments}
      "-DQT_NODES_TEST_LIBRARY_MODE=${qt_nodes_library_mode}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_installed_consumer.cmake")
  set_tests_properties(
    qt_nodes_installed_consumer_${qt_nodes_library_mode}
    PROPERTIES
      TIMEOUT 300
      RUN_SERIAL TRUE)
endforeach()
