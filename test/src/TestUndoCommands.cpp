#include "ApplicationSetup.hpp"
#include "TestGraphModel.hpp"
#include "UITestHelper.hpp"

#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/DataFlowGraphModel>
#include <QtNodes/DataFlowGraphicsScene>
#include <QtNodes/Definitions>
#include <QtNodes/GraphicsView>
#include <QtNodes/NodeData>
#include <QtNodes/NodeDelegateModel>
#include <QtNodes/NodeDelegateModelRegistry>
#include <QtNodes/UndoCommands>
#include <QtNodes/internal/ConnectionGraphicsObject.hpp>
#include <QtNodes/internal/NodeGraphicsObject.hpp>
#include <QtNodes/internal/locateNode.hpp>

#include <catch2/catch.hpp>

#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <QUndoStack>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

Q_DECLARE_METATYPE(QtNodes::ConnectionId)

using QtNodes::BasicGraphicsScene;
using QtNodes::ConnectionGraphicsObject;
using QtNodes::ConnectionId;
using QtNodes::ConnectionPolicy;
using QtNodes::ConnectionReplacementTransaction;
using QtNodes::CreateCommand;
using QtNodes::DataFlowGraphicsScene;
using QtNodes::DataFlowGraphModel;
using QtNodes::GraphicsView;
using QtNodes::InvalidNodeId;
using QtNodes::NodeData;
using QtNodes::NodeDataType;
using QtNodes::NodeDelegateModel;
using QtNodes::NodeDelegateModelRegistry;
using QtNodes::NodeId;
using QtNodes::NodeRole;
using QtNodes::PortIndex;
using QtNodes::PortType;
using QtNodes::ReplaceConnectionCommand;

namespace {

using PrepareReplacementSignature = std::unique_ptr<ConnectionReplacementTransaction> (
    QtNodes::AbstractGraphModel::*)(std::vector<ConnectionId> const &,
                                    std::vector<ConnectionId> const &) noexcept;

static_assert(std::is_same_v<decltype(&QtNodes::AbstractGraphModel::prepareConnectionReplacement),
                             PrepareReplacementSignature>);
static_assert(noexcept(std::declval<ConnectionReplacementTransaction &>().undo()));
static_assert(noexcept(std::declval<ConnectionReplacementTransaction &>().redo()));
static_assert(!noexcept(std::declval<ConnectionReplacementTransaction &>().publishUndo()));
static_assert(!noexcept(std::declval<ConnectionReplacementTransaction &>().publishRedo()));

using ThrowingMovePublisher = TestGraphModelDetail::ThrowingMoveConnectionReplacementPublisher;
using ThrowingMoveTransaction
    = QtNodes::ConnectionIdIndexReplacementTransaction<ThrowingMovePublisher>;
static_assert(!std::is_nothrow_constructible_v<ThrowingMoveTransaction,
                                               QtNodes::ConnectionIdIndex &,
                                               QtNodes::ConnectionIdIndex,
                                               std::vector<ConnectionId>,
                                               std::vector<ConnectionId>,
                                               ThrowingMovePublisher>);

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

class ReplacementData : public NodeData
{
public:
    explicit ReplacementData(int value)
        : m_value(value)
    {}

    NodeDataType type() const override
    {
        return {QStringLiteral("replacement-data"), QStringLiteral("Replacement data")};
    }

    int value() const { return m_value; }

private:
    int m_value;
};

class ReplacementNodeModel : public NodeDelegateModel
{
public:
    QString name() const override { return QStringLiteral("ReplacementNodeModel"); }
    QString caption() const override { return QStringLiteral("Replacement node"); }

    unsigned int nPorts(PortType) const override { return 1U; }

    NodeDataType dataType(PortType, PortIndex) const override
    {
        return {m_data_type_id, QStringLiteral("Replacement data")};
    }

    ConnectionPolicy portConnectionPolicy(PortType port_type, PortIndex) const override
    {
        return port_type == PortType::Out ? m_output_policy : ConnectionPolicy::One;
    }

    std::shared_ptr<NodeData> outData(PortIndex) override
    {
        return std::make_shared<ReplacementData>(m_output_value);
    }

    void setInData(std::shared_ptr<NodeData> data, PortIndex) override
    {
        ++m_input_data_set_count;
        if (m_throw_on_input) {
            throw std::runtime_error("replacement input rejected with an exception");
        }

        auto replacement_data = std::dynamic_pointer_cast<ReplacementData>(data);
        m_last_input_value = replacement_data ? replacement_data->value() : -1;
    }

    QWidget *embeddedWidget() override { return nullptr; }

    void inputConnectionCreated(ConnectionId const &) override { ++m_input_created_count; }
    void inputConnectionDeleted(ConnectionId const &) override { ++m_input_deleted_count; }
    void outputConnectionCreated(ConnectionId const &) override { ++m_output_created_count; }
    void outputConnectionDeleted(ConnectionId const &) override { ++m_output_deleted_count; }

    void reset_callback_counts()
    {
        m_input_created_count = 0;
        m_input_deleted_count = 0;
        m_output_created_count = 0;
        m_output_deleted_count = 0;
        m_input_data_set_count = 0;
        m_last_input_value = -1;
    }

    int input_created_count() const { return m_input_created_count; }
    int input_deleted_count() const { return m_input_deleted_count; }
    int output_created_count() const { return m_output_created_count; }
    int output_deleted_count() const { return m_output_deleted_count; }
    int input_data_set_count() const { return m_input_data_set_count; }
    int last_input_value() const { return m_last_input_value; }

    void set_output_policy(ConnectionPolicy policy) { m_output_policy = policy; }
    void set_data_type_id(QString data_type_id) { m_data_type_id = std::move(data_type_id); }
    void set_throw_on_input(bool enabled) { m_throw_on_input = enabled; }
    void publish_output_value(int value)
    {
        m_output_value = value;
        Q_EMIT dataUpdated(0U);
    }

private:
    ConnectionPolicy m_output_policy = ConnectionPolicy::One;
    QString m_data_type_id = QStringLiteral("replacement-data");
    int m_input_created_count = 0;
    int m_input_deleted_count = 0;
    int m_output_created_count = 0;
    int m_output_deleted_count = 0;
    int m_input_data_set_count = 0;
    int m_output_value = 0;
    int m_last_input_value = -1;
    bool m_throw_on_input = false;
};

class ManyConnectionNodeModel : public ReplacementNodeModel
{
public:
    QString name() const override { return QStringLiteral("ManyConnectionNodeModel"); }

