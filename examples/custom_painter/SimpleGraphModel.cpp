#include "SimpleGraphModel.hpp"

#include <vector>

SimpleGraphModel::SimpleGraphModel()
    : _nextNodeId{0}
{}

SimpleGraphModel::~SimpleGraphModel() {}

QtNodes::AbstractGraphModel::NodeIdSet const &SimpleGraphModel::allNodeIds() const
{
    return _nodeIds;
}

QtNodes::AbstractGraphModel::ConnectionIdSet const &
SimpleGraphModel::allConnectionIds(NodeId const nodeId) const
{
    auto const it = _nodeConnections.find(nodeId);
    if (it == _nodeConnections.end()) {
        return emptyConnections();
    }

    return it->second;
}

QtNodes::AbstractGraphModel::ConnectionIdSet const &
SimpleGraphModel::connections(NodeId nodeId, PortType portType, PortIndex portIndex) const
{
    if (portType == PortType::None) {
        return emptyConnections();
    }

    auto const &connectionsByPort = (portType == PortType::In) ? _inConnectionsByPort
                                                                : _outConnectionsByPort;
    auto const nodeIt = connectionsByPort.find(nodeId);
    if (nodeIt == connectionsByPort.end()) {
        return emptyConnections();
    }

    auto const portIt = nodeIt->second.find(portIndex);
    if (portIt == nodeIt->second.end()) {
        return emptyConnections();
    }

    return portIt->second;
}

bool SimpleGraphModel::connectionExists(ConnectionId const connectionId) const
{
    return (_connectivity.find(connectionId) != _connectivity.end());
}

NodeId SimpleGraphModel::addNode(QString const nodeType)
{
    NodeId newId = newNodeId();
    _nodeIds.insert(newId);

    Q_EMIT nodeCreated(newId);

    return newId;
}

bool SimpleGraphModel::connectionPossible(ConnectionId const connectionId) const
{
    return _connectivity.find(connectionId) == _connectivity.end();
}

void SimpleGraphModel::addConnection(ConnectionId const connectionId)
{
    _connectivity.insert(connectionId);
    indexConnection(connectionId);

    Q_EMIT connectionCreated(connectionId);
}

bool SimpleGraphModel::nodeExists(NodeId const nodeId) const
{
    return (_nodeIds.find(nodeId) != _nodeIds.end());
}

QVariant SimpleGraphModel::nodeData(NodeId nodeId, NodeRole role) const
{
    QVariant result;

    switch (role) {
    case NodeRole::Type:
        result = QString("Custom Painted Node");
        break;

    case NodeRole::Position:
        result = _nodeGeometryData[nodeId].pos;
        break;

    case NodeRole::Size:
        result = _nodeGeometryData[nodeId].size;
        break;

    case NodeRole::CaptionVisible:
        result = true;
        break;

    case NodeRole::Caption:
        result = QString("Node %1").arg(nodeId);
        break;

    case NodeRole::Style: {
        auto style = StyleCollection::nodeStyle();
        result = style.toJson().toVariantMap();
    } break;

    case NodeRole::InternalData:
        break;

    case NodeRole::InPortCount:
        result = 2u;
        break;

    case NodeRole::OutPortCount:
        result = 2u;
        break;

    case NodeRole::Widget:
        result = QVariant();
        break;

    default:
        break;
    }

    return result;
}

bool SimpleGraphModel::setNodeData(NodeId nodeId, NodeRole role, QVariant value)
{
    bool result = false;

    switch (role) {
    case NodeRole::Position: {
        _nodeGeometryData[nodeId].pos = value.value<QPointF>();
        Q_EMIT nodePositionUpdated(nodeId);
        result = true;
    } break;

    case NodeRole::Size: {
        _nodeGeometryData[nodeId].size = value.value<QSize>();
        result = true;
    } break;

    default:
        break;
    }

    return result;
}

QVariant SimpleGraphModel::portData(NodeId nodeId,
                                    PortType portType,
                                    PortIndex portIndex,
                                    PortRole role) const
{
    switch (role) {
    case PortRole::Data:
        return QVariant();

    case PortRole::DataType:
        return QVariant();

    case PortRole::ConnectionPolicyRole:
        return QVariant::fromValue(ConnectionPolicy::One);

    case PortRole::CaptionVisible:
        return false;

    case PortRole::Caption:
        return QString();
    }

    return QVariant();
}

