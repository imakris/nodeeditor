#pragma once

#include <QtNodes/AbstractGraphModel>
#include <QtNodes/ConnectionIdIndex>

#include <QDebug>
#include <QJsonObject>
#include <QPointF>
#include <QSizeF>

#include <algorithm>
#include <exception>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using QtNodes::AbstractGraphModel;
using QtNodes::ConnectionId;
using QtNodes::NodeFlags;
using QtNodes::NodeId;
using QtNodes::NodeRole;
using QtNodes::PortIndex;
using QtNodes::PortRole;
using QtNodes::PortType;

namespace TestGraphModelDetail {

template<typename Notification>
void publishConnectionReplacementNotification(Notification &&notification)
{
    try {
        notification();
    } catch (std::exception const &error) {
        qWarning() << "Connection replacement notification failed:" << error.what();
    } catch (...) {
        qWarning() << "Connection replacement notification failed with an unknown exception";
    }
}

class ThrowingMoveConnectionReplacementPublisher
{
public:
    ThrowingMoveConnectionReplacementPublisher() = default;
    ThrowingMoveConnectionReplacementPublisher(
        ThrowingMoveConnectionReplacementPublisher const &) = delete;
    ThrowingMoveConnectionReplacementPublisher &operator=(
        ThrowingMoveConnectionReplacementPublisher const &) = delete;

    ThrowingMoveConnectionReplacementPublisher(ThrowingMoveConnectionReplacementPublisher &&other)
        : _throwOnMove(true)
    {
        if (other._throwOnMove) {
            throw std::runtime_error("connection replacement publisher move failed");
        }
    }

    void operator()(std::vector<ConnectionId> const &, std::vector<ConnectionId> const &) {}

private:
    bool _throwOnMove = false;
};

} // namespace TestGraphModelDetail

/**
 * @brief A simple test implementation of AbstractGraphModel for unit testing.
 */
class TestGraphModel : public AbstractGraphModel
{
    Q_OBJECT

public:
    TestGraphModel()
        : AbstractGraphModel()
    {}

    NodeId newNodeId() override { return _nextNodeId++; }

    NodeIdSet const &allNodeIds() const override { return _nodeIds; }

    ConnectionIdSet const &allConnectionIds(NodeId const nodeId) const override
    {
        return _connection_index.allConnectionIds(nodeId);
    }

    ConnectionIdSet const &connections(NodeId nodeId,
                                       PortType portType,
                                       PortIndex portIndex) const override
    {
        return _connection_index.connections(nodeId, portType, portIndex);
    }

    bool connectionExists(ConnectionId const connectionId) const override
    {
        return _connection_index.contains(connectionId);
    }

    NodeId addNode(QString const nodeType = QString()) override
    {
        NodeId id = newNodeId();
        _nodeIds.insert(id);
        _nodeData[id][NodeRole::Type] = nodeType;
        _nodeData[id][NodeRole::Position] = QPointF(0, 0);
        _nodeData[id][NodeRole::Caption] = QString("Node %1").arg(id);
        _nodeData[id][NodeRole::InPortCount] = 1u;
        _nodeData[id][NodeRole::OutPortCount] = 1u;
        Q_EMIT nodeCreated(id);
        return id;
    }

    bool connectionPossible(ConnectionId const connectionId,
                            std::vector<ConnectionId> const &replacedConnectionIds) const override
    {
        bool const duplicate = connectionExists(connectionId)
                               && std::find(replacedConnectionIds.begin(),
                                            replacedConnectionIds.end(),
                                            connectionId)
                                      == replacedConnectionIds.end();
        // Basic validation: nodes exist and not connecting to self
        return nodeExists(connectionId.inNodeId) && nodeExists(connectionId.outNodeId)
               && connectionId.inNodeId != connectionId.outNodeId && !duplicate;
    }

