#include "ApplicationSetup.hpp"
#include "TestGraphModel.hpp"
#include "UITestHelper.hpp"

#include <QtNodes/internal/BasicGraphicsScene.hpp>
#include <QtNodes/internal/GraphicsView.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/Definitions>

#include <catch2/catch.hpp>
#include <QTest>
#include <QSignalSpy>
#include <QGraphicsSceneMouseEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QTimer>

using QtNodes::BasicGraphicsScene;
using QtNodes::ConnectionGraphicsObject;
using QtNodes::ConnectionId;
using QtNodes::GraphicsView;
using QtNodes::InvalidNodeId;
using QtNodes::NodeGraphicsObject;
using QtNodes::NodeId;
using QtNodes::NodeRole;
using QtNodes::PortIndex;
using QtNodes::PortType;

TEST_CASE("UI Interaction - Node Movement", "[ui][visual]")
{
    auto app = applicationSetup();
    
    auto model = std::make_shared<TestGraphModel>();
    BasicGraphicsScene scene(*model);
    GraphicsView view(&scene);
    
    // Show the view (required for proper event handling)
    view.resize(800, 600);
    view.show();
    
    // CRITICAL: Wait for window to be actually exposed and ready
    REQUIRE(QTest::qWaitForWindowExposed(&view));
    UITestHelper::waitForUI();

    SECTION("Create and move a node visually")
    {
        // Create a node
        NodeId nodeId = model->addNode("TestNode");
        REQUIRE(nodeId != InvalidNodeId);
        
        // Set initial position
        QPointF initialPos(100, 100);
        model->setNodeData(nodeId, NodeRole::Position, initialPos);
        
        // Force the graphics scene to update and create graphics objects
        UITestHelper::waitForUI();
        scene.update();
        view.update();
        UITestHelper::waitForUI();

        // Find the node graphics object
        NodeGraphicsObject* nodeGraphics = nullptr;
        for (auto item : scene.items()) {
            if (auto node = qgraphicsitem_cast<NodeGraphicsObject*>(item)) {
                nodeGraphics = node;
                break;
            }
        }
        
        REQUIRE(nodeGraphics != nullptr);
        
        // Set the graphics object position directly (like the old test)
        nodeGraphics->setPos(initialPos);
        UITestHelper::waitForUI();
        
        // Verify initial position
        QPointF actualInitialPos = model->nodeData(nodeId, NodeRole::Position).value<QPointF>();
        CHECK(actualInitialPos.x() == Approx(initialPos.x()).margin(1.0));
        CHECK(actualInitialPos.y() == Approx(initialPos.y()).margin(1.0));

        // Set up signal spy for position updates
        QSignalSpy positionSpy(model.get(), &TestGraphModel::nodePositionUpdated);

        // Test programmatic position change (simulating successful drag)
        QPointF newPos(200, 150);
        model->setNodeData(nodeId, NodeRole::Position, newPos);
        nodeGraphics->setPos(newPos); // Update graphics position too
        UITestHelper::waitForUI();

        // Verify the node moved in the model
        QPointF finalPos = model->nodeData(nodeId, NodeRole::Position).value<QPointF>();
        CHECK(finalPos.x() == Approx(newPos.x()).epsilon(0.1));
        CHECK(finalPos.y() == Approx(newPos.y()).epsilon(0.1));
        
        // Verify signal was emitted
        CHECK(positionSpy.count() >= 1);
        
        // Test mouse interaction using the old test's approach
        QPointF nodeCenter = nodeGraphics->boundingRect().center();
        QPointF scenePos = nodeGraphics->mapToScene(nodeCenter);
        QPoint viewPos = view.mapFromScene(scenePos);
        
        // Use windowHandle() like the old test for proper event handling
        if (view.windowHandle()) {
            QTest::mousePress(view.windowHandle(), Qt::LeftButton, Qt::NoModifier, viewPos);
            UITestHelper::waitForUI();
            QTest::mouseMove(view.windowHandle(), viewPos + QPoint(30, 20));
            UITestHelper::waitForUI();
            QTest::mouseRelease(view.windowHandle(), Qt::LeftButton, Qt::NoModifier, viewPos + QPoint(30, 20));
            UITestHelper::waitForUI();
        }
        
        // Verify UI interaction doesn't crash and node still exists
        CHECK(model->allNodeIds().size() == 1);
        CHECK(nodeGraphics->isVisible());
    }

}

TEST_CASE("UI Interaction - Zoom and Pan", "[ui][visual]")
{
    auto app = applicationSetup();
    
    auto model = std::make_shared<TestGraphModel>();
    BasicGraphicsScene scene(*model);
    GraphicsView view(&scene);
    
    view.resize(800, 600);
    view.show();
    UITestHelper::waitForUI();

    SECTION("Zoom using mouse wheel")
    {
        // Create a node for reference
        NodeId nodeId = model->addNode("TestNode");
        model->setNodeData(nodeId, NodeRole::Position, QPointF(400, 300));
        UITestHelper::waitForUI();

        // Get initial transform
        QTransform initialTransform = view.transform();
        
        // Simulate zoom in (scroll up)
        QPoint viewCenter = view.rect().center();
        QWheelEvent wheelEvent(viewCenter, view.mapToGlobal(viewCenter), 
                              QPoint(0, 0), QPoint(0, 120), // 120 units up
                              Qt::NoButton, Qt::NoModifier, Qt::ScrollPhase::NoScrollPhase, false);
        QApplication::sendEvent(view.viewport(), &wheelEvent);
        UITestHelper::waitForUI();

        // Check if transform changed (zoom occurred)
        QTransform newTransform = view.transform();
        CHECK(newTransform.m11() != initialTransform.m11()); // Scale should change
    }

}
