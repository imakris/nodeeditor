#pragma once

#include <QtCore/QJsonObject>
#include <QtCore/QPointF>
#include <QtCore/QSize>

#include <QtNodes/AbstractGraphModel>
#include <QtNodes/ConnectionIdIndex>
#include <QtNodes/ConnectionIdUtils>
#include <QtNodes/StyleCollection>

#include <unordered_map>

using ConnectionId = QtNodes::ConnectionId;
using ConnectionPolicy = QtNodes::ConnectionPolicy;
using NodeFlag = QtNodes::NodeFlag;
using NodeId = QtNodes::NodeId;
using NodeRole = QtNodes::NodeRole;
using PortIndex = QtNodes::PortIndex;
using PortRole = QtNodes::PortRole;
using PortType = QtNodes::PortType;
using StyleCollection = QtNodes::StyleCollection;
using QtNodes::InvalidNodeId;

/// Simple graph model for demonstrating custom painters
class SimpleGraphModel : public QtNodes::AbstractGraphModel
{
    Q_OBJECT
public:
    struct NodeGeometryData
    {
        QSize size;
        QPointF pos;
    };

public:
    SimpleGraphModel();

    ~SimpleGraphModel() override;

    NodeIdSet const &allNodeIds() const override;

    ConnectionIdSet const &allConnectionIds(NodeId const nodeId) const override;

    ConnectionIdSet const &connections(NodeId nodeId,
                                       PortType portType,
                                       PortIndex portIndex) const override;

    bool connectionExists(ConnectionId const connectionId) const override;

    NodeId addNode(QString const nodeType = QString()) override;

    bool connectionPossible(ConnectionId const connectionId) const override;

    void addConnection(ConnectionId const connectionId) override;

    bool nodeExists(NodeId const nodeId) const override;

    QVariant nodeData(NodeId nodeId, NodeRole role) const override;

    bool setNodeData(NodeId nodeId, NodeRole role, QVariant value) override;

    QVariant portData(NodeId nodeId,
                      PortType portType,
                      PortIndex portIndex,
                      PortRole role) const override;

    bool setPortData(NodeId nodeId,
                     PortType portType,
                     PortIndex portIndex,
                     QVariant const &value,
                     PortRole role = PortRole::Data) override;

    bool deleteConnection(ConnectionId const connectionId) override;

    bool deleteNode(NodeId const nodeId) override;

    QJsonObject saveNode(NodeId const) const override;

    void loadNode(QJsonObject const &nodeJson) override;

    NodeId newNodeId() override { return _nextNodeId++; }

private:
    NodeIdSet _nodeIds;
    QtNodes::ConnectionIdIndex _connectionIndex;
    mutable std::unordered_map<NodeId, NodeGeometryData> _nodeGeometryData;
    NodeId _nextNodeId;
};