    [[nodiscard]] std::unique_ptr<QtNodes::ConnectionReplacementTransaction>
    prepareConnectionReplacement(std::vector<ConnectionId> const &removedConnectionIds,
                                 std::vector<ConnectionId> const &addedConnectionIds) noexcept override
    {
        try {
            if (addedConnectionIds.size() != 1U) {
                return {};
            }
            for (ConnectionId const connectionId : removedConnectionIds) {
                if (!connectionExists(connectionId) || !detachPossible(connectionId)) {
                    return {};
                }
            }
            if (!connectionPossible(addedConnectionIds.front(), removedConnectionIds)) {
                return {};
            }

            auto preparedIndex = _connection_index.preparedReplacement(removedConnectionIds,
                                                                       addedConnectionIds);
            if (!preparedIndex) {
                return {};
            }

            if (_throwOnReplacementPublisherMove) {
                using Publisher = TestGraphModelDetail::ThrowingMoveConnectionReplacementPublisher;
                using Transaction = QtNodes::ConnectionIdIndexReplacementTransaction<Publisher>;
                return std::make_unique<Transaction>(_connection_index,
                                                     std::move(*preparedIndex),
                                                     removedConnectionIds,
                                                     addedConnectionIds,
                                                     Publisher{});
            }

            auto publisher = [this](std::vector<ConnectionId> const &removedIds,
                                    std::vector<ConnectionId> const &addedIds) {
                for (ConnectionId const connectionId : removedIds) {
                    TestGraphModelDetail::publishConnectionReplacementNotification(
                        [&] { Q_EMIT connectionDeleted(connectionId); });
                }
                for (ConnectionId const connectionId : addedIds) {
                    TestGraphModelDetail::publishConnectionReplacementNotification(
                        [&] { Q_EMIT connectionCreated(connectionId); });
                }
            };
            using Transaction = QtNodes::ConnectionIdIndexReplacementTransaction<decltype(publisher)>;
            return std::make_unique<Transaction>(_connection_index,
                                                 std::move(*preparedIndex),
                                                 removedConnectionIds,
                                                 addedConnectionIds,
                                                 std::move(publisher));
        } catch (...) {
            return {};
        }
    }

    void setThrowOnReplacementPublisherMove(bool enabled)
    {
        _throwOnReplacementPublisherMove = enabled;
    }

    void addConnection(ConnectionId const connectionId) override
    {
        if (connectionPossible(connectionId, {})) {
            _connection_index.add(connectionId);
            Q_EMIT connectionCreated(connectionId);
        }
    }

    bool nodeExists(NodeId const nodeId) const override
    {
        return _nodeIds.find(nodeId) != _nodeIds.end();
    }

    QVariant nodeData(NodeId nodeId, NodeRole role) const override
    {
        auto nodeIt = _nodeData.find(nodeId);
        if (nodeIt != _nodeData.end()) {
            auto roleIt = nodeIt->second.find(role);
            if (roleIt != nodeIt->second.end()) {
                return roleIt->second;
            }
        }

        // Provide default values for essential display properties
        switch (role) {
        case NodeRole::Type:
            return QString("TestNode");

        case NodeRole::Caption:
            return QString("Test Node %1").arg(nodeId);

        case NodeRole::CaptionVisible:
            return true;

        case NodeRole::Size:
            return QSizeF(120, 80);

        case NodeRole::Position:
            return QPointF(0, 0); // Default position if none set

        default:
            break;
        }

        return QVariant();
    }

    // Make the template version from the base class available
    using AbstractGraphModel::nodeData;

    bool setNodeData(NodeId nodeId, NodeRole role, QVariant value) override
    {
        if (nodeExists(nodeId)) {
            _nodeData[nodeId][role] = value;

            // Only emit specific signals for user-initiated changes
            // Don't emit for computed/internal roles to avoid recursion
            switch (role) {
            case NodeRole::Position:
                Q_EMIT nodePositionUpdated(nodeId);
                break;
            case NodeRole::Type:
            case NodeRole::Caption:
            case NodeRole::CaptionVisible:
            case NodeRole::InPortCount:
            case NodeRole::OutPortCount:
                Q_EMIT nodeUpdated(nodeId);
                break;
            case NodeRole::Size:
            case NodeRole::Style:
            case NodeRole::InternalData:
            case NodeRole::Widget:
                // These are often computed/internal - don't emit signals
                break;
            }
            return true;
        }
        return false;
    }

