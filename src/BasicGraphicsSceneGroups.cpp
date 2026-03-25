#include "BasicGraphicsScene.hpp"

#include "ConnectionIdUtils.hpp"
#include "NodeGraphicsObject.hpp"
#include "SerializationValidation.hpp"

#include <QtWidgets/QFileDialog>

#include <QtCore/QDir>
#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QIODevice>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QJsonValue>
#include <QtCore/QString>
#include <QtCore/QtGlobal>

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace {

using QtNodes::ConnectionId;
using QtNodes::GroupId;
using QtNodes::InvalidGroupId;
using QtNodes::InvalidNodeId;
using QtNodes::NodeId;

NodeId json_value_to_node_id(QJsonValue const &value)
{
    NodeId nodeId = InvalidNodeId;

    if (!QtNodes::detail::read_node_id(value, nodeId)) {
        return InvalidNodeId;
    }

    return nodeId;
}

void validate_group_json(QJsonObject const &groupJson)
{
    QString groupName;
    if (!QtNodes::detail::read_required_string(groupJson, "name", groupName)) {
        throw std::logic_error("Serialized group contains invalid name");
    }
    Q_UNUSED(groupName);

    QJsonArray nodesJson;
    if (!QtNodes::detail::read_required_array(groupJson, "nodes", nodesJson)) {
        throw std::logic_error("Serialized group contains invalid nodes array");
    }

    QJsonArray connectionsJson;
    if (!QtNodes::detail::read_required_array(groupJson, "connections", connectionsJson)) {
        throw std::logic_error("Serialized group contains invalid connections array");
    }

    for (QJsonValue const &nodeValue : nodesJson) {
        if (!nodeValue.isObject()) {
            throw std::logic_error("Serialized group contains invalid node entry");
        }
    }

    for (QJsonValue const &connectionValue : connectionsJson) {
        if (!connectionValue.isObject()) {
            throw std::logic_error("Serialized group contains invalid connection entry");
        }

        ConnectionId connId;
        if (!QtNodes::tryFromJson(connectionValue.toObject(), connId)) {
            throw std::logic_error("Serialized group contains invalid connection id");
        }
    }
}

} // namespace