    ConnectionPolicy portConnectionPolicy(PortType portType, PortIndex) const override
    {
        return portType == PortType::Out ? ConnectionPolicy::Many : ConnectionPolicy::One;
    }
};

class IncompatibleReplacementNodeModel : public ReplacementNodeModel
{
public:
    QString name() const override { return QStringLiteral("IncompatibleReplacementNodeModel"); }

    NodeDataType dataType(PortType, PortIndex) const override
    {
        return {QStringLiteral("incompatible-data"), QStringLiteral("Incompatible data")};
    }
};

class ReplacementGraphModel : public DataFlowGraphModel
{
public:
    using DataFlowGraphModel::DataFlowGraphModel;

    bool detachPossible(ConnectionId const) const override { return m_detach_possible; }

    void set_detach_possible(bool possible) { m_detach_possible = possible; }

    bool deleteConnection(ConnectionId const connection_id) override
    {
        if (m_delete_connection_fails) {
            return false;
        }
        return DataFlowGraphModel::deleteConnection(connection_id);
    }

    std::unique_ptr<ConnectionReplacementTransaction> prepareConnectionReplacement(
        std::vector<ConnectionId> const &removed_connection_ids,
        std::vector<ConnectionId> const &added_connection_ids) noexcept override
    {
        if (m_prepare_connection_replacement_fails) {
            return {};
        }
        return DataFlowGraphModel::prepareConnectionReplacement(removed_connection_ids,
                                                                added_connection_ids);
    }

    void set_delete_connection_fails(bool fails) { m_delete_connection_fails = fails; }
    void set_prepare_connection_replacement_fails(bool fails)
    {
        m_prepare_connection_replacement_fails = fails;
    }

private:
    bool m_detach_possible = true;
    bool m_delete_connection_fails = false;
    bool m_prepare_connection_replacement_fails = false;
};

std::shared_ptr<NodeDelegateModelRegistry> create_undo_document_registry()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();
    registry->registerModel<UndoDocumentNodeModel>();
    return registry;
}

std::shared_ptr<NodeDelegateModelRegistry> create_replacement_registry()
{
    auto registry = std::make_shared<NodeDelegateModelRegistry>();
    registry->registerModel<ReplacementNodeModel>();
    registry->registerModel<ManyConnectionNodeModel>();
    registry->registerModel<IncompatibleReplacementNodeModel>();
    return registry;
}

QPointF port_scene_position(BasicGraphicsScene &scene, NodeId node_id, PortType port_type)
{
    auto *node = scene.nodeGraphicsObject(node_id);
    REQUIRE(node != nullptr);
    QPointF position = scene.nodeGeometry().portScenePosition(node_id,
                                                              port_type,
                                                              0U,
                                                              node->sceneTransform());
    // Stay just inside the node shape so the viewport hit is unambiguously
    // delivered to the node while remaining inside the port's hit radius.
    position.rx() += (port_type == PortType::In) ? 4.0 : -4.0;
    return position;
}

ConnectionGraphicsObject *draft_connection(BasicGraphicsScene &scene)
{
    for (QGraphicsItem *item : scene.items()) {
        auto *connection = qgraphicsitem_cast<ConnectionGraphicsObject *>(item);
        if (connection && connection->connectionState().requiresPort()) {
            return connection;
        }
    }

    return nullptr;
}

void prepare_view(GraphicsView &view)
{
    view.resize(1000, 500);
    view.show();
    REQUIRE(QTest::qWaitForWindowExposed(&view));
    UITestHelper::waitForUI();
}

void viewport_press_port(GraphicsView &view,
                         BasicGraphicsScene &scene,
                         NodeId node_id,
                         PortType port_type)
{
    QPoint const viewport_position = view.mapFromScene(
        port_scene_position(scene, node_id, port_type));
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, viewport_position);
    UITestHelper::waitForUI();
}

void viewport_release_at(GraphicsView &view, QPointF scene_position)
{
    QPoint const viewport_position = view.mapFromScene(scene_position);
    QTest::mouseMove(view.viewport(), viewport_position);
    UITestHelper::waitForUI();
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, viewport_position);
    UITestHelper::waitForUI();
}

void viewport_drag_connection(GraphicsView &view,
                              BasicGraphicsScene &scene,
                              NodeId source_node_id,
                              PortType source_port_type,
                              NodeId target_node_id,
                              PortType target_port_type)
{
    viewport_press_port(view, scene, source_node_id, source_port_type);
    auto *draft = draft_connection(scene);
    REQUIRE(draft != nullptr);
    REQUIRE(scene.mouseGrabberItem() == draft);
    QPointF const target_position = port_scene_position(scene, target_node_id, target_port_type);
    REQUIRE(view.viewport()->rect().contains(view.mapFromScene(target_position)));
    REQUIRE(QtNodes::locateNodeAt(target_position, scene, view.transform())
            == scene.nodeGraphicsObject(target_node_id));
    viewport_release_at(view, target_position);
}

template<typename DelegateModel>
NodeId add_replacement_node(ReplacementGraphModel &model)
{
    NodeId const node_id = model.addNode(DelegateModel{}.name());
    REQUIRE(node_id != InvalidNodeId);
    QPointF const position = node_id == 0U ? QPointF(-400.0, -40.0)
                                           : QPointF(100.0, -220.0 + (node_id - 1U) * 145.0);
    REQUIRE(model.setNodeData(node_id, NodeRole::Position, position));
    return node_id;
}

