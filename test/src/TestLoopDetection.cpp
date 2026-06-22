#include "ApplicationSetup.hpp"
#include "TestGraphModel.hpp"
#include "TestDataFlowNodes.hpp"

#include <catch2/catch.hpp>

#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/NodeDelegateModelRegistry>

using QtNodes::ConnectionId;
using QtNodes::DataFlowGraphModel;
using QtNodes::NodeDelegateModelRegistry;
using QtNodes::NodeId;

TEST_CASE("Loop detection configuration", "[loops]")
{
    SECTION("Default AbstractGraphModel allows loops")
    {
        TestGraphModel model;
        CHECK(model.loopsEnabled() == true);
    }

    SECTION("DataFlowGraphModel disables loops by default")
    {
        auto app = applicationSetup();
        auto registry = std::make_shared<NodeDelegateModelRegistry>();
        registry->registerModel<TestSourceNode>("Sources");

        DataFlowGraphModel model(registry);
        CHECK(model.loopsEnabled() == false);
    }
}

TEST_CASE("Loop detection in DataFlowGraphModel", "[loops]")
{
    auto app = applicationSetup();
    auto registry = std::make_shared<NodeDelegateModelRegistry>();
    registry->registerModel<TestSourceNode>("Sources");
    registry->registerModel<TestDisplayNode>("Sinks");

    DataFlowGraphModel model(registry);

    SECTION("Direct self-loop is not possible")
    {
        NodeId node1 = model.addNode("TestSourceNode");

        // Try to connect node to itself
        ConnectionId selfLoop{node1, 0, node1, 0};
        CHECK_FALSE(model.connectionPossible(selfLoop));
    }

    SECTION("Simple A->B connection is allowed")
    {
        NodeId node1 = model.addNode("TestSourceNode");
        NodeId node2 = model.addNode("TestDisplayNode");

        ConnectionId conn{node1, 0, node2, 0};
        CHECK(model.connectionPossible(conn));

        model.addConnection(conn);
        CHECK(model.connectionExists(conn));
    }

    SECTION("Indirect loop A->B->A is prevented")
    {
        // Use TestDisplayNode which has both input and output ports
        NodeId node1 = model.addNode("TestDisplayNode");
        NodeId node2 = model.addNode("TestDisplayNode");

        // Create A->B connection
        ConnectionId conn1{node1, 0, node2, 0};
        CHECK(model.connectionPossible(conn1));
        model.addConnection(conn1);

        // Try to create B->A connection (would form a loop)
        ConnectionId conn2{node2, 0, node1, 0};
        CHECK_FALSE(model.connectionPossible(conn2));
    }

    SECTION("Three node loop A->B->C->A is prevented")
    {
        // Use TestDisplayNode which has both input and output ports
        NodeId node1 = model.addNode("TestDisplayNode");
        NodeId node2 = model.addNode("TestDisplayNode");
        NodeId node3 = model.addNode("TestDisplayNode");

        // Create A->B
        ConnectionId conn1{node1, 0, node2, 0};
        model.addConnection(conn1);
        CHECK(model.connectionExists(conn1));

        // Create B->C
        ConnectionId conn2{node2, 0, node3, 0};
        model.addConnection(conn2);
        CHECK(model.connectionExists(conn2));

        // Try to create C->A (would form a loop)
        ConnectionId conn3{node3, 0, node1, 0};
        CHECK_FALSE(model.connectionPossible(conn3));
    }

    SECTION("Loop-detection cache stays correct across topology changes")
    {
        NodeId node1 = model.addNode("TestDisplayNode");
        NodeId node2 = model.addNode("TestDisplayNode");

        ConnectionId aToB{node1, 0, node2, 0};
        ConnectionId bToA{node2, 0, node1, 0};

        // Probe both directions first so the loop DFS is memoized for each node pair.
        CHECK(model.connectionPossible(aToB));
        CHECK(model.connectionPossible(bToA));

        // Realizing A->B must invalidate the memoized result for B->A...
        model.addConnection(aToB);
        CHECK_FALSE(model.connectionPossible(bToA)); // would close a cycle

        // ...and removing it must make B->A possible again.
        model.deleteConnection(aToB);
        CHECK(model.connectionPossible(bToA));
    }
}
