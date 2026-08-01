#include "ApplicationSetup.hpp"
#include "TestDataFlowNodes.hpp"

#include <catch2/catch.hpp>

#include <QtNodes/internal/DataFlowGraphModel.hpp>
#include <QtNodes/internal/NodeDelegateModelRegistry.hpp>
#include <QtNodes/internal/NodeStyle.hpp>
#include <QtNodes/internal/StyleCollection.hpp>

#include <memory>

using QtNodes::DataFlowGraphModel;
using QtNodes::NodeDelegateModelRegistry;
using QtNodes::NodeId;
using QtNodes::NodeStyle;
using QtNodes::StyleCollection;

namespace {

/// Restores the process-wide default so one test cannot leak a style into the next.
class DefaultNodeStyleGuard
{
public:
    DefaultNodeStyleGuard()
    :
        _saved(StyleCollection::nodeStyle())
    {}

    ~DefaultNodeStyleGuard() { StyleCollection::setNodeStyle(_saved); }

    DefaultNodeStyleGuard(DefaultNodeStyleGuard const &) = delete;
    DefaultNodeStyleGuard &operator=(DefaultNodeStyleGuard const &) = delete;

    NodeStyle const &saved() const { return _saved; }

private:
    NodeStyle _saved;
};

std::shared_ptr<NodeDelegateModelRegistry> makeStyleTestRegistry()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();
    registry->registerModel<TestDisplayNode>();
    return registry;
}

} // namespace

// A delegate model holds a style only when one was installed on it; otherwise it
// resolves the current StyleCollection default. Snapshotting the default at
// construction made a global style install silently ineffective on every model
// that already existed, unlike the sibling ConnectionStyle and GraphicsViewStyle
// entry points, which take effect immediately.
TEST_CASE("A delegate model without its own style follows the global default", "[style]")
{
    auto app = applicationSetup();

    DefaultNodeStyleGuard styleGuard;

    auto registry = makeStyleTestRegistry();
    DataFlowGraphModel model(registry);

    NodeId const nodeId = model.addNode("TestDisplayNode");
    REQUIRE(nodeId != QtNodes::InvalidNodeId);

    auto *delegate = model.delegateModel<TestDisplayNode>(nodeId);
    REQUIRE(delegate != nullptr);

    NodeStyle installedDefault = styleGuard.saved();
    installedDefault.ConnectionPointDiameter = styleGuard.saved().ConnectionPointDiameter * 3.0f;
    StyleCollection::setNodeStyle(installedDefault);

    SECTION("A default installed after the node was created reaches the node")
    {
        CHECK(delegate->nodeStyle().ConnectionPointDiameter
              == installedDefault.ConnectionPointDiameter);
    }

    SECTION("A style installed on the node itself wins over the default")
    {
        NodeStyle ownStyle = styleGuard.saved();
        ownStyle.ConnectionPointDiameter = styleGuard.saved().ConnectionPointDiameter * 7.0f;
        delegate->setNodeStyle(ownStyle);

        CHECK(delegate->nodeStyle().ConnectionPointDiameter == ownStyle.ConnectionPointDiameter);
    }
}
