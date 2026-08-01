#include "ApplicationSetup.hpp"
#include "TestGraphModel.hpp"

#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/Definitions>
#include <QtNodes/NodeData>
#include <QtNodes/NodeDelegateModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/UndoCommands>

#include <catch2/catch.hpp>

#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryFile>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>
#include <memory>

using QtNodes::BasicGraphicsScene;
using QtNodes::ConnectionId;
using QtNodes::CreateCommand;
using QtNodes::DataFlowGraphicsScene;
using QtNodes::DataFlowGraphModel;
using QtNodes::InvalidNodeId;
using QtNodes::NodeData;
using QtNodes::NodeDataType;
using QtNodes::NodeDelegateModel;
using QtNodes::NodeDelegateModelRegistry;
using QtNodes::NodeId;
using QtNodes::NodeRole;
using QtNodes::PortIndex;
using QtNodes::PortType;

namespace {

class UndoDocumentNodeModel : public NodeDelegateModel
{
public:
    QString name() const override { return QStringLiteral("UndoDocumentNodeModel"); }
    QString caption() const override { return QStringLiteral("Undo document node"); }

    unsigned int nPorts(PortType) const override { return 1U; }

    NodeDataType dataType(PortType, PortIndex) const override
    {
        return {QStringLiteral("undo-document-data"), QStringLiteral("Undo document data")};
    }

    std::shared_ptr<NodeData> outData(PortIndex) override { return nullptr; }
    void setInData(std::shared_ptr<NodeData>, PortIndex) override {}
    QWidget *embeddedWidget() override { return nullptr; }
};

std::shared_ptr<NodeDelegateModelRegistry> create_undo_document_registry()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();
    registry->registerModel<UndoDocumentNodeModel>();
    return registry;
}

QJsonObject make_scene_document(std::shared_ptr<NodeDelegateModelRegistry> const &registry,
                                bool reuse_old_node_id)
{
    DataFlowGraphModel sourceModel(registry);

    if (!reuse_old_node_id) {
        NodeId const discardedId = sourceModel.addNode(QStringLiteral("UndoDocumentNodeModel"));
        REQUIRE(discardedId != InvalidNodeId);
        REQUIRE(sourceModel.deleteNode(discardedId));
    }

    NodeId const firstId = sourceModel.addNode(QStringLiteral("UndoDocumentNodeModel"));
    NodeId const secondId = sourceModel.addNode(QStringLiteral("UndoDocumentNodeModel"));
    REQUIRE(firstId != InvalidNodeId);
    REQUIRE(secondId != InvalidNodeId);

    sourceModel.setNodeData(firstId, NodeRole::Position, QPointF(20.0, 30.0));
    sourceModel.setNodeData(secondId, NodeRole::Position, QPointF(220.0, 130.0));

    ConnectionId const connection{firstId, 0, secondId, 0};
    sourceModel.addConnection(connection);
    REQUIRE(sourceModel.connectionExists(connection));

    QJsonObject groupJson;
    groupJson["id"] = 17;
    groupJson["name"] = QStringLiteral("Loaded group");
    groupJson["locked"] = false;

    QJsonArray groupNodeIds;
    groupNodeIds.append(static_cast<qint64>(firstId));
    groupNodeIds.append(static_cast<qint64>(secondId));
    groupJson["nodes"] = groupNodeIds;

    QJsonArray groupsJson;
    groupsJson.append(groupJson);

    QJsonObject sceneJson = sourceModel.save();
    sceneJson["groups"] = groupsJson;
    return sceneJson;
}

bool load_scene_document(DataFlowGraphicsScene &scene, QJsonObject const &sceneJson)
{
    QTemporaryFile file(QDir::tempPath() + QStringLiteral("/qt-nodes-undo-XXXXXX.flow"));
    REQUIRE(file.open());
    REQUIRE(file.write(QJsonDocument(sceneJson).toJson()) > 0);
    REQUIRE(file.flush());

    QString const fileName = file.fileName();
    bool selectedFile = false;
    QTimer::singleShot(0, &scene, [&selectedFile, fileName] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (dialog) {
            selectedFile = true;
            dialog->setDirectory(QFileInfo(fileName).absolutePath());
            dialog->selectFile(fileName);
            static_cast<QDialog *>(dialog)->accept();
        }
    });

    bool const loaded = scene.load();
    REQUIRE(selectedFile);
    return loaded;
}