    QVariant portData(NodeId nodeId,
                      PortType portType,
                      PortIndex portIndex,
                      PortRole role) const override
    {
        Q_UNUSED(nodeId)
        Q_UNUSED(portType)
        Q_UNUSED(portIndex)
        Q_UNUSED(role)
        return QVariant();
    }

    bool setPortData(NodeId nodeId,
                     PortType portType,
                     PortIndex portIndex,
                     QVariant const &value,
                     PortRole role = PortRole::Data) override
    {
        Q_UNUSED(nodeId)
        Q_UNUSED(portType)
        Q_UNUSED(portIndex)
        Q_UNUSED(value)
        Q_UNUSED(role)
        return false;
    }

    bool deleteConnection(ConnectionId const connectionId) override
    {
        if (_connection_index.remove(connectionId)) {
            Q_EMIT connectionDeleted(connectionId);
            return true;
        }
        return false;
    }

    bool deleteNode(NodeId const nodeId) override
    {
        if (!nodeExists(nodeId))
            return false;

        std::vector<ConnectionId> connectionsToRemove;
        auto const &attachedConnections = allConnectionIds(nodeId);
        connectionsToRemove.reserve(attachedConnections.size());
        for (auto const &conn : attachedConnections) {
            connectionsToRemove.push_back(conn);
        }

        for (auto const &conn : connectionsToRemove) {
            deleteConnection(conn);
        }

        // Remove the node
        _nodeIds.erase(nodeId);
        _nodeData.erase(nodeId);
        Q_EMIT nodeDeleted(nodeId);
        return true;
    }

    QJsonObject saveNode(NodeId const nodeId) const override
    {
        QJsonObject result;
        result["id"] = static_cast<int>(nodeId);
        auto nodeIt = _nodeData.find(nodeId);
        if (nodeIt != _nodeData.end()) {
            const auto &data = nodeIt->second;
            auto posIt = data.find(NodeRole::Position);
            if (posIt != data.end()) {
                QPointF pos = posIt->second.toPointF();
                QJsonObject posObj;
                posObj["x"] = pos.x();
                posObj["y"] = pos.y();
                result["position"] = posObj;
            }
            auto typeIt = data.find(NodeRole::Type);
            if (typeIt != data.end()) {
                result["type"] = typeIt->second.toString();
            }
        }
        return result;
    }

    void loadNode(QJsonObject const &nodeJson) override
    {
        NodeId id = static_cast<NodeId>(nodeJson["id"].toInt());

        _nodeIds.insert(id);

        if (id >= _nextNodeId) {
            _nextNodeId = id + 1;
        }

        QJsonObject posObj = nodeJson["position"].toObject();
        QPointF pos(posObj["x"].toDouble(), posObj["y"].toDouble());
        _nodeData[id][NodeRole::Position] = pos;

        if (nodeJson.contains("type")) {
            _nodeData[id][NodeRole::Type] = nodeJson["type"].toString();
        } else {
            _nodeData[id][NodeRole::Type] = QString("TestNode");
        }

        _nodeData[id][NodeRole::Caption] = QString("Node %1").arg(id);
        _nodeData[id][NodeRole::InPortCount] = 1u;
        _nodeData[id][NodeRole::OutPortCount] = 1u;

        Q_EMIT nodeCreated(id);
    }

private:
    NodeId _nextNodeId = 1;
    NodeIdSet _nodeIds;
    QtNodes::ConnectionIdIndex _connection_index;
    std::unordered_map<NodeId, std::unordered_map<NodeRole, QVariant>> _nodeData;
    bool _throwOnReplacementPublisherMove = false;
};