ReplacementNodeModel *replacement_delegate(ReplacementGraphModel &model, NodeId node_id)
{
    auto *delegate = model.delegateModel<ReplacementNodeModel>(node_id);
    REQUIRE(delegate != nullptr);
    return delegate;
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

TEST_CASE("Cancelling a One-output connection draft preserves topology and history",
          "[undo][connections]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();
    ReplacementGraphModel model(registry);

    NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
    ConnectionId const old_connection{source, 0U, old_target, 0U};
    model.addConnection(old_connection);
    REQUIRE(model.connectionExists(old_connection));

    BasicGraphicsScene scene(model);
    GraphicsView view(&scene);
    prepare_view(view);
    auto &undo_stack = scene.undoStack();
    QSignalSpy created_spy(&model, &DataFlowGraphModel::connectionCreated);
    QSignalSpy deleted_spy(&model, &DataFlowGraphModel::connectionDeleted);
    auto *source_delegate = replacement_delegate(model, source);
    auto *old_target_delegate = replacement_delegate(model, old_target);
    source_delegate->reset_callback_counts();
    old_target_delegate->reset_callback_counts();

    viewport_press_port(view, scene, source, PortType::Out);

    CHECK(draft_connection(scene) != nullptr);
    CHECK(model.connectionExists(old_connection));
    CHECK(scene.connectionGraphicsObject(old_connection) != nullptr);
    CHECK(undo_stack.count() == 0);

    viewport_release_at(view, QPointF(0.0, 200.0));

    CHECK(model.connectionExists(old_connection));
    CHECK(scene.connectionGraphicsObject(old_connection) != nullptr);
    CHECK(draft_connection(scene) == nullptr);
    CHECK(undo_stack.count() == 0);
    CHECK(created_spy.count() == 0);
    CHECK(deleted_spy.count() == 0);
    CHECK(source_delegate->output_created_count() == 0);
    CHECK(source_delegate->output_deleted_count() == 0);
    CHECK(old_target_delegate->input_created_count() == 0);
    CHECK(old_target_delegate->input_deleted_count() == 0);
}

TEST_CASE("Replacing a One-output connection is one exact undo transaction", "[undo][connections]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();
    ReplacementGraphModel model(registry);

    NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const new_target = add_replacement_node<ReplacementNodeModel>(model);
    ConnectionId const old_connection{source, 0U, old_target, 0U};
    ConnectionId const new_connection{source, 0U, new_target, 0U};
    model.addConnection(old_connection);
    REQUIRE(model.connectionExists(old_connection));

    BasicGraphicsScene scene(model);
    GraphicsView view(&scene);
    prepare_view(view);
    auto &undo_stack = scene.undoStack();
    auto *source_delegate = replacement_delegate(model, source);
    auto *old_target_delegate = replacement_delegate(model, old_target);
    auto *new_target_delegate = replacement_delegate(model, new_target);
    QSignalSpy created_spy(&model, &DataFlowGraphModel::connectionCreated);
    QSignalSpy deleted_spy(&model, &DataFlowGraphModel::connectionDeleted);
    source_delegate->reset_callback_counts();
    old_target_delegate->reset_callback_counts();
    new_target_delegate->reset_callback_counts();

    viewport_drag_connection(view, scene, source, PortType::Out, new_target, PortType::In);

    CHECK_FALSE(model.connectionExists(old_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(old_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
    CHECK(model.connections(source, PortType::Out, 0U).size() == 1);
    CHECK(undo_stack.count() == 1);
    CHECK(undo_stack.index() == 1);
    CHECK(source_delegate->output_deleted_count() == 1);
    CHECK(source_delegate->output_created_count() == 1);
    CHECK(old_target_delegate->input_deleted_count() == 1);
    CHECK(old_target_delegate->input_data_set_count() == 1);
    CHECK(new_target_delegate->input_created_count() == 1);
    CHECK(new_target_delegate->input_data_set_count() == 1);
    CHECK(created_spy.count() == 1);
    CHECK(deleted_spy.count() == 1);

    undo_stack.undo();

    CHECK(model.connectionExists(old_connection));
    CHECK_FALSE(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(old_connection) != nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) == nullptr);
    CHECK(model.connections(source, PortType::Out, 0U).size() == 1);
    CHECK(undo_stack.index() == 0);
    CHECK(source_delegate->output_deleted_count() == 2);
    CHECK(source_delegate->output_created_count() == 2);
    CHECK(old_target_delegate->input_created_count() == 1);
    CHECK(old_target_delegate->input_data_set_count() == 2);
    CHECK(new_target_delegate->input_deleted_count() == 1);
    CHECK(new_target_delegate->input_data_set_count() == 2);
    CHECK(created_spy.count() == 2);
    CHECK(deleted_spy.count() == 2);

    undo_stack.redo();

    CHECK_FALSE(model.connectionExists(old_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(old_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
    CHECK(model.connections(source, PortType::Out, 0U).size() == 1);
    CHECK(undo_stack.index() == 1);
    CHECK(source_delegate->output_deleted_count() == 3);
    CHECK(source_delegate->output_created_count() == 3);
    CHECK(old_target_delegate->input_deleted_count() == 2);
    CHECK(old_target_delegate->input_data_set_count() == 3);
    CHECK(new_target_delegate->input_created_count() == 2);
    CHECK(new_target_delegate->input_data_set_count() == 3);
    CHECK(created_spy.count() == 3);
    CHECK(deleted_spy.count() == 3);
}

TEST_CASE("Replacement replay propagates the source's current data on every recreation",
          "[undo][connections][data]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();
    ReplacementGraphModel model(registry);

    NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const new_target = add_replacement_node<ReplacementNodeModel>(model);
    ConnectionId const old_connection{source, 0U, old_target, 0U};
    ConnectionId const new_connection{source, 0U, new_target, 0U};
    auto *source_delegate = replacement_delegate(model, source);
    auto *old_target_delegate = replacement_delegate(model, old_target);
    auto *new_target_delegate = replacement_delegate(model, new_target);

    source_delegate->publish_output_value(1);
    model.addConnection(old_connection);
    REQUIRE(old_target_delegate->last_input_value() == 1);

    BasicGraphicsScene scene(model);
    scene.undoStack().push(new ReplaceConnectionCommand(&scene, new_connection, {old_connection}));
    REQUIRE(new_target_delegate->last_input_value() == 1);

    source_delegate->publish_output_value(2);
    REQUIRE(new_target_delegate->last_input_value() == 2);
    scene.undoStack().undo();

    CHECK(model.connectionExists(old_connection));
    CHECK_FALSE(model.connectionExists(new_connection));
    CHECK(scene.undoStack().index() == 0);
    CHECK(old_target_delegate->last_input_value() == 2);

    source_delegate->publish_output_value(3);
    REQUIRE(old_target_delegate->last_input_value() == 3);
    scene.undoStack().redo();

    CHECK_FALSE(model.connectionExists(old_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(scene.undoStack().index() == 1);
    CHECK(new_target_delegate->last_input_value() == 3);

    source_delegate->publish_output_value(4);
    REQUIRE(new_target_delegate->last_input_value() == 4);
    scene.undoStack().undo();

    CHECK(model.connectionExists(old_connection));
    CHECK_FALSE(model.connectionExists(new_connection));
    CHECK(scene.undoStack().index() == 0);
    CHECK(old_target_delegate->last_input_value() == 4);
}

TEST_CASE("Replacement replay preserves ordinary frozen-target data semantics",
          "[undo][connections][data]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();

    SECTION("Ordinary add and update retain the edge while a frozen target refuses data")
    {
        ReplacementGraphModel model(registry);
        NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const target = add_replacement_node<ReplacementNodeModel>(model);
        ConnectionId const connection{source, 0U, target, 0U};
        auto *source_delegate = replacement_delegate(model, source);
        auto *target_delegate = replacement_delegate(model, target);
        BasicGraphicsScene scene(model);
        QSignalSpy data_spy(&model, &DataFlowGraphModel::inPortDataWasSet);

        target_delegate->setFrozenState(true);
        target_delegate->reset_callback_counts();
        source_delegate->publish_output_value(5);
        model.addConnection(connection);

        CHECK(model.connectionExists(connection));
        CHECK(scene.connectionGraphicsObject(connection) != nullptr);
        CHECK(target_delegate->input_created_count() == 1);
        CHECK(target_delegate->input_data_set_count() == 0);
        CHECK(data_spy.count() == 0);

        source_delegate->publish_output_value(6);
        CHECK(target_delegate->input_data_set_count() == 0);
        CHECK(data_spy.count() == 0);

        target_delegate->setFrozenState(false);
        source_delegate->publish_output_value(7);
        CHECK(target_delegate->input_data_set_count() == 1);
        CHECK(target_delegate->last_input_value() == 7);
        CHECK(data_spy.count() == 1);
    }

    SECTION("Replacement recreation has the same frozen-target refusal semantics")
    {
        ReplacementGraphModel model(registry);
        NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const new_target = add_replacement_node<ReplacementNodeModel>(model);
        ConnectionId const old_connection{source, 0U, old_target, 0U};
        ConnectionId const new_connection{source, 0U, new_target, 0U};
        auto *source_delegate = replacement_delegate(model, source);
        auto *old_target_delegate = replacement_delegate(model, old_target);

        source_delegate->publish_output_value(5);
        model.addConnection(old_connection);
        BasicGraphicsScene scene(model);
        scene.undoStack().push(
            new ReplaceConnectionCommand(&scene, new_connection, {old_connection}));
        REQUIRE(model.connectionExists(new_connection));

        old_target_delegate->setFrozenState(true);
        old_target_delegate->reset_callback_counts();
        source_delegate->publish_output_value(6);
        QSignalSpy data_spy(&model, &DataFlowGraphModel::inPortDataWasSet);
        scene.undoStack().undo();

        CHECK(model.connectionExists(old_connection));
        CHECK_FALSE(model.connectionExists(new_connection));
        CHECK(scene.connectionGraphicsObject(old_connection) != nullptr);
        CHECK(scene.connectionGraphicsObject(new_connection) == nullptr);
        CHECK(scene.undoStack().index() == 0);
        CHECK(old_target_delegate->input_created_count() == 1);
        CHECK(old_target_delegate->input_data_set_count() == 0);
        REQUIRE(data_spy.count() == 1);
        CHECK(data_spy.at(0).at(0).value<NodeId>() == new_target);

        old_target_delegate->setFrozenState(false);
        source_delegate->publish_output_value(7);
        CHECK(old_target_delegate->input_data_set_count() == 1);
        CHECK(old_target_delegate->last_input_value() == 7);
        REQUIRE(data_spy.count() == 2);
        CHECK(data_spy.at(1).at(0).value<NodeId>() == old_target);
    }
}

TEST_CASE("Throwing replacement data delivery cannot terminate or desynchronize history",
          "[undo][connections][data]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();
    ReplacementGraphModel model(registry);

    NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const new_target = add_replacement_node<ReplacementNodeModel>(model);
    ConnectionId const old_connection{source, 0U, old_target, 0U};
    ConnectionId const new_connection{source, 0U, new_target, 0U};
    auto *source_delegate = replacement_delegate(model, source);
    auto *old_target_delegate = replacement_delegate(model, old_target);
    auto *new_target_delegate = replacement_delegate(model, new_target);

    source_delegate->publish_output_value(11);
    model.addConnection(old_connection);
    BasicGraphicsScene scene(model);
    scene.undoStack().push(new ReplaceConnectionCommand(&scene, new_connection, {old_connection}));
    REQUIRE(scene.undoStack().index() == 1);

    source_delegate->publish_output_value(12);
    old_target_delegate->reset_callback_counts();
    old_target_delegate->set_throw_on_input(true);
    CHECK_NOTHROW(scene.undoStack().undo());

    CHECK(model.connectionExists(old_connection));
    CHECK_FALSE(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(old_connection) != nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) == nullptr);
    CHECK(scene.undoStack().count() == 1);
    CHECK(scene.undoStack().index() == 0);
    CHECK(old_target_delegate->input_created_count() == 1);
    CHECK(old_target_delegate->input_data_set_count() == 1);

    old_target_delegate->set_throw_on_input(false);
    CHECK_NOTHROW(scene.undoStack().redo());
    CHECK_FALSE(model.connectionExists(old_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(old_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
    CHECK(scene.undoStack().index() == 1);
    CHECK(new_target_delegate->last_input_value() == 12);

    source_delegate->publish_output_value(13);
    CHECK_NOTHROW(scene.undoStack().undo());
    CHECK(model.connectionExists(old_connection));
    CHECK_FALSE(model.connectionExists(new_connection));
    CHECK(scene.undoStack().index() == 0);
    CHECK(old_target_delegate->last_input_value() == 13);
}

TEST_CASE("Replacement publication continues after a deletion observer throws",
          "[undo][connections][publication]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();
    ReplacementGraphModel model(registry);

    NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const first_target = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const second_target = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const new_target = add_replacement_node<ReplacementNodeModel>(model);
    ConnectionId const first_connection{source, 0U, first_target, 0U};
    ConnectionId const second_connection{source, 0U, second_target, 0U};
    ConnectionId const new_connection{source, 0U, new_target, 0U};
    auto *source_delegate = replacement_delegate(model, source);
    auto *first_target_delegate = replacement_delegate(model, first_target);
    auto *second_target_delegate = replacement_delegate(model, second_target);
    auto *new_target_delegate = replacement_delegate(model, new_target);

    source_delegate->set_output_policy(ConnectionPolicy::Many);
    model.addConnection(first_connection);
    model.addConnection(second_connection);
    source_delegate->set_output_policy(ConnectionPolicy::One);
    REQUIRE(model.connectionExists(first_connection));
    REQUIRE(model.connectionExists(second_connection));

    BasicGraphicsScene scene(model);
    source_delegate->reset_callback_counts();
    first_target_delegate->reset_callback_counts();
    second_target_delegate->reset_callback_counts();
    new_target_delegate->reset_callback_counts();

    int deleted_signal_count = 0;
    int created_signal_count = 0;
    QObject::connect(&model,
                     &DataFlowGraphModel::connectionDeleted,
                     &model,
                     [&](ConnectionId const &) { ++deleted_signal_count; });
    QObject::connect(&model,
                     &DataFlowGraphModel::connectionCreated,
                     &model,
                     [&](ConnectionId const &) { ++created_signal_count; });
    bool throw_on_first_deletion = true;
    QObject::connect(&model,
                     &DataFlowGraphModel::connectionDeleted,
                     &model,
                     [&](ConnectionId const connection_id) {
                         if (throw_on_first_deletion && connection_id == first_connection) {
                             throw_on_first_deletion = false;
                             throw std::runtime_error("first deletion observer failed");
                         }
                     });

    CHECK_NOTHROW(scene.undoStack().push(
        new ReplaceConnectionCommand(&scene, new_connection, {first_connection, second_connection})));

    CHECK_FALSE(model.connectionExists(first_connection));
    CHECK_FALSE(model.connectionExists(second_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(first_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(second_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
    CHECK(scene.undoStack().count() == 1);
    CHECK(scene.undoStack().index() == 1);
    CHECK(deleted_signal_count == 2);
    CHECK(created_signal_count == 1);
    CHECK(source_delegate->output_deleted_count() == 1);
    CHECK(source_delegate->output_created_count() == 1);
    CHECK(first_target_delegate->input_deleted_count() == 0);
    CHECK(first_target_delegate->input_data_set_count() == 1);
    CHECK(second_target_delegate->input_deleted_count() == 1);
    CHECK(second_target_delegate->input_data_set_count() == 1);
    CHECK(new_target_delegate->input_created_count() == 1);
    CHECK(new_target_delegate->input_data_set_count() == 1);

    CHECK_NOTHROW(scene.undoStack().undo());
    CHECK(model.connectionExists(first_connection));
    CHECK(model.connectionExists(second_connection));
    CHECK_FALSE(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(first_connection) != nullptr);
    CHECK(scene.connectionGraphicsObject(second_connection) != nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) == nullptr);
    CHECK(scene.undoStack().index() == 0);
    CHECK(deleted_signal_count == 3);
    CHECK(created_signal_count == 3);

    CHECK_NOTHROW(scene.undoStack().redo());
    CHECK_FALSE(model.connectionExists(first_connection));
    CHECK_FALSE(model.connectionExists(second_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(first_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(second_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
    CHECK(scene.undoStack().index() == 1);
    CHECK(deleted_signal_count == 5);
    CHECK(created_signal_count == 4);
    CHECK(source_delegate->output_deleted_count() == 4);
    CHECK(source_delegate->output_created_count() == 4);
    CHECK(first_target_delegate->input_deleted_count() == 1);
    CHECK(first_target_delegate->input_created_count() == 1);
    CHECK(second_target_delegate->input_deleted_count() == 2);
    CHECK(second_target_delegate->input_created_count() == 1);
    CHECK(new_target_delegate->input_deleted_count() == 1);
    CHECK(new_target_delegate->input_created_count() == 2);
}

TEST_CASE("TestGraphModel replacement publisher continues after an observer throws",
          "[undo][connections][publication]")
{
    auto app = applicationSetup();
    TestGraphModel model;
    NodeId const source = model.addNode();
    NodeId const first_target = model.addNode();
    NodeId const second_target = model.addNode();
    NodeId const new_target = model.addNode();
    ConnectionId const first_connection{source, 0U, first_target, 0U};
    ConnectionId const second_connection{source, 0U, second_target, 0U};
    ConnectionId const new_connection{source, 0U, new_target, 0U};
    model.addConnection(first_connection);
    model.addConnection(second_connection);

    BasicGraphicsScene scene(model);
    int deleted_signal_count = 0;
    int created_signal_count = 0;
    QObject::connect(&model, &TestGraphModel::connectionDeleted, &model, [&](ConnectionId const &) {
        ++deleted_signal_count;
    });
    QObject::connect(&model, &TestGraphModel::connectionCreated, &model, [&](ConnectionId const &) {
        ++created_signal_count;
    });
    bool throw_on_first_deletion = true;
    QObject::connect(&model,
                     &TestGraphModel::connectionDeleted,
                     &model,
                     [&](ConnectionId const connection_id) {
                         if (throw_on_first_deletion && connection_id == first_connection) {
                             throw_on_first_deletion = false;
                             throw std::runtime_error("test publisher deletion observer failed");
                         }
                     });

    CHECK_NOTHROW(scene.undoStack().push(
        new ReplaceConnectionCommand(&scene, new_connection, {first_connection, second_connection})));
    CHECK_FALSE(model.connectionExists(first_connection));
    CHECK_FALSE(model.connectionExists(second_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(first_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(second_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
    CHECK(scene.undoStack().index() == 1);
    CHECK(deleted_signal_count == 2);
    CHECK(created_signal_count == 1);

    CHECK_NOTHROW(scene.undoStack().undo());
    CHECK(model.connectionExists(first_connection));
    CHECK(model.connectionExists(second_connection));
    CHECK_FALSE(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(first_connection) != nullptr);
    CHECK(scene.connectionGraphicsObject(second_connection) != nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) == nullptr);
    CHECK(scene.undoStack().index() == 0);

    CHECK_NOTHROW(scene.undoStack().redo());
    CHECK_FALSE(model.connectionExists(first_connection));
    CHECK_FALSE(model.connectionExists(second_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(first_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(second_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
    CHECK(scene.undoStack().index() == 1);
}

TEST_CASE("Throwing publisher construction is contained before replacement enters history",
          "[undo][connections][publication]")
{
    auto app = applicationSetup();
    TestGraphModel model;
    NodeId const source = model.addNode();
    NodeId const old_target = model.addNode();
    NodeId const new_target = model.addNode();
    ConnectionId const old_connection{source, 0U, old_target, 0U};
    ConnectionId const new_connection{source, 0U, new_target, 0U};
    model.addConnection(old_connection);
    REQUIRE(model.connectionExists(old_connection));

    BasicGraphicsScene scene(model);
    QSignalSpy created_spy(&model, &TestGraphModel::connectionCreated);
    QSignalSpy deleted_spy(&model, &TestGraphModel::connectionDeleted);
    model.setThrowOnReplacementPublisherMove(true);

    CHECK_NOTHROW(scene.undoStack().push(
        new ReplaceConnectionCommand(&scene, new_connection, {old_connection})));

    CHECK(model.connectionExists(old_connection));
    CHECK_FALSE(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(old_connection) != nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) == nullptr);
    CHECK(scene.undoStack().count() == 0);
    CHECK(scene.undoStack().index() == 0);
    CHECK(created_spy.count() == 0);
    CHECK(deleted_spy.count() == 0);
}

TEST_CASE("Rejected One-output replacement preserves topology callbacks and history",
          "[undo][connections]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();
    ReplacementGraphModel model(registry);

    NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const invalid_target = add_replacement_node<IncompatibleReplacementNodeModel>(model);
    ConnectionId const old_connection{source, 0U, old_target, 0U};
    ConnectionId const invalid_connection{source, 0U, invalid_target, 0U};
    model.addConnection(old_connection);
    REQUIRE(model.connectionExists(old_connection));

    BasicGraphicsScene scene(model);
    GraphicsView view(&scene);
    prepare_view(view);
    auto &undo_stack = scene.undoStack();
    auto *source_delegate = replacement_delegate(model, source);
    auto *old_target_delegate = replacement_delegate(model, old_target);
    auto *invalid_target_delegate = replacement_delegate(model, invalid_target);
    QSignalSpy created_spy(&model, &DataFlowGraphModel::connectionCreated);
    QSignalSpy deleted_spy(&model, &DataFlowGraphModel::connectionDeleted);
    source_delegate->reset_callback_counts();
    old_target_delegate->reset_callback_counts();
    invalid_target_delegate->reset_callback_counts();

    viewport_drag_connection(view, scene, source, PortType::Out, invalid_target, PortType::In);

    CHECK(model.connectionExists(old_connection));
    CHECK_FALSE(model.connectionExists(invalid_connection));
    CHECK(scene.connectionGraphicsObject(old_connection) != nullptr);
    CHECK(scene.connectionGraphicsObject(invalid_connection) == nullptr);
    CHECK(draft_connection(scene) == nullptr);
    CHECK(undo_stack.count() == 0);
    CHECK(source_delegate->output_deleted_count() == 0);
    CHECK(source_delegate->output_created_count() == 0);
    CHECK(old_target_delegate->input_deleted_count() == 0);
    CHECK(old_target_delegate->input_data_set_count() == 0);
    CHECK(invalid_target_delegate->input_created_count() == 0);
    CHECK(invalid_target_delegate->input_data_set_count() == 0);
    CHECK(created_spy.count() == 0);
    CHECK(deleted_spy.count() == 0);
}

TEST_CASE("Dropping a One-output draft on its existing target is a no-op", "[undo][connections]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();
    ReplacementGraphModel model(registry);

    NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const target = add_replacement_node<ReplacementNodeModel>(model);
    ConnectionId const connection{source, 0U, target, 0U};
    model.addConnection(connection);
    REQUIRE(model.connectionExists(connection));

    BasicGraphicsScene scene(model);
    GraphicsView view(&scene);
    prepare_view(view);
    QSignalSpy created_spy(&model, &DataFlowGraphModel::connectionCreated);
    QSignalSpy deleted_spy(&model, &DataFlowGraphModel::connectionDeleted);
    auto *source_delegate = replacement_delegate(model, source);
    auto *target_delegate = replacement_delegate(model, target);
    source_delegate->reset_callback_counts();
    target_delegate->reset_callback_counts();

    viewport_drag_connection(view, scene, source, PortType::Out, target, PortType::In);

    CHECK(model.connectionExists(connection));
    CHECK(model.connections(source, PortType::Out, 0U).size() == 1U);
    CHECK(scene.connectionGraphicsObject(connection) != nullptr);
    CHECK(scene.undoStack().count() == 0);
    CHECK(created_spy.count() == 0);
    CHECK(deleted_spy.count() == 0);
    CHECK(source_delegate->output_created_count() == 0);
    CHECK(source_delegate->output_deleted_count() == 0);
    CHECK(target_delegate->input_created_count() == 0);
    CHECK(target_delegate->input_deleted_count() == 0);
}

TEST_CASE("Replacing all legacy edges after Many changes to One has exact history",
          "[undo][connections]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();
    ReplacementGraphModel model(registry);

    NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const first_target = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const second_target = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const new_target = add_replacement_node<ReplacementNodeModel>(model);
    ConnectionId const first_connection{source, 0U, first_target, 0U};
    ConnectionId const second_connection{source, 0U, second_target, 0U};
    ConnectionId const new_connection{source, 0U, new_target, 0U};
    auto *source_delegate = replacement_delegate(model, source);
    source_delegate->set_output_policy(ConnectionPolicy::Many);
    model.addConnection(first_connection);
    model.addConnection(second_connection);
    REQUIRE(model.connectionExists(first_connection));
    REQUIRE(model.connectionExists(second_connection));
    source_delegate->set_output_policy(ConnectionPolicy::One);

    BasicGraphicsScene scene(model);
    GraphicsView view(&scene);
    prepare_view(view);
    QSignalSpy created_spy(&model, &DataFlowGraphModel::connectionCreated);
    QSignalSpy deleted_spy(&model, &DataFlowGraphModel::connectionDeleted);
    auto *first_target_delegate = replacement_delegate(model, first_target);
    auto *second_target_delegate = replacement_delegate(model, second_target);
    auto *new_target_delegate = replacement_delegate(model, new_target);
    source_delegate->reset_callback_counts();
    first_target_delegate->reset_callback_counts();
    second_target_delegate->reset_callback_counts();
    new_target_delegate->reset_callback_counts();

    viewport_drag_connection(view, scene, source, PortType::Out, new_target, PortType::In);

    CHECK_FALSE(model.connectionExists(first_connection));
    CHECK_FALSE(model.connectionExists(second_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(model.connections(source, PortType::Out, 0U).size() == 1U);
    CHECK(scene.connectionGraphicsObject(first_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(second_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
    CHECK(scene.undoStack().count() == 1);
    CHECK(created_spy.count() == 1);
    CHECK(deleted_spy.count() == 2);
    CHECK(source_delegate->output_created_count() == 1);
    CHECK(source_delegate->output_deleted_count() == 2);
    CHECK(first_target_delegate->input_deleted_count() == 1);
    CHECK(second_target_delegate->input_deleted_count() == 1);
    CHECK(new_target_delegate->input_created_count() == 1);

    scene.undoStack().undo();

    CHECK(model.connectionExists(first_connection));
    CHECK(model.connectionExists(second_connection));
    CHECK_FALSE(model.connectionExists(new_connection));
    CHECK(model.connections(source, PortType::Out, 0U).size() == 2U);
    CHECK(scene.connectionGraphicsObject(first_connection) != nullptr);
    CHECK(scene.connectionGraphicsObject(second_connection) != nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) == nullptr);
    CHECK(created_spy.count() == 3);
    CHECK(deleted_spy.count() == 3);
    CHECK(source_delegate->output_created_count() == 3);
    CHECK(source_delegate->output_deleted_count() == 3);
    CHECK(first_target_delegate->input_created_count() == 1);
    CHECK(second_target_delegate->input_created_count() == 1);
    CHECK(new_target_delegate->input_deleted_count() == 1);

    scene.undoStack().redo();

    CHECK_FALSE(model.connectionExists(first_connection));
    CHECK_FALSE(model.connectionExists(second_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(model.connections(source, PortType::Out, 0U).size() == 1U);
    CHECK(scene.connectionGraphicsObject(first_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(second_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
    CHECK(created_spy.count() == 4);
    CHECK(deleted_spy.count() == 5);
    CHECK(source_delegate->output_created_count() == 4);
    CHECK(source_delegate->output_deleted_count() == 5);
    CHECK(first_target_delegate->input_deleted_count() == 2);
    CHECK(second_target_delegate->input_deleted_count() == 2);
    CHECK(new_target_delegate->input_created_count() == 2);
}

TEST_CASE("Replacement command validates before mutating the model", "[undo][connections]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();
    ReplacementGraphModel model(registry);

    NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const invalid_target = add_replacement_node<IncompatibleReplacementNodeModel>(model);
    ConnectionId const old_connection{source, 0U, old_target, 0U};
    ConnectionId const invalid_connection{source, 0U, invalid_target, 0U};
    model.addConnection(old_connection);
    REQUIRE(model.connectionExists(old_connection));

    BasicGraphicsScene scene(model);
    auto &undo_stack = scene.undoStack();
    auto *source_delegate = replacement_delegate(model, source);
    auto *old_target_delegate = replacement_delegate(model, old_target);
    auto *invalid_target_delegate = replacement_delegate(model, invalid_target);
    source_delegate->reset_callback_counts();
    old_target_delegate->reset_callback_counts();
    invalid_target_delegate->reset_callback_counts();

    undo_stack.push(new ReplaceConnectionCommand(&scene, invalid_connection, {old_connection}));

    CHECK(model.connectionExists(old_connection));
    CHECK_FALSE(model.connectionExists(invalid_connection));
    CHECK(undo_stack.count() == 0);
    CHECK(source_delegate->output_deleted_count() == 0);
    CHECK(source_delegate->output_created_count() == 0);
    CHECK(old_target_delegate->input_deleted_count() == 0);
    CHECK(old_target_delegate->input_data_set_count() == 0);
    CHECK(invalid_target_delegate->input_created_count() == 0);
    CHECK(invalid_target_delegate->input_data_set_count() == 0);
}

TEST_CASE("Replacement preparation is fallible but admitted replay is not", "[undo][connections]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();

    SECTION("Per-edge deletion and replacement preparation refusal leave history unchanged")
    {
        ReplacementGraphModel model(registry);
        NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const new_target = add_replacement_node<ReplacementNodeModel>(model);
        ConnectionId const old_connection{source, 0U, old_target, 0U};
        ConnectionId const new_connection{source, 0U, new_target, 0U};
        model.addConnection(old_connection);
        REQUIRE(model.connectionExists(old_connection));

        BasicGraphicsScene scene(model);
        QSignalSpy created_spy(&model, &DataFlowGraphModel::connectionCreated);
        QSignalSpy deleted_spy(&model, &DataFlowGraphModel::connectionDeleted);
        auto *source_delegate = replacement_delegate(model, source);
        auto *old_target_delegate = replacement_delegate(model, old_target);
        auto *new_target_delegate = replacement_delegate(model, new_target);
        source_delegate->reset_callback_counts();
        old_target_delegate->reset_callback_counts();
        new_target_delegate->reset_callback_counts();
        model.set_delete_connection_fails(true);
        CHECK_FALSE(model.deleteConnection(old_connection));
        REQUIRE(model.connectionExists(old_connection));
        model.set_prepare_connection_replacement_fails(true);

        scene.undoStack().push(
            new ReplaceConnectionCommand(&scene, new_connection, {old_connection}));

        CHECK(model.connectionExists(old_connection));
        CHECK_FALSE(model.connectionExists(new_connection));
        CHECK(scene.connectionGraphicsObject(old_connection) != nullptr);
        CHECK(scene.connectionGraphicsObject(new_connection) == nullptr);
        CHECK(scene.undoStack().count() == 0);
        CHECK(created_spy.count() == 0);
        CHECK(deleted_spy.count() == 0);
        CHECK(source_delegate->output_created_count() == 0);
        CHECK(source_delegate->output_deleted_count() == 0);
        CHECK(old_target_delegate->input_deleted_count() == 0);
        CHECK(new_target_delegate->input_created_count() == 0);
    }

    SECTION("Later refusal flags cannot stop an admitted transaction")
    {
        ReplacementGraphModel model(registry);
        NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const new_target = add_replacement_node<ReplacementNodeModel>(model);
        ConnectionId const old_connection{source, 0U, old_target, 0U};
        ConnectionId const new_connection{source, 0U, new_target, 0U};
        model.addConnection(old_connection);
        REQUIRE(model.connectionExists(old_connection));

        BasicGraphicsScene scene(model);
        QSignalSpy created_spy(&model, &DataFlowGraphModel::connectionCreated);
        QSignalSpy deleted_spy(&model, &DataFlowGraphModel::connectionDeleted);
        auto *source_delegate = replacement_delegate(model, source);
        auto *old_target_delegate = replacement_delegate(model, old_target);
        auto *new_target_delegate = replacement_delegate(model, new_target);
        source_delegate->reset_callback_counts();
        old_target_delegate->reset_callback_counts();
        new_target_delegate->reset_callback_counts();

        scene.undoStack().push(
            new ReplaceConnectionCommand(&scene, new_connection, {old_connection}));

        CHECK_FALSE(model.connectionExists(old_connection));
        CHECK(model.connectionExists(new_connection));
        CHECK(scene.connectionGraphicsObject(old_connection) == nullptr);
        CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
        CHECK(scene.undoStack().count() == 1);
        CHECK(scene.undoStack().index() == 1);
        CHECK(created_spy.count() == 1);
        CHECK(deleted_spy.count() == 1);

        model.set_delete_connection_fails(true);
        model.set_prepare_connection_replacement_fails(true);
        scene.undoStack().undo();

        CHECK(model.connectionExists(old_connection));
        CHECK_FALSE(model.connectionExists(new_connection));
        CHECK(scene.connectionGraphicsObject(old_connection) != nullptr);
        CHECK(scene.connectionGraphicsObject(new_connection) == nullptr);
        CHECK(scene.undoStack().count() == 1);
        CHECK(scene.undoStack().index() == 0);
        CHECK(created_spy.count() == 2);
        CHECK(deleted_spy.count() == 2);
        CHECK(source_delegate->output_created_count() == 2);
        CHECK(source_delegate->output_deleted_count() == 2);
        CHECK(old_target_delegate->input_created_count() == 1);
        CHECK(old_target_delegate->input_deleted_count() == 1);
        CHECK(new_target_delegate->input_created_count() == 1);
        CHECK(new_target_delegate->input_deleted_count() == 1);

        scene.undoStack().redo();

        CHECK_FALSE(model.connectionExists(old_connection));
        CHECK(model.connectionExists(new_connection));
        CHECK(scene.connectionGraphicsObject(old_connection) == nullptr);
        CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
        CHECK(scene.undoStack().count() == 1);
        CHECK(scene.undoStack().index() == 1);
        CHECK(created_spy.count() == 3);
        CHECK(deleted_spy.count() == 3);
        CHECK(source_delegate->output_created_count() == 3);
        CHECK(source_delegate->output_deleted_count() == 3);
        CHECK(old_target_delegate->input_deleted_count() == 2);
        CHECK(new_target_delegate->input_created_count() == 2);
    }
}

TEST_CASE("Replacement storage is complete before deletion observers can change validation",
          "[undo][connections]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();
    ReplacementGraphModel model(registry);

    NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
    NodeId const new_target = add_replacement_node<ReplacementNodeModel>(model);
    ConnectionId const old_connection{source, 0U, old_target, 0U};
    ConnectionId const new_connection{source, 0U, new_target, 0U};
    model.addConnection(old_connection);
    REQUIRE(model.connectionExists(old_connection));

    BasicGraphicsScene scene(model);
    auto *new_target_delegate = replacement_delegate(model, new_target);
    int observed_deletions = 0;
    QObject::connect(&model,
                     &DataFlowGraphModel::connectionDeleted,
                     &model,
                     [&](ConnectionId const connection_id) {
                         if (connection_id != old_connection) {
                             return;
                         }
                         ++observed_deletions;
                         CHECK_FALSE(model.connectionExists(old_connection));
                         CHECK(model.connectionExists(new_connection));
                         new_target_delegate->set_data_type_id(
                             QStringLiteral("became-incompatible-during-notification"));
                     });

    scene.undoStack().push(new ReplaceConnectionCommand(&scene, new_connection, {old_connection}));

    CHECK(observed_deletions == 1);
    CHECK_FALSE(model.connectionExists(old_connection));
    CHECK(model.connectionExists(new_connection));
    CHECK(scene.connectionGraphicsObject(old_connection) == nullptr);
    CHECK(scene.connectionGraphicsObject(new_connection) != nullptr);
    CHECK(scene.undoStack().count() == 1);
}

TEST_CASE("Many outputs and explicit input detach keep their existing history semantics",
          "[undo][connections]")
{
    auto app = applicationSetup();
    auto registry = create_replacement_registry();

    SECTION("Many output adds a second connection and undoes only the candidate")
    {
        ReplacementGraphModel model(registry);
        NodeId const source = add_replacement_node<ManyConnectionNodeModel>(model);
        NodeId const first_target = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const second_target = add_replacement_node<ReplacementNodeModel>(model);
        ConnectionId const first_connection{source, 0U, first_target, 0U};
        ConnectionId const second_connection{source, 0U, second_target, 0U};
        model.addConnection(first_connection);
        REQUIRE(model.connectionExists(first_connection));

        BasicGraphicsScene scene(model);
        GraphicsView view(&scene);
        prepare_view(view);
        auto &undo_stack = scene.undoStack();

        viewport_drag_connection(view, scene, source, PortType::Out, second_target, PortType::In);

        CHECK(model.connectionExists(first_connection));
        CHECK(model.connectionExists(second_connection));
        CHECK(model.connections(source, PortType::Out, 0U).size() == 2);
        CHECK(undo_stack.count() == 1);

        undo_stack.undo();
        CHECK(model.connectionExists(first_connection));
        CHECK_FALSE(model.connectionExists(second_connection));

        undo_stack.redo();
        CHECK(model.connectionExists(first_connection));
        CHECK(model.connectionExists(second_connection));
    }

    SECTION("Input detach remains a separate undoable operation")
    {
        ReplacementGraphModel model(registry);
        NodeId const source = add_replacement_node<ManyConnectionNodeModel>(model);
        NodeId const target = add_replacement_node<ReplacementNodeModel>(model);
        ConnectionId const connection{source, 0U, target, 0U};
        model.addConnection(connection);
        REQUIRE(model.connectionExists(connection));

        BasicGraphicsScene scene(model);
        GraphicsView view(&scene);
        prepare_view(view);
        auto &undo_stack = scene.undoStack();

        viewport_press_port(view, scene, target, PortType::In);

        CHECK_FALSE(model.connectionExists(connection));
        CHECK(draft_connection(scene) != nullptr);
        CHECK(undo_stack.count() == 1);

        viewport_release_at(view, QPointF(0.0, 200.0));
        undo_stack.undo();
        CHECK(model.connectionExists(connection));
    }

    SECTION("A model that prohibits detach keeps its connection and history")
    {
        ReplacementGraphModel model(registry);
        model.set_detach_possible(false);
        NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const target = add_replacement_node<ReplacementNodeModel>(model);
        ConnectionId const connection{source, 0U, target, 0U};
        model.addConnection(connection);
        REQUIRE(model.connectionExists(connection));

        BasicGraphicsScene scene(model);
        GraphicsView view(&scene);
        prepare_view(view);
        auto &undo_stack = scene.undoStack();

        viewport_press_port(view, scene, target, PortType::In);

        CHECK(model.connectionExists(connection));
        CHECK(draft_connection(scene) == nullptr);
        CHECK(undo_stack.count() == 0);
    }

    SECTION("A model that prohibits detach rejects output replacement atomically")
    {
        ReplacementGraphModel model(registry);
        NodeId const source = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const old_target = add_replacement_node<ReplacementNodeModel>(model);
        NodeId const new_target = add_replacement_node<ReplacementNodeModel>(model);
        ConnectionId const old_connection{source, 0U, old_target, 0U};
        ConnectionId const new_connection{source, 0U, new_target, 0U};
        model.addConnection(old_connection);
        REQUIRE(model.connectionExists(old_connection));
        model.set_detach_possible(false);

        BasicGraphicsScene scene(model);
        GraphicsView view(&scene);
        prepare_view(view);
        auto &undo_stack = scene.undoStack();

        viewport_drag_connection(view, scene, source, PortType::Out, new_target, PortType::In);

        CHECK(model.connectionExists(old_connection));
        CHECK_FALSE(model.connectionExists(new_connection));
        CHECK(undo_stack.count() == 0);
    }
}