namespace QtNodes {

std::vector<ConnectionId> BasicGraphicsScene::connectionsWithinGroup(GroupId groupID)
{
    if (!_groupingEnabled)
        return {};

    std::vector<ConnectionId> ret{};
    ret.reserve(_connectionGraphicsObjects.size());

    for (auto const &connection : _connectionGraphicsObjects) {
        auto outNode = nodeGraphicsObject(connection.first.outNodeId);
        auto inNode = nodeGraphicsObject(connection.first.inNodeId);
        if (outNode && inNode) {
            auto group1 = outNode->nodeGroup().lock();
            auto group2 = inNode->nodeGroup().lock();
            if (group1 && group2 && group1->id() == group2->id() && group1->id() == groupID) {
                ret.push_back(connection.first);
            }
        }
    }

    return ret;
}

std::weak_ptr<NodeGroup> BasicGraphicsScene::createGroup(std::vector<NodeGraphicsObject *> &nodes,
                                                         QString groupName,
                                                         GroupId groupId)
{
    if (!_groupingEnabled || nodes.empty())
        return {};

    for (auto *node : nodes) {
        if (!node->nodeGroup().expired())
            removeNodeFromGroup(node->nodeId());
    }

    if (groupName.isEmpty()) {
        groupName = "Group " + QString::number(NodeGroup::groupCount());
    }

    if (groupId == InvalidGroupId) {
        groupId = nextGroupId();
    } else {
        if (_groups.count(groupId) != 0) {
            throw std::runtime_error("Group identifier collision");
        }

        if (groupId >= _nextGroupId && _nextGroupId != InvalidGroupId) {
            _nextGroupId = groupId + 1;
        }
    }

    auto group = std::make_shared<NodeGroup>(nodes, groupId, groupName, this);
    auto ggo = std::make_unique<GroupGraphicsObject>(*this, *group);

    group->setGraphicsObject(std::move(ggo));

    for (auto *nodePtr : nodes) {
        auto *node = _nodeGraphicsObjects[nodePtr->nodeId()].get();
        node->setNodeGroup(group);
    }

    std::weak_ptr<NodeGroup> groupWeakPtr = group;
    _groups[group->id()] = std::move(group);
    return groupWeakPtr;
}

void BasicGraphicsScene::addNodeToGroup(NodeId nodeId, GroupId groupId)
{
    if (!_groupingEnabled)
        return;

    auto groupIt = _groups.find(groupId);
    auto nodeIt = _nodeGraphicsObjects.find(nodeId);
    if (groupIt == _groups.end() || nodeIt == _nodeGraphicsObjects.end())
        return;

    auto group = groupIt->second;
    auto *node = nodeIt->second.get();
    group->addNode(node);
    node->setNodeGroup(group);
}

void BasicGraphicsScene::removeNodeFromGroup(NodeId nodeId)
{
    if (!_groupingEnabled)
        return;

    auto nodeIt = _nodeGraphicsObjects.find(nodeId);
    if (nodeIt == _nodeGraphicsObjects.end())
        return;

    auto group = nodeIt->second->nodeGroup().lock();
    if (group) {
        group->removeNode(nodeIt->second.get());
        if (group->empty()) {
            _groups.erase(group->id());
        }
    }
    nodeIt->second->unsetNodeGroup();
    nodeIt->second->lock(false);
}

NodeGraphicsObject &BasicGraphicsScene::loadNodeToMap(QJsonObject nodeJson, bool keepOriginalId)
{
    NodeId newNodeId = InvalidNodeId;

    if (keepOriginalId) {
        newNodeId = json_value_to_node_id(nodeJson["id"]);
        if (newNodeId == InvalidNodeId) {
            throw std::logic_error("Invalid node id in serialized node");
        }
    } else {
        newNodeId = _graphModel.newNodeId();
        nodeJson["id"] = static_cast<qint64>(newNodeId);
    }

    _graphModel.loadNode(nodeJson);

    auto *nodeObject = nodeGraphicsObject(newNodeId);
    if (!nodeObject) {
        auto graphicsObject = std::make_unique<NodeGraphicsObject>(*this, newNodeId);
        nodeObject = graphicsObject.get();
        _nodeGraphicsObjects[newNodeId] = std::move(graphicsObject);
    }

    return *nodeObject;
}

void BasicGraphicsScene::loadConnectionToMap(QJsonObject const &connectionJson,
                                             std::unordered_map<NodeId, NodeId> const &nodeIdMap)
{
    ConnectionId connId;
    if (!tryFromJson(connectionJson, connId)) {
        throw std::logic_error("Invalid serialized connection");
    }

    auto const outIt = nodeIdMap.find(connId.outNodeId);
    auto const inIt = nodeIdMap.find(connId.inNodeId);

    if (outIt == nodeIdMap.end() || inIt == nodeIdMap.end()) {
        throw std::logic_error("Serialized connection references unknown node id");
    }

    ConnectionId remapped{outIt->second, connId.outPortIndex, inIt->second, connId.inPortIndex};

    if (_graphModel.connectionExists(remapped)) {
        return;
    }

    if (!_graphModel.connectionPossible(remapped)) {
        throw std::logic_error("Serialized connection is not valid for restored nodes");
    }

    _graphModel.addConnection(remapped);
}

std::pair<std::weak_ptr<NodeGroup>, std::unordered_map<GroupId, GroupId>>
BasicGraphicsScene::restoreGroup(QJsonObject const &groupJson)
{
    if (!_groupingEnabled)
        return {{}, {}};

    validate_group_json(groupJson);

    std::unordered_map<GroupId, GroupId> IDsMap{};
    std::unordered_map<NodeId, NodeId> nodeIdMap{};
    std::vector<NodeGraphicsObject *> groupChildren{};
    std::vector<NodeId> createdNodeIds{};

    try {
        QJsonArray const nodesJson = groupJson["nodes"].toArray();
        for (QJsonValue const &nodeJson : nodesJson) {
            QJsonObject nodeObject = nodeJson.toObject();
            NodeId const oldNodeId = json_value_to_node_id(nodeObject["id"]);

            NodeGraphicsObject &nodeRef = loadNodeToMap(nodeObject, false);
            NodeId const newNodeId = nodeRef.nodeId();

            createdNodeIds.push_back(newNodeId);

            if (oldNodeId != InvalidNodeId) {
                nodeIdMap.emplace(oldNodeId, newNodeId);
                IDsMap.emplace(static_cast<GroupId>(oldNodeId), static_cast<GroupId>(newNodeId));
            }

            groupChildren.push_back(&nodeRef);
        }

        QJsonArray const connectionJsonArray = groupJson["connections"].toArray();
        for (QJsonValue const &connection : connectionJsonArray) {
            loadConnectionToMap(connection.toObject(), nodeIdMap);
        }

        return {createGroup(groupChildren, groupJson["name"].toString()), IDsMap};
    } catch (...) {
        for (NodeId const nodeId : createdNodeIds) {
            if (_graphModel.nodeExists(nodeId)) {
                _graphModel.deleteNode(nodeId);
            }
        }

        throw;
    }
}

std::unordered_map<GroupId, std::shared_ptr<NodeGroup>> const &BasicGraphicsScene::groups() const
{
    return _groups;
}

void BasicGraphicsScene::saveGroupFile(GroupId groupID)
{
    if (!_groupingEnabled)
        return;

    QString fileName = QFileDialog::getSaveFileName(nullptr,
                                                    tr("Save Node Group"),
                                                    QDir::homePath(),
                                                    tr("Node Group files (*.group)"));

    if (fileName.isEmpty())
        return;

    if (!fileName.endsWith("group", Qt::CaseInsensitive))
        fileName += ".group";

    auto groupIt = _groups.find(groupID);
    if (groupIt == _groups.end()) {
        qDebug() << "Error! Couldn't find group while saving.";
        return;
    }

    QFile file(fileName);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(groupIt->second->saveToFile());
    } else {
        qDebug() << "Error saving group file!";
    }
}

