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

/**
 * The class implements a bare minimum required to demonstrate a model-based
 * graph.
 */
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

    /**
   * Connection is possible when graph contains no connectivity data
   * in both directions `Out -> In` and `In -> Out`.
   */
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

    /// @brief Creates a new node based on the informatoin in `nodeJson`.
    /**
   * @param nodeJson conains a `NodeId`, node's position, internal node
   * information.
   */
    void loadNode(QJsonObject const &nodeJson) override;

    NodeId newNodeId() override { return _nextNodeId++; }

private:
    NodeIdSet _nodeIds;

    /// [Important] This is a user defined data structure backing your model.
    /// In your case it could be anything else representing a graph, for example, a
    /// table. Or a collection of structs with pointers to each other. Or an
    /// abstract syntax tree, you name it.
    ///
    /// This data structure contains the graph connectivity information in both
    /// directions, i.e. from Node1 to Node2 and from Node2 to Node1.
    QtNodes::ConnectionIdIndex _connectionIndex;

    mutable std::unordered_map<NodeId, NodeGeometryData> _nodeGeometryData;

    /// A convenience variable needed for generating unique node ids.
    NodeId _nextNodeId;
};
