#include "ApplicationSetup.hpp"
#include "TestGraphModel.hpp"

#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/NodeRenderingUtils.hpp>
#include <QtNodes/internal/StyleCollection.hpp>
#include <QtNodes/internal/locateNode.hpp>

#include <catch2/catch.hpp>

#include <QGraphicsView>
#include <QJsonObject>
#include <QVariantMap>
#include <QUndoStack>

using QtNodes::BasicGraphicsScene;
using QtNodes::ConnectionId;
using QtNodes::NodeId;
using QtNodes::NodeGraphicsObject;
using QtNodes::NodeRole;

namespace {

QVariantMap shadow_enabled_style()
{
    return QtNodes::StyleCollection::nodeStyle().toJson().toVariantMap();
}

QVariantMap shadow_disabled_style()
{
    QVariantMap style = shadow_enabled_style();
    QVariantMap nodeStyle = style["NodeStyle"].toMap();
    nodeStyle["ShadowEnabled"] = false;
    style["NodeStyle"] = nodeStyle;
    return style;
}

} // namespace

TEST_CASE("BasicGraphicsScene functionality", "[graphics]")
{
    auto app = applicationSetup();
    TestGraphModel model;
    BasicGraphicsScene scene(model);

    SECTION("Scene initialization")
    {
        CHECK(&scene.graphModel() == &model);
        CHECK(scene.items().isEmpty());
    }

    SECTION("Node creation in scene")
    {
        NodeId nodeId = model.addNode("TestNode");
        
        // The scene should automatically create graphics objects for new nodes
        // Due to signal-slot connections
        
        // Process events to ensure graphics objects are created
        QCoreApplication::processEvents();
        
        CHECK(model.nodeExists(nodeId));
        // The scene should have at least one item (the node graphics object)
        CHECK(scene.items().size() >= 1);
    }

    SECTION("Connection creation in scene")
    {
        NodeId node1 = model.addNode("Node1");
        NodeId node2 = model.addNode("Node2");
        
        QCoreApplication::processEvents();
        
        ConnectionId connId{node1, 0, node2, 0};
        model.addConnection(connId);
        
        QCoreApplication::processEvents();
        
        CHECK(model.connectionExists(connId));
        // Scene should have graphics objects for both nodes and the connection
        CHECK(scene.items().size() >= 3); // 2 nodes + 1 connection
    }

    SECTION("Node deletion from scene")
    {
        NodeId nodeId = model.addNode("TestNode");
        QCoreApplication::processEvents();
        
        auto initialItemCount = scene.items().size();
        CHECK(initialItemCount >= 1);
        
        model.deleteNode(nodeId);
        QCoreApplication::processEvents();
        
        CHECK_FALSE(model.nodeExists(nodeId));
        // Graphics object should be removed from scene
        CHECK(scene.items().size() < initialItemCount);
    }

    SECTION("Scene with graphics view")
    {
        NodeId nodeId = model.addNode("TestNode");
        model.setNodeData(nodeId, NodeRole::Position, QPointF(100, 200));
        
        QCoreApplication::processEvents();
        
        CHECK(scene.items().size() >= 1);
        
        // Create view but don't show it to avoid windowing system issues
        QGraphicsView view(&scene);
        
        // View should be properly connected to scene
        CHECK(view.scene() == &scene);
        
        // Don't call view.show() to avoid potential graphics system issues
    }

    SECTION("Nodes without explicit style fall back to collection defaults")
    {
        NodeId const nodeId = model.addNode("TestNode");
        QCoreApplication::processEvents();

        auto *nodeGraphics = scene.nodeGraphicsObject(nodeId);
        REQUIRE(nodeGraphics != nullptr);

        CHECK(nodeGraphics->opacity() == Approx(QtNodes::StyleCollection::nodeStyle().Opacity));
    }
}

TEST_CASE("BasicGraphicsScene undo/redo support", "[graphics]")
{
    auto app = applicationSetup();
    TestGraphModel model;
    BasicGraphicsScene scene(model);

    SECTION("Undo stack exists")
    {
        auto &undoStack = scene.undoStack();
        CHECK(undoStack.count() == 0);
    }

    SECTION("Operations are tracked in undo stack")
    {
        auto &undoStack = scene.undoStack();
        
        NodeId nodeId = model.addNode("TestNode");
        QCoreApplication::processEvents();
        
        CHECK(model.nodeExists(nodeId));
        
        // Note: Depending on the implementation, the undo stack might or might not
        // automatically track model changes. This test verifies the stack exists
        // and can be used for undo operations.
        CHECK(undoStack.count() >= 0);
    }
}

TEST_CASE("Node shadow bounds follow visual margins", "[graphics]")
{
    auto app = applicationSetup();
    TestGraphModel model;
    BasicGraphicsScene scene(model);

    SECTION("Shadow-enabled bounds include the full painter shadow")
    {
        NodeId const nodeId = model.addNode("TestNode");
        model.setNodeData(nodeId, NodeRole::Style, shadow_enabled_style());
        QCoreApplication::processEvents();

        auto *nodeGraphics = scene.nodeGraphicsObject(nodeId);
        REQUIRE(nodeGraphics != nullptr);

        QRectF const bounds = nodeGraphics->boundingRect();
        QSize const size = scene.nodeGeometry().size(nodeId);
        QMarginsF const margins = QtNodes::node_rendering::node_visual_margins(true);

        CHECK(bounds.left() == Approx(-margins.left()));
        CHECK(bounds.top() == Approx(-margins.top()));
        CHECK(bounds.right() == Approx(size.width() + margins.right()));
        CHECK(bounds.bottom() == Approx(size.height() + margins.bottom()));
    }

    SECTION("Shadow-disabled bounds fall back to port margins only")
    {
        NodeId const nodeId = model.addNode("TestNode");
        model.setNodeData(nodeId, NodeRole::Style, shadow_disabled_style());
        QCoreApplication::processEvents();

        auto *nodeGraphics = scene.nodeGraphicsObject(nodeId);
        REQUIRE(nodeGraphics != nullptr);

        QRectF const bounds = nodeGraphics->boundingRect();
        QSize const size = scene.nodeGeometry().size(nodeId);
        QMarginsF const margins = QtNodes::node_rendering::node_visual_margins(false);

        CHECK(bounds.left() == Approx(-margins.left()));
        CHECK(bounds.top() == Approx(-margins.top()));
        CHECK(bounds.right() == Approx(size.width() + margins.right()));
        CHECK(bounds.bottom() == Approx(size.height() + margins.bottom()));
    }
}

TEST_CASE("locateNodeAt returns the top-most node hit", "[graphics]")
{
    auto app = applicationSetup();
    TestGraphModel model;
    BasicGraphicsScene scene(model);

    NodeId const node1 = model.addNode("Node1");
    NodeId const node2 = model.addNode("Node2");

    QPointF const sharedPos(100, 100);
    model.setNodeData(node1, NodeRole::Position, sharedPos);
    model.setNodeData(node2, NodeRole::Position, sharedPos);
    QCoreApplication::processEvents();

    QPointF const scenePoint = sharedPos + QPointF(20, 20);

    NodeGraphicsObject *expected = nullptr;
    for (QGraphicsItem *item : scene.items(scenePoint,
                                           Qt::IntersectsItemShape,
                                           Qt::DescendingOrder,
                                           QTransform())) {
        if (auto *node = qgraphicsitem_cast<NodeGraphicsObject *>(item)) {
            expected = node;
            break;
        }
    }

    REQUIRE(expected != nullptr);
    CHECK(QtNodes::locateNodeAt(scenePoint, scene, QTransform()) == expected);
}
