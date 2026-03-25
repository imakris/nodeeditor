#include "DataFlowGraphicsScene.hpp"

#include "ConnectionGraphicsObject.hpp"
#include "GraphicsView.hpp"
#include "NodeDelegateModelRegistry.hpp"
#include "NodeGraphicsObject.hpp"
#include "SerializationValidation.hpp"
#include "UndoCommands.hpp"

#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGraphicsSceneMoveEvent>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QWidgetAction>

#include <QtCore/QBuffer>
#include <QtCore/QByteArray>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QtGlobal>

#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

using QtNodes::GroupId;
using QtNodes::InvalidGroupId;
using QtNodes::InvalidNodeId;
using QtNodes::NodeId;

std::unordered_set<NodeId> collect_node_ids(QJsonArray const &nodesJsonArray)
{
    std::unordered_set<NodeId> nodeIds;
    nodeIds.reserve(nodesJsonArray.size());

    for (QJsonValue const &nodeValue : nodesJsonArray) {
        if (!nodeValue.isObject()) {
            throw std::logic_error("Serialized scene contains invalid node entry");
        }

        NodeId nodeId = InvalidNodeId;
        if (!QtNodes::detail::read_node_id(nodeValue.toObject()["id"], nodeId)) {
            throw std::logic_error("Serialized scene contains invalid node id");
        }

        if (!nodeIds.insert(nodeId).second) {
            throw std::logic_error("Serialized scene contains duplicate node ids");
        }
    }

    return nodeIds;
}

void validate_groups_json(QJsonObject const &sceneJson)
{
    if (!sceneJson.contains("groups")) {
        return;
    }

    QJsonArray groupsJsonArray;
    if (!QtNodes::detail::read_required_array(sceneJson, "groups", groupsJsonArray)) {
        throw std::logic_error("Serialized scene contains invalid groups array");
    }

    QJsonArray nodesJsonArray;
    if (!QtNodes::detail::read_required_array(sceneJson, "nodes", nodesJsonArray)) {
        throw std::logic_error("Serialized scene is missing nodes array");
    }

    std::unordered_set<NodeId> const nodeIds = collect_node_ids(nodesJsonArray);
    std::unordered_set<GroupId> seenGroupIds;

    for (QJsonValue const &groupValue : groupsJsonArray) {
        if (!groupValue.isObject()) {
            throw std::logic_error("Serialized scene contains invalid group entry");
        }

        QJsonObject const groupObject = groupValue.toObject();

        GroupId groupId = InvalidGroupId;
        if (!QtNodes::detail::read_group_id(groupObject["id"], groupId)) {
            throw std::logic_error("Serialized scene contains invalid group id");
        }

        if (!seenGroupIds.insert(groupId).second) {
            throw std::logic_error("Serialized scene contains duplicate group ids");
        }

        QString groupName;
        if (!QtNodes::detail::read_required_string(groupObject, "name", groupName)) {
            throw std::logic_error("Serialized scene contains invalid group name");
        }
        Q_UNUSED(groupName);

        QJsonArray nodeIdsJson;
        if (!QtNodes::detail::read_required_array(groupObject, "nodes", nodeIdsJson)) {
            throw std::logic_error("Serialized scene contains invalid group nodes");
        }

        bool locked = true;
        if (!QtNodes::detail::read_optional_bool(groupObject, "locked", locked)) {
            throw std::logic_error("Serialized scene contains invalid group lock state");
        }
        Q_UNUSED(locked);

        for (QJsonValue const &idValue : nodeIdsJson) {
            NodeId nodeId = InvalidNodeId;
            if (!QtNodes::detail::read_node_id(idValue, nodeId) || nodeIds.count(nodeId) == 0) {
                throw std::logic_error("Serialized scene group references unknown node id");
            }
        }
    }
}

} // namespace

