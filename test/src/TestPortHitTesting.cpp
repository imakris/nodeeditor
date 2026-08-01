#include "ApplicationSetup.hpp"
#include "TestDataFlowNodes.hpp"

#include <catch2/catch.hpp>

#include <QtNodes/internal/AbstractNodeGeometry.hpp>
#include <QtNodes/internal/BasicGraphicsScene.hpp>
#include <QtNodes/internal/DataFlowGraphModel.hpp>
#include <QtNodes/internal/NodeDelegateModelRegistry.hpp>
#include <QtNodes/internal/NodeStyle.hpp>
#include <QtNodes/internal/StyleCollection.hpp>

#include <memory>

using QtNodes::AbstractNodeGeometry;
using QtNodes::BasicGraphicsScene;
using QtNodes::DataFlowGraphModel;
using QtNodes::InvalidPortIndex;
using QtNodes::NodeDelegateModelRegistry;
using QtNodes::NodeId;
using QtNodes::NodeStyle;
using QtNodes::PortType;
using QtNodes::StyleCollection;

namespace {

std::shared_ptr<NodeDelegateModelRegistry> makePortHitTestRegistry()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();
    registry->registerModel<TestDisplayNode>();
    return registry;
}

void applyPortDiameter(DataFlowGraphModel &model, NodeId nodeId, float diameter)
{
    auto *delegate = model.delegateModel<TestDisplayNode>(nodeId);
    REQUIRE(delegate != nullptr);

    NodeStyle style = delegate->nodeStyle();
    style.ConnectionPointDiameter = diameter;
    delegate->setNodeStyle(style);
}

} // namespace

// DefaultNodePainter sizes the drawn port circles from the per-node style
// (radius = ConnectionPointDiameter * 0.6).  The clickable region must be
// derived from the same style, otherwise a node with its own style draws its
// ports in one place and accepts clicks in another.
TEST_CASE("Port hit testing follows the per-node style", "[geometry][style]")
{
    auto app = applicationSetup();

    auto registry = makePortHitTestRegistry();
    DataFlowGraphModel model(registry);
    BasicGraphicsScene scene(model);

    NodeId const nodeId = model.addNode("TestDisplayNode");
    REQUIRE(nodeId != QtNodes::InvalidNodeId);

    AbstractNodeGeometry &geometry = scene.nodeGeometry();
    QPointF const portPosition = geometry.portPosition(nodeId, PortType::In, 0);

    float const globalDiameter = StyleCollection::nodeStyle().ConnectionPointDiameter;
    REQUIRE(globalDiameter > 0.0f);

    SECTION("A larger per-node port is clickable over its whole drawn extent")
    {
        applyPortDiameter(model, nodeId, globalDiameter * 4.0f);

        // Inside the node's own tolerance (8 * globalDiameter) but outside the
        // global one (2 * globalDiameter).
        QPointF const probe = portPosition + QPointF(globalDiameter * 4.0, 0.0);

        CHECK(geometry.checkPortHit(nodeId, PortType::In, probe) == 0);
    }

    SECTION("A smaller per-node port does not capture clicks outside its drawn extent")
    {
        applyPortDiameter(model, nodeId, globalDiameter * 0.25f);

        // Outside the node's own tolerance (0.5 * globalDiameter) but inside
        // the global one (2 * globalDiameter).
        QPointF const probe = portPosition + QPointF(globalDiameter, 0.0);

        CHECK(geometry.checkPortHit(nodeId, PortType::In, probe) == InvalidPortIndex);
    }
}
