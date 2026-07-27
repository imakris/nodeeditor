#include "ApplicationSetup.hpp"
#include "TestGraphModel.hpp"

#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/NodeRenderingUtils.hpp>
#include <QtNodes/internal/StyleCollection.hpp>
#include <QtNodes/internal/locateNode.hpp>

#include <catch2/catch.hpp>

#include <QJsonObject>
#include <QSignalSpy>
#include <QVariantMap>

#include <algorithm>
#include <vector>

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

class Constraining_scene : public BasicGraphicsScene
{
public:
    explicit Constraining_scene(TestGraphModel &model)
    :
        BasicGraphicsScene(model)
    {}

    QPointF adjustedNodePosition(NodeGraphicsObject const &node,
                                 QPointF const &requestedPosition) const override
    {
        Q_UNUSED(node);
        return QPointF(
            std::min(requestedPosition.x(), m_max_position.x()),
            std::min(requestedPosition.y(), m_max_position.y()));
    }

private:
    QPointF m_max_position{120.0, 90.0};
};

class Counting_step_scene : public BasicGraphicsScene
{
public:
    explicit Counting_step_scene(TestGraphModel &model)
    :
        BasicGraphicsScene(model)
    {}

    QPointF adjustedNodePosition(NodeGraphicsObject const &node,
                                 QPointF const &requestedPosition) const override
    {
        Q_UNUSED(node);
        ++m_adjustment_count;

        if (requestedPosition.x() <= m_max_position.x()
            && requestedPosition.y() <= m_max_position.y()) {
            return requestedPosition;
        }

        return QPointF(
            std::max(requestedPosition.x() - k_step, m_max_position.x()),
            std::max(requestedPosition.y() - k_step, m_max_position.y()));
    }

    int adjustmentCount() const { return m_adjustment_count; }

    void resetAdjustmentCount() { m_adjustment_count = 0; }

private:
    static constexpr double k_step = 10.0;

    mutable int m_adjustment_count = 0;
    QPointF m_max_position{120.0, 90.0};
};

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

    SECTION("Nodes without explicit style fall back to collection defaults")
    {
        NodeId const nodeId = model.addNode("TestNode");
        QCoreApplication::processEvents();

        auto *nodeGraphics = scene.nodeGraphicsObject(nodeId);
        REQUIRE(nodeGraphics != nullptr);

        CHECK(nodeGraphics->opacity() == Approx(QtNodes::StyleCollection::nodeStyle().Opacity));
    }
}

TEST_CASE("BasicGraphicsScene adjusts node positions through one scene hook", "[graphics]")
{
    auto app = applicationSetup();
    TestGraphModel model;
    NodeId const nodeId = model.addNode("TestNode");
    std::vector<QPointF> positionsObservedBeforeScene;

    QObject::connect(&model,
                     &TestGraphModel::nodePositionUpdated,
                     [&](NodeId const updatedNodeId) {
                         if (updatedNodeId == nodeId) {
                             positionsObservedBeforeScene.push_back(
                                 model.nodeData<QPointF>(nodeId, NodeRole::Position));
                         }
                     });

    Constraining_scene scene(model);
    QCoreApplication::processEvents();

    auto *nodeGraphics = scene.nodeGraphicsObject(nodeId);
    REQUIRE(nodeGraphics != nullptr);

    SECTION("Direct graphics moves are adjusted before commit")
    {
        nodeGraphics->setPos(QPointF(220.0, 140.0));

        CHECK(nodeGraphics->pos().x() == Approx(120.0));
        CHECK(nodeGraphics->pos().y() == Approx(90.0));
    }

    SECTION("Model position writes are adjusted and written back")
    {
        std::vector<QPointF> positionsObservedAfterScene;
        QObject::connect(&model,
                         &TestGraphModel::nodePositionUpdated,
                         [&](NodeId const updatedNodeId) {
                             if (updatedNodeId == nodeId) {
                                 positionsObservedAfterScene.push_back(
                                     model.nodeData<QPointF>(nodeId, NodeRole::Position));
                             }
                         });

        QSignalSpy positionSpy(&model, &TestGraphModel::nodePositionUpdated);
        REQUIRE(positionSpy.isValid());

        CHECK(positionsObservedBeforeScene.empty());

        model.setNodeData(nodeId, NodeRole::Position, QPointF(220.0, 140.0));

        QPointF const modelPosition =
            model.nodeData(nodeId, NodeRole::Position).value<QPointF>();

        CHECK(positionSpy.count() == 2);
        REQUIRE(positionsObservedBeforeScene.size() == 2);
        CHECK(positionsObservedBeforeScene[0] == QPointF(220.0, 140.0));
        CHECK(positionsObservedBeforeScene[1] == QPointF(120.0, 90.0));
        REQUIRE(positionsObservedAfterScene.size() == 2);
        CHECK(positionsObservedAfterScene[0] == QPointF(120.0, 90.0));
        CHECK(positionsObservedAfterScene[1] == QPointF(120.0, 90.0));
        CHECK(modelPosition.x() == Approx(120.0));
        CHECK(modelPosition.y() == Approx(90.0));
        CHECK(nodeGraphics->pos().x() == Approx(120.0));
        CHECK(nodeGraphics->pos().y() == Approx(90.0));
    }
}