void check_loaded_document(DataFlowGraphModel const &model,
                           DataFlowGraphicsScene const &scene,
                           bool reused_old_node_id)
{
    NodeId const firstId = reused_old_node_id ? 0U : 1U;
    NodeId const secondId = firstId + 1U;
    ConnectionId const connection{firstId, 0, secondId, 0};

    CHECK(model.allNodeIds().size() == 2);
    CHECK(model.nodeExists(firstId));
    CHECK(model.nodeExists(secondId));
    CHECK(model.connectionExists(connection));

    REQUIRE(scene.groups().size() == 1);
    auto const &group = scene.groups().begin()->second;
    REQUIRE(group);
    CHECK(group->name() == QStringLiteral("Loaded group"));

    auto const groupNodeIds = group->nodeIDs();
    CHECK(groupNodeIds.size() == 2);
    CHECK(std::find(groupNodeIds.begin(), groupNodeIds.end(), firstId) != groupNodeIds.end());
    CHECK(std::find(groupNodeIds.begin(), groupNodeIds.end(), secondId) != groupNodeIds.end());
}

} // namespace

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

TEST_CASE("Full document load retires the previous document history", "[undo][serialization]")
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    auto app = applicationSetup();
    auto registry = create_undo_document_registry();
    DataFlowGraphModel model(registry);
    DataFlowGraphicsScene scene(model);
    auto &undoStack = scene.undoStack();

    undoStack.push(
        new CreateCommand(&scene, QStringLiteral("UndoDocumentNodeModel"), QPointF(5.0, 10.0)));
    REQUIRE(model.nodeExists(0U));
    REQUIRE(undoStack.count() == 1);
    REQUIRE_FALSE(undoStack.isClean());

    SECTION("Loaded document does not reuse the old node id")
    {
        QJsonObject const document = make_scene_document(registry, false);
        int modifiedCount = 0;
        int cleanChangedCount = 0;
        bool cleanChangedValue = false;
        bool historyCommittedAtSceneLoaded = false;
        QObject::connect(&scene,
                         &BasicGraphicsScene::modified,
                         [&modifiedCount](BasicGraphicsScene *) { ++modifiedCount; });
        QObject::connect(&undoStack, &QUndoStack::cleanChanged, [&](bool clean) {
            ++cleanChangedCount;
            cleanChangedValue = clean;
        });
        QObject::connect(&scene, &DataFlowGraphicsScene::sceneLoaded, [&] {
            historyCommittedAtSceneLoaded = undoStack.count() == 0 && undoStack.isClean();
        });

        REQUIRE(load_scene_document(scene, document));
        CHECK(modifiedCount > 0);
        CHECK(cleanChangedCount == 1);
        CHECK(cleanChangedValue);
        CHECK(historyCommittedAtSceneLoaded);
        CHECK(undoStack.count() == 0);
        CHECK(undoStack.isClean());
        check_loaded_document(model, scene, false);

        QJsonObject const loadedGraph = model.save();
        CHECK_NOTHROW(undoStack.undo());
        CHECK(model.save() == loadedGraph);
        check_loaded_document(model, scene, false);
    }

    SECTION("Loaded document reuses the old node id")
    {
        QJsonObject const document = make_scene_document(registry, true);

        REQUIRE(load_scene_document(scene, document));
        CHECK(undoStack.count() == 0);
        CHECK(undoStack.isClean());
        check_loaded_document(model, scene, true);

        QJsonObject const loadedGraph = model.save();
        CHECK_NOTHROW(undoStack.undo());
        CHECK(model.save() == loadedGraph);
        check_loaded_document(model, scene, true);
    }
}

