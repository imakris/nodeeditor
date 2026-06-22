#include "ApplicationSetup.hpp"
#include "TestGraphModel.hpp"

#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/Definitions>

#include <catch2/catch.hpp>

#include <QUndoStack>

using QtNodes::BasicGraphicsScene;
using QtNodes::ConnectionId;
using QtNodes::InvalidNodeId;
using QtNodes::NodeId;
using QtNodes::NodeRole;

TEST_CASE("UndoStack integration with BasicGraphicsScene", "[undo]")
{
    auto app = applicationSetup();
    TestGraphModel model;
    BasicGraphicsScene scene(model);

    SECTION("Scene has undo stack")
    {
        auto &undoStack = scene.undoStack();
        CHECK(undoStack.count() == 0);
        CHECK_FALSE(undoStack.canUndo());
        CHECK_FALSE(undoStack.canRedo());
    }

}