namespace QtNodes {

DataFlowGraphicsScene::DataFlowGraphicsScene(DataFlowGraphModel &graphModel, QObject *parent)
    : BasicGraphicsScene(graphModel, parent)
    , _graphModel(graphModel)
{
    connect(&_graphModel,
            &DataFlowGraphModel::inPortDataWasSet,
            [this](NodeId const nodeId, PortType const, PortIndex const) { onNodeUpdated(nodeId); });
}

// TODO constructor for an empyt scene?

std::vector<NodeId> DataFlowGraphicsScene::selectedNodes() const
{
    QList<QGraphicsItem *> graphicsItems = selectedItems();

    std::vector<NodeId> result;
    result.reserve(graphicsItems.size());

    for (QGraphicsItem *item : graphicsItems) {
        auto ngo = qgraphicsitem_cast<NodeGraphicsObject *>(item);

        if (ngo != nullptr) {
            result.push_back(ngo->nodeId());
        }
    }

    return result;
}

QMenu *DataFlowGraphicsScene::createSceneMenu(QPointF const scenePos)
{
    QMenu *modelMenu = new QMenu();

    // Add filterbox to the context menu
    auto *txtBox = new QLineEdit(modelMenu);
    txtBox->setPlaceholderText(QStringLiteral("Filter"));
    txtBox->setClearButtonEnabled(true);

    auto *txtBoxAction = new QWidgetAction(modelMenu);
    txtBoxAction->setDefaultWidget(txtBox);

    // 1.
    modelMenu->addAction(txtBoxAction);

    // Add result treeview to the context menu
    QTreeWidget *treeView = new QTreeWidget(modelMenu);
    treeView->header()->close();

    auto *treeViewAction = new QWidgetAction(modelMenu);
    treeViewAction->setDefaultWidget(treeView);

    // 2.
    modelMenu->addAction(treeViewAction);

    auto registry = _graphModel.dataModelRegistry();

    for (auto const &cat : registry->categories()) {
        auto item = new QTreeWidgetItem(treeView);
        item->setText(0, cat);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
    }

    for (auto const &assoc : registry->registeredModelsCategoryAssociation()) {
        QList<QTreeWidgetItem *> parent = treeView->findItems(assoc.second, Qt::MatchExactly);

        if (parent.count() <= 0)
            continue;

        auto item = new QTreeWidgetItem(parent.first());
        item->setText(0, assoc.first);
    }

    treeView->expandAll();

    connect(treeView,
            &QTreeWidget::itemClicked,
            [this, modelMenu, scenePos](QTreeWidgetItem *item, int) {
                if (!(item->flags() & (Qt::ItemIsSelectable))) {
                    return;
                }

                this->undoStack().push(new CreateCommand(this, item->text(0), scenePos));

                modelMenu->close();
            });

    //Setup filtering
    connect(txtBox, &QLineEdit::textChanged, [treeView](const QString &text) {
        QTreeWidgetItemIterator categoryIt(treeView, QTreeWidgetItemIterator::HasChildren);
        while (*categoryIt)
            (*categoryIt++)->setHidden(true);
        QTreeWidgetItemIterator it(treeView, QTreeWidgetItemIterator::NoChildren);
        while (*it) {
            auto modelName = (*it)->text(0);
            const bool match = (modelName.contains(text, Qt::CaseInsensitive));
            (*it)->setHidden(!match);
            if (match) {
                QTreeWidgetItem *parent = (*it)->parent();
                while (parent) {
                    parent->setHidden(false);
                    parent = parent->parent();
                }
            }
            ++it;
        }
    });

    // make sure the text box gets focus so the user doesn't have to click on it
    txtBox->setFocus();

    // QMenu's instance auto-destruction
    modelMenu->setAttribute(Qt::WA_DeleteOnClose);

    return modelMenu;
}

bool DataFlowGraphicsScene::save() const
{
    QString fileName = QFileDialog::getSaveFileName(nullptr,
                                                    tr("Open Flow Scene"),
                                                    QDir::homePath(),
                                                    tr("Flow Scene Files (*.flow)"));

    if (!fileName.isEmpty()) {
        if (!fileName.endsWith("flow", Qt::CaseInsensitive))
            fileName += ".flow";

        QFile file(fileName);
        if (file.open(QIODevice::WriteOnly)) {
            QJsonObject sceneJson = _graphModel.save();

            QJsonArray groupsJsonArray;
            for (auto const &[groupId, groupPtr] : groups()) {
                if (!groupPtr)
                    continue;

                QJsonObject groupJson;
                groupJson["id"] = static_cast<qint64>(groupId);
                groupJson["name"] = groupPtr->name();

                QJsonArray nodeIdsJson;
                for (NodeId const nodeId : groupPtr->nodeIDs()) {
                    nodeIdsJson.append(static_cast<qint64>(nodeId));
                }
                groupJson["nodes"] = nodeIdsJson;
                groupJson["locked"] = groupPtr->groupGraphicsObject().locked();

                groupsJsonArray.append(groupJson);
            }

            if (!groupsJsonArray.isEmpty()) {
                sceneJson["groups"] = groupsJsonArray;
            }

            file.write(QJsonDocument(sceneJson).toJson());
            return true;
        }
    }
    return false;
}

bool DataFlowGraphicsScene::load()
{
    QString fileName = QFileDialog::getOpenFileName(nullptr,
                                                    tr("Open Flow Scene"),
                                                    QDir::homePath(),
                                                    tr("Flow Scene Files (*.flow)"));

    if (!QFileInfo::exists(fileName))
        return false;

    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly))
        return false;

    QByteArray const wholeFile = file.readAll();

    QJsonParseError parseError{};
    QJsonDocument const sceneDocument = QJsonDocument::fromJson(wholeFile, &parseError);
    if (parseError.error != QJsonParseError::NoError || !sceneDocument.isObject())
        return false;

    QJsonObject const sceneJson = sceneDocument.object();

    try {
        validate_groups_json(sceneJson);

        DataFlowGraphModel stagingModel(_graphModel.dataModelRegistry());
        stagingModel.load(sceneJson);
    } catch (...) {
        return false;
    }

    clearScene();

    try {
        _graphModel.load(sceneJson);
    } catch (...) {
        return false;
    }

    if (sceneJson.contains("groups")) {
        QJsonArray const groupsJsonArray = sceneJson["groups"].toArray();

        for (QJsonValue groupValue : groupsJsonArray) {
            QJsonObject const groupObject = groupValue.toObject();

            QJsonArray const nodeIdsJson = groupObject["nodes"].toArray();
            std::vector<NodeGraphicsObject *> groupNodes;
            groupNodes.reserve(nodeIdsJson.size());

            for (QJsonValue idValue : nodeIdsJson) {
                NodeId nodeId = InvalidNodeId;
                detail::read_node_id(idValue, nodeId);
                if (auto *nodeObject = nodeGraphicsObject(nodeId)) {
                    groupNodes.push_back(nodeObject);
                }
            }

            if (groupNodes.empty())
                continue;

            QString const groupName = groupObject["name"].toString();
            GroupId groupId = InvalidGroupId;
            detail::read_group_id(groupObject["id"], groupId);

            auto const groupWeak = createGroup(groupNodes, groupName, groupId);
            if (auto group = groupWeak.lock()) {
                bool const locked = groupObject["locked"].toBool(true);
                group->groupGraphicsObject().lock(locked);
            }
        }
    }

    Q_EMIT sceneLoaded();

    return true;
}

void DataFlowGraphicsScene::updateConnectionGraphics(
    const std::unordered_set<ConnectionId> &connections, bool state)
{
    for (auto const &c : connections) {
        if (auto *cgo = connectionGraphicsObject(c)) {
            cgo->connectionState().setFrozen(state);
            cgo->update();
        }
    }
}

} // namespace QtNodes