std::weak_ptr<NodeGroup> BasicGraphicsScene::loadGroupFile()
{
    if (!_groupingEnabled)
        return {};

    QString fileName = QFileDialog::getOpenFileName(nullptr,
                                                    tr("Open Node Group"),
                                                    QDir::currentPath(),
                                                    tr("Node Group files (*.group)"));

    if (!QFileInfo::exists(fileName))
        return {};

    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly)) {
        qDebug() << "Error loading group file!";
        return {};
    }

    struct CurrentDirGuard
    {
        QString path;

        ~CurrentDirGuard()
        {
            if (!path.isEmpty()) {
                QDir::setCurrent(path);
            }
        }
    };

    CurrentDirGuard currentDirGuard{QDir::currentPath()};
    QDir const directory = QFileInfo(fileName).absoluteDir();
    QDir::setCurrent(directory.absolutePath());

    QByteArray const wholeFile = file.readAll();

    QJsonParseError parseError{};
    QJsonDocument const groupDocument = QJsonDocument::fromJson(wholeFile, &parseError);
    if (parseError.error != QJsonParseError::NoError || !groupDocument.isObject()) {
        return {};
    }

    try {
        return restoreGroup(groupDocument.object()).first;
    } catch (std::exception const &ex) {
        qWarning() << "Failed to load group file:" << ex.what();
        return {};
    } catch (...) {
        qWarning() << "Failed to load group file due to an unknown error";
        return {};
    }
}

GroupId BasicGraphicsScene::nextGroupId()
{
    if (_nextGroupId == InvalidGroupId) {
        throw std::runtime_error("No available group identifiers");
    }

    while (_groups.count(_nextGroupId) != 0) {
        ++_nextGroupId;
        if (_nextGroupId == InvalidGroupId) {
            throw std::runtime_error("No available group identifiers");
        }
    }

    GroupId const newId = _nextGroupId;
    ++_nextGroupId;
    return newId;
}

} // namespace QtNodes