TEST_CASE("BasicGraphicsScene adjusts pre-positioned model nodes during population", "[graphics]")
{
    auto app = applicationSetup();
    TestGraphModel model;

    NodeId const nodeId = model.addNode("TestNode");
    model.setNodeData(nodeId, NodeRole::Position, QPointF(220.0, 140.0));

    Constraining_scene scene(model);
    std::vector<QPointF> positionsObservedAfterScene;
    QObject::connect(&model,
                     &TestGraphModel::nodePositionUpdated,
                     [&](NodeId const updatedNodeId) {
                         if (updatedNodeId == nodeId) {
                             positionsObservedAfterScene.push_back(
                                 model.nodeData<QPointF>(nodeId, NodeRole::Position));
                         }
                     });

    QSignalSpy positionSpy(&model, &TestGraphModel::nodePositionUpdated);
    REQUIRE(positionSpy.isValid());

    QCoreApplication::processEvents();

    auto *nodeGraphics = scene.nodeGraphicsObject(nodeId);
    REQUIRE(nodeGraphics != nullptr);

    QPointF const modelPosition =
        model.nodeData(nodeId, NodeRole::Position).value<QPointF>();

    CHECK(positionSpy.count() == 1);
    REQUIRE(positionsObservedAfterScene.size() == 1);
    CHECK(positionsObservedAfterScene[0] == QPointF(120.0, 90.0));
    CHECK(modelPosition.x() == Approx(120.0));
    CHECK(modelPosition.y() == Approx(90.0));
    CHECK(nodeGraphics->pos().x() == Approx(120.0));
    CHECK(nodeGraphics->pos().y() == Approx(90.0));
}

TEST_CASE("BasicGraphicsScene applies node position hook once for model-originated moves",
          "[graphics]")
{
    auto app = applicationSetup();
    TestGraphModel model;
    Counting_step_scene scene(model);

    NodeId const nodeId = model.addNode("TestNode");
    QCoreApplication::processEvents();

    auto *nodeGraphics = scene.nodeGraphicsObject(nodeId);
    REQUIRE(nodeGraphics != nullptr);

    scene.resetAdjustmentCount();
    model.setNodeData(nodeId, NodeRole::Position, QPointF(150.0, 120.0));

    QPointF const modelPosition =
        model.nodeData(nodeId, NodeRole::Position).value<QPointF>();

    CHECK(scene.adjustmentCount() == 1);
    CHECK(modelPosition.x() == Approx(140.0));
    CHECK(modelPosition.y() == Approx(110.0));
    CHECK(nodeGraphics->pos().x() == Approx(140.0));
    CHECK(nodeGraphics->pos().y() == Approx(110.0));
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
