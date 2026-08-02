#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QPointF>
#include <QtCore/QPointer>
#include <QtCore/QSize>

#include <QtNodes/AbstractGraphModel>
#include <QtNodes/ConnectionIdIndex>
#include <QtNodes/StyleCollection>

#include <memory>
#include <unordered_map>
#include <vector>

using ConnectionId = QtNodes::ConnectionId;
using ConnectionPolicy = QtNodes::ConnectionPolicy;
using NodeFlag = QtNodes::NodeFlag;
using NodeId = QtNodes::NodeId;
using NodeRole = QtNodes::NodeRole;
using PortCount = QtNodes::PortCount;
using PortIndex = QtNodes::PortIndex;
using PortRole = QtNodes::PortRole;
using PortType = QtNodes::PortType;
using StyleCollection = QtNodes::StyleCollection;
using QtNodes::InvalidNodeId;

class PortAddRemoveWidget;

/**
 * The class implements a bare minimum required to demonstrate a model-based
 * graph.
 */
class DynamicPortsModel : public QtNodes::AbstractGraphModel
{
    Q_OBJECT
public:
    /// Operational bounds for this widget-heavy teaching example.
    static constexpr std::size_t s_max_serialized_nodes = 128;
    static constexpr PortCount s_max_serialized_ports_per_node = 32;
    static constexpr std::size_t s_max_serialized_ports = 256;
    static constexpr std::size_t s_max_serialized_connections = 256;
    static constexpr qsizetype s_max_serialized_bytes = 1024 * 1024;

    struct NodeGeometryData
    {
        QSize size;
        QPointF pos;
    };

public:
    DynamicPortsModel();

    ~DynamicPortsModel() override;

    NodeIdSet const &allNodeIds() const override;

    ConnectionIdSet const &allConnectionIds(NodeId const nodeId) const override;

    ConnectionIdSet const &connections(NodeId nodeId,
                                       PortType portType,
                                       PortIndex portIndex) const override;

    bool connectionExists(ConnectionId const connectionId) const override;

    NodeId addNode(QString const nodeType = QString()) override;

    /// Adds and positions one UI-created node, returning false on expected refusal.
    bool try_add_node(QPointF const &position, NodeId *node_id = nullptr) noexcept;

    /**
   * Connection is possible when graph contains no connectivity data
   * in both directions `Out -> In` and `In -> Out`.
   */
    bool connectionPossible(ConnectionId const connectionId,
                            std::vector<ConnectionId> const &replacedConnectionIds) const override;

    [[nodiscard]] std::unique_ptr<QtNodes::ConnectionReplacementTransaction>
    prepareConnectionReplacement(
        std::vector<ConnectionId> const &removedConnectionIds,
        std::vector<ConnectionId> const &addedConnectionIds) noexcept override;

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

    QJsonObject save() const;

    /// @brief Creates a new node based on the informatoin in `nodeJson`.
    /**
   * @param nodeJson conains a `NodeId`, node's position, internal node
   * information.
   */
    void loadNode(QJsonObject const &nodeJson) override;

    bool load(QJsonObject const &jsonDocument);

    /// Parses a complete JSON document and applies it only when fully valid.
    bool load_from_json(QByteArray const &serialized);

    bool addPort(NodeId nodeId, PortType portType, PortIndex portIndex);

    void removePort(NodeId nodeId, PortType portType, PortIndex first);

    NodeId newNodeId() override;

private:
    bool port_count_allowed(NodeId node_id, PortType port_type, PortCount count) const;

    bool position_allowed(QPointF const &position) const;

    void delete_widget(QPointer<PortAddRemoveWidget> const &widget) const;

    NodeIdSet _nodeIds;

    /// [Important] This is a user defined data structure backing your model.
    /// In your case it could be anything else representing a graph, for example, a
    /// table. Or a collection of structs with pointers to each other. Or an
    /// abstract syntax tree, you name it.
    QtNodes::ConnectionIdIndex _connectionIndex;

    mutable std::unordered_map<NodeId, NodeGeometryData> _nodeGeometryData;

    struct NodePortCount
    {
        unsigned int in = 0;
        unsigned int out = 0;
    };

    PortAddRemoveWidget *widget(NodeId) const;

    mutable std::unordered_map<NodeId, NodePortCount> _nodePortCounts;
    mutable std::unordered_map<NodeId, QPointer<PortAddRemoveWidget>> _nodeWidgets;

    /// A convenience variable needed for generating unique node ids.
    NodeId _nextNodeId;
};