bool SimpleGraphModel::setPortData(
    NodeId nodeId, PortType portType, PortIndex portIndex, QVariant const &value, PortRole role)
{
    Q_UNUSED(nodeId);
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    Q_UNUSED(value);
    Q_UNUSED(role);

    return false;
}

bool SimpleGraphModel::deleteConnection(ConnectionId const connectionId)
{
    bool disconnected = false;

    auto it = _connectivity.find(connectionId);

    if (it != _connectivity.end()) {
        disconnected = true;
        _connectivity.erase(it);
        unindexConnection(connectionId);
    }

    if (disconnected)
        Q_EMIT connectionDeleted(connectionId);

    return disconnected;
}

bool SimpleGraphModel::deleteNode(NodeId const nodeId)
{
    std::vector<ConnectionId> connectionIds;
    auto const &attachedConnections = allConnectionIds(nodeId);
    connectionIds.reserve(attachedConnections.size());
    for (auto const &connectionId : attachedConnections) {
        connectionIds.push_back(connectionId);
    }

    for (auto const &cId : connectionIds) {
        deleteConnection(cId);
    }

    _nodeIds.erase(nodeId);
    _nodeGeometryData.erase(nodeId);

    Q_EMIT nodeDeleted(nodeId);

    return true;
}

QJsonObject SimpleGraphModel::saveNode(NodeId const nodeId) const
{
    QJsonObject nodeJson;

    nodeJson["id"] = static_cast<qint64>(nodeId);

    {
        QPointF const pos = nodeData(nodeId, NodeRole::Position).value<QPointF>();

        QJsonObject posJson;
        posJson["x"] = pos.x();
        posJson["y"] = pos.y();
        nodeJson["position"] = posJson;
    }

    return nodeJson;
}

void SimpleGraphModel::loadNode(QJsonObject const &nodeJson)
{
    NodeId restoredNodeId = static_cast<NodeId>(nodeJson["id"].toInt());

    _nextNodeId = std::max(_nextNodeId, restoredNodeId + 1);

    _nodeIds.insert(restoredNodeId);

    Q_EMIT nodeCreated(restoredNodeId);

    {
        QJsonObject posJson = nodeJson["position"].toObject();
        QPointF const pos(posJson["x"].toDouble(), posJson["y"].toDouble());

        setNodeData(restoredNodeId, NodeRole::Position, pos);
    }
}

QtNodes::AbstractGraphModel::ConnectionIdSet const &SimpleGraphModel::emptyConnections()
{
    static ConnectionIdSet const empty{};
    return empty;
}

void SimpleGraphModel::indexConnection(ConnectionId const connectionId)
{
    _nodeConnections[connectionId.inNodeId].insert(connectionId);
    _nodeConnections[connectionId.outNodeId].insert(connectionId);
    _inConnectionsByPort[connectionId.inNodeId][connectionId.inPortIndex].insert(connectionId);
    _outConnectionsByPort[connectionId.outNodeId][connectionId.outPortIndex].insert(connectionId);
}

void SimpleGraphModel::unindexConnection(ConnectionId const connectionId)
{
    auto eraseFromNode = [&](NodeId nodeId) {
        auto nodeIt = _nodeConnections.find(nodeId);
        if (nodeIt == _nodeConnections.end()) {
            return;
        }

        nodeIt->second.erase(connectionId);
        if (nodeIt->second.empty()) {
            _nodeConnections.erase(nodeIt);
        }
    };

    auto eraseFromPortMap =
        [&](std::unordered_map<NodeId, ConnectionsByPort> &connectionsByPort,
            NodeId nodeId,
            PortIndex portIndex) {
            auto nodeIt = connectionsByPort.find(nodeId);
            if (nodeIt == connectionsByPort.end()) {
                return;
            }

            auto portIt = nodeIt->second.find(portIndex);
            if (portIt == nodeIt->second.end()) {
                return;
            }

            portIt->second.erase(connectionId);
            if (portIt->second.empty()) {
                nodeIt->second.erase(portIt);
            }

            if (nodeIt->second.empty()) {
                connectionsByPort.erase(nodeIt);
            }
        };

    eraseFromNode(connectionId.inNodeId);
    eraseFromNode(connectionId.outNodeId);
    eraseFromPortMap(_inConnectionsByPort, connectionId.inNodeId, connectionId.inPortIndex);
    eraseFromPortMap(_outConnectionsByPort, connectionId.outNodeId, connectionId.outPortIndex);
}
