#include "ApplicationSetup.hpp"
#include "TestDataFlowNodes.hpp"

#include <catch2/catch.hpp>

#include <QtNodes/internal/BasicGraphicsScene.hpp>
#include <QtNodes/internal/DataFlowGraphModel.hpp>
#include <QtNodes/internal/GraphicsView.hpp>
#include <QtNodes/internal/GraphicsViewStyle.hpp>
#include <QtNodes/internal/GroupGraphicsObject.hpp>
#include <QtNodes/internal/NodeDelegateModelRegistry.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/NodeGroup.hpp>
#include <QtNodes/internal/NodeStyle.hpp>
#include <QtNodes/internal/StyleCollection.hpp>

#include <QtCore/QCoreApplication>
#include <QtCore/QRectF>
#include <QtGui/QColor>
#include <QtGui/QImage>
#include <QtGui/QPixmap>
#include <QtTest/QSignalSpy>
#include <QtWidgets/QGraphicsItem>

#include <memory>
#include <vector>

using QtNodes::BasicGraphicsScene;
using QtNodes::DataFlowGraphModel;
using QtNodes::GraphicsView;
using QtNodes::GraphicsViewStyle;
using QtNodes::NodeDelegateModelRegistry;
using QtNodes::NodeGraphicsObject;
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

/// Restores the process-wide default so one test cannot leak a style into the next.
class DefaultGraphicsViewStyleGuard
{
public:
    DefaultGraphicsViewStyleGuard()
    :
        _saved(StyleCollection::flowViewStyle())
    {}

    ~DefaultGraphicsViewStyleGuard() { StyleCollection::setGraphicsViewStyle(_saved); }

    DefaultGraphicsViewStyleGuard(DefaultGraphicsViewStyleGuard const &) = delete;
    DefaultGraphicsViewStyleGuard &operator=(DefaultGraphicsViewStyleGuard const &) = delete;

    GraphicsViewStyle const &saved() const { return _saved; }

private:
    GraphicsViewStyle _saved;
};

/**
 * Runs the scene's queued repaint bookkeeping to completion. QGraphicsScene
 * defers dirty-item processing to the event loop and the resulting changed()
 * emission to a further round, so a single processEvents() does not settle it.
 */
void settleQueuedSceneUpdates()
{
    constexpr int settleRounds = 8;

    for (int round = 0; round < settleRounds; ++round) {
        QCoreApplication::processEvents();
    }
}

std::shared_ptr<NodeDelegateModelRegistry> makeStyleTestRegistry()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();
    registry->registerModel<TestDisplayNode>();
    return registry;
}

/// Counts the pixels two same-sized grabs disagree on. A negative result means
/// the grabs cannot be compared at all.
int differingPixelCount(QImage const &left, QImage const &right)
{
    if (left.size() != right.size()) {
        return -1;
    }

    int differing = 0;

    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            if (left.pixel(x, y) != right.pixel(x, y)) {
                ++differing;
            }
        }
    }

    return differing;
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

// Resolving the style correctly is only half of the documented promise: an
// installed default has to reach the graphics objects that already exist
// (docs/guide/styling.rst). A NodeGraphicsObject copies Opacity out of the style
// into item state, and it renders through a device-coordinate cache, so the
// install has to re-apply the copied state and invalidate the cached rendering.
TEST_CASE("A default installed after the scene exists reaches existing nodes", "[style][graphics]")
{
    auto app = applicationSetup();

    DefaultNodeStyleGuard styleGuard;

    auto registry = makeStyleTestRegistry();
    DataFlowGraphModel model(registry);
    BasicGraphicsScene scene(model);

    NodeId const nodeId = model.addNode("TestDisplayNode");
    REQUIRE(nodeId != QtNodes::InvalidNodeId);

    auto *node = scene.nodeGraphicsObject(nodeId);
    REQUIRE(node != nullptr);
    REQUIRE(node->opacity() == Approx(styleGuard.saved().Opacity));

    SECTION("The installed opacity reaches a node that already exists")
    {
        NodeStyle installedDefault = styleGuard.saved();
        installedDefault.Opacity = styleGuard.saved().Opacity * 0.5f;
        REQUIRE(installedDefault.Opacity != styleGuard.saved().Opacity);

        StyleCollection::setNodeStyle(installedDefault);

        CHECK(node->opacity() == Approx(installedDefault.Opacity));
    }

    // A repaint REQUEST oracle, not a cache oracle: it proves the install does
    // not stop at the resolution layer. What the request is worth at the pixel
    // level is pinned separately, below, on the cached rendering path.
    SECTION("An install that only changes colours still requests a repaint")
    {
        QSignalSpy sceneChanged(&scene, &BasicGraphicsScene::changed);
        REQUIRE(sceneChanged.isValid());

        // Let the repaints queued by populating the scene finish, then prove the
        // scene is quiet. Without that proof the check below could pass on the
        // tail of the population instead of on the install.
        settleQueuedSceneUpdates();
        sceneChanged.clear();
        settleQueuedSceneUpdates();
        REQUIRE(sceneChanged.count() == 0);

        NodeStyle installedDefault = styleGuard.saved();
        installedDefault.GradientColor0 = QColor(180, 60, 30);
        REQUIRE(installedDefault.GradientColor0 != styleGuard.saved().GradientColor0);

        StyleCollection::setNodeStyle(installedDefault);

        settleQueuedSceneUpdates();

        CHECK(sceneChanged.count() > 0);
    }
}