TEST_CASE("Successful document load commits an empty undo stack", "[undo][serialization]")
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    auto app = applicationSetup();
    auto registry = create_undo_document_registry();
    DataFlowGraphModel model(registry);
    DataFlowGraphicsScene scene(model);
    auto &undoStack = scene.undoStack();
    QJsonObject const document = make_scene_document(registry, false);

    SECTION("An empty dirty stack becomes clean before sceneLoaded")
    {
        undoStack.resetClean();
        REQUIRE(undoStack.count() == 0);
        REQUIRE(undoStack.index() == 0);
        REQUIRE_FALSE(undoStack.isClean());

        int cleanChangedCount = 0;
        bool cleanChangedValue = false;
        bool sceneLoadedSawCommittedHistory = false;
        bool sceneLoadedFollowedCleanChanged = false;
        QObject::connect(&undoStack, &QUndoStack::cleanChanged, [&](bool clean) {
            ++cleanChangedCount;
            cleanChangedValue = clean;
        });
        QObject::connect(&scene, &DataFlowGraphicsScene::sceneLoaded, [&] {
            sceneLoadedSawCommittedHistory = undoStack.count() == 0 && undoStack.index() == 0
                                             && undoStack.isClean();
            sceneLoadedFollowedCleanChanged = cleanChangedCount == 1 && cleanChangedValue;
        });

        REQUIRE(load_scene_document(scene, document));
        CHECK(undoStack.count() == 0);
        CHECK(undoStack.index() == 0);
        CHECK(undoStack.isClean());
        CHECK(cleanChangedCount == 1);
        CHECK(cleanChangedValue);
        CHECK(sceneLoadedSawCommittedHistory);
        CHECK(sceneLoadedFollowedCleanChanged);
        check_loaded_document(model, scene, false);
    }

    SECTION("An already-clean empty stack does not emit a spurious clean change")
    {
        REQUIRE(undoStack.count() == 0);
        REQUIRE(undoStack.index() == 0);
        REQUIRE(undoStack.isClean());

        int cleanChangedCount = 0;
        bool sceneLoadedSawCommittedHistory = false;
        QObject::connect(&undoStack, &QUndoStack::cleanChanged, [&cleanChangedCount](bool) {
            ++cleanChangedCount;
        });
        QObject::connect(&scene, &DataFlowGraphicsScene::sceneLoaded, [&] {
            sceneLoadedSawCommittedHistory = undoStack.count() == 0 && undoStack.index() == 0
                                             && undoStack.isClean();
        });

        REQUIRE(load_scene_document(scene, document));
        CHECK(undoStack.count() == 0);
        CHECK(undoStack.index() == 0);
        CHECK(undoStack.isClean());
        CHECK(cleanChangedCount == 0);
        CHECK(sceneLoadedSawCommittedHistory);
        check_loaded_document(model, scene, false);
    }
}

TEST_CASE("Rejected document load preserves graph and history", "[undo][serialization]")
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    auto app = applicationSetup();
    auto registry = create_undo_document_registry();
    DataFlowGraphModel model(registry);
    DataFlowGraphicsScene scene(model);
    auto &undoStack = scene.undoStack();

    undoStack.push(
        new CreateCommand(&scene, QStringLiteral("UndoDocumentNodeModel"), QPointF(5.0, 10.0)));
    REQUIRE(model.nodeExists(0U));
    REQUIRE(undoStack.count() == 1);

    QJsonObject rejectedDocument = make_scene_document(registry, false);
    QJsonArray groups = rejectedDocument["groups"].toArray();
    QJsonObject group = groups.first().toObject();
    QJsonArray groupNodes = group["nodes"].toArray();
    groupNodes.append(999);
    group["nodes"] = groupNodes;
    groups.replace(0, group);
    rejectedDocument["groups"] = groups;

    int modifiedCount = 0;
    int sceneLoadedCount = 0;
    QObject::connect(&scene, &BasicGraphicsScene::modified, [&modifiedCount](BasicGraphicsScene *) {
        ++modifiedCount;
    });
    QObject::connect(&scene, &DataFlowGraphicsScene::sceneLoaded, [&sceneLoadedCount] {
        ++sceneLoadedCount;
    });

    CHECK_FALSE(load_scene_document(scene, rejectedDocument));
    CHECK(modifiedCount == 0);
    CHECK(sceneLoadedCount == 0);
    CHECK(model.nodeExists(0U));
    CHECK(model.allNodeIds().size() == 1);
    CHECK(undoStack.count() == 1);
    CHECK(undoStack.canUndo());
    CHECK_FALSE(undoStack.isClean());

    undoStack.undo();
    CHECK(model.allNodeIds().empty());
    CHECK_FALSE(undoStack.canUndo());
    CHECK(undoStack.isClean());
}

TEST_CASE("Orientation rebuild preserves document history", "[undo][graphics]")
{
    auto app = applicationSetup();
    auto registry = create_undo_document_registry();
    DataFlowGraphModel model(registry);
    DataFlowGraphicsScene scene(model);
    auto &undoStack = scene.undoStack();

    undoStack.push(
        new CreateCommand(&scene, QStringLiteral("UndoDocumentNodeModel"), QPointF(5.0, 10.0)));
    REQUIRE(model.nodeExists(0U));
    REQUIRE(undoStack.count() == 1);

    scene.setOrientation(Qt::Vertical);

    CHECK(undoStack.count() == 1);
    CHECK(undoStack.canUndo());
    CHECK_FALSE(undoStack.isClean());
    undoStack.undo();
    CHECK(model.allNodeIds().empty());
    CHECK(undoStack.isClean());
}