// A node takes QGraphicsItem::DeviceCoordinateCache only under the Crisp
// rasterization policy, so that is the policy under which an install can be
// served from a stale pixmap. QWidget::grab() forces a full repaint, so a grab
// that comes back unchanged after a colour install can only be the item cache.
TEST_CASE("An install invalidates the pixmap a cached node renders from", "[style][graphics]")
{
    auto app = applicationSetup();

    DefaultNodeStyleGuard styleGuard;

    auto registry = makeStyleTestRegistry();
    DataFlowGraphModel model(registry);
    BasicGraphicsScene scene(model);

    GraphicsView view(&scene);
    view.setRasterizationPolicy(GraphicsView::RasterizationPolicy::Crisp);

    NodeId const nodeId = model.addNode("TestDisplayNode");
    REQUIRE(nodeId != QtNodes::InvalidNodeId);

    auto *node = scene.nodeGraphicsObject(nodeId);
    REQUIRE(node != nullptr);

    // Without this the test passes vacuously the day the cache path stops being
    // taken, which is exactly when it would stop being worth anything.
    REQUIRE(node->cacheMode() == QGraphicsItem::DeviceCoordinateCache);

    view.resize(500, 400);
    view.show();
    settleQueuedSceneUpdates();
    view.centerOn(node);
    settleQueuedSceneUpdates();

    QImage const before = view.grab().toImage();
    settleQueuedSceneUpdates();
    QImage const beforeAgain = view.grab().toImage();

    // A grab that is not reproducible on its own would make the comparison below
    // meaningless in either direction.
    REQUIRE(differingPixelCount(before, beforeAgain) == 0);

    NodeStyle installedDefault = styleGuard.saved();
    installedDefault.GradientColor0 = QColor(255, 0, 0);
    installedDefault.GradientColor1 = QColor(255, 0, 0);
    installedDefault.GradientColor2 = QColor(255, 0, 0);
    installedDefault.GradientColor3 = QColor(255, 0, 0);
    installedDefault.NormalBoundaryColor = QColor(0, 255, 0);

    StyleCollection::setNodeStyle(installedDefault);
    settleQueuedSceneUpdates();

    QImage const after = view.grab().toImage();

    CHECK(differingPixelCount(before, after) > 0);
}

// A group frame paints from its own constants, but the rect it paints into is
// stored, and it is the union of its member nodes' bounding rects, which carry
// the installed style's shadow margins. So the frame is style-derived state too
// and an install has to recompute it.
TEST_CASE("An install reaches the frame of a group that already exists", "[style][graphics]")
{
    auto app = applicationSetup();

    DefaultNodeStyleGuard styleGuard;

    auto registry = makeStyleTestRegistry();
    DataFlowGraphModel model(registry);
    BasicGraphicsScene scene(model);

    NodeId const nodeId = model.addNode("TestDisplayNode");
    REQUIRE(nodeId != QtNodes::InvalidNodeId);

    auto *node = scene.nodeGraphicsObject(nodeId);
    REQUIRE(node != nullptr);

    std::vector<NodeGraphicsObject *> members{node};
    auto group = scene.createGroup(members, QStringLiteral("StyleGroup")).lock();
    REQUIRE(group);

    QRectF const nodeRectBefore = node->boundingRect();
    QRectF const groupRectBefore = group->groupGraphicsObject().rect();

    NodeStyle installedDefault = styleGuard.saved();
    installedDefault.ShadowEnabled = !styleGuard.saved().ShadowEnabled;

    StyleCollection::setNodeStyle(installedDefault);
    settleQueuedSceneUpdates();

    QRectF const nodeRectAfter = node->boundingRect();

    // Precondition, not the behaviour under test: the shadow margins have to
    // actually move the node's extent, or the group has nothing to follow.
    REQUIRE(nodeRectAfter.size() != nodeRectBefore.size());

    // The group frame of a single-node group is that node's rect plus fixed
    // margins, so it has to move by exactly the same amount.
    QRectF const groupRectAfter = group->groupGraphicsObject().rect();

    CHECK(groupRectAfter.width() - groupRectBefore.width()
          == Approx(nodeRectAfter.width() - nodeRectBefore.width()));
    CHECK(groupRectAfter.height() - groupRectBefore.height()
          == Approx(nodeRectAfter.height() - nodeRectBefore.height()));
}

// The canvas carries the same promise. A view holds the background colour as its
// own brush instead of reading it per paint, so an installed default has to be
// pushed into the views that already exist.
TEST_CASE("A view style installed after the view exists reaches the view", "[style][graphics]")
{
    auto app = applicationSetup();

    DefaultGraphicsViewStyleGuard styleGuard;

    GraphicsView view;
    REQUIRE(view.backgroundBrush().color() == styleGuard.saved().BackgroundColor);

    GraphicsViewStyle installedDefault = styleGuard.saved();
    installedDefault.BackgroundColor = QColor(21, 34, 55);
    REQUIRE(installedDefault.BackgroundColor != styleGuard.saved().BackgroundColor);

    StyleCollection::setGraphicsViewStyle(installedDefault);

    CHECK(view.backgroundBrush().color() == installedDefault.BackgroundColor);
}
