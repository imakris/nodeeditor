#include "DynamicPortsModel.hpp"

#include "PortAddRemoveWidget.hpp"

#include <QtNodes/ConnectionIdUtils>

#include <QtCore/QDebug>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>

#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

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

struct restored_node_t
{
    NodeId id;
    QPointF position;
    PortCount in_port_count;
    PortCount out_port_count;
};

struct port_counts_t
{
    PortCount in;
    PortCount out;
};

struct restored_graph_t
{
    std::vector<restored_node_t> nodes;
    std::vector<ConnectionId> connections;
    QtNodes::ConnectionIdIndex connection_index;
};

enum class publication_kind_t {
    connection_deleted,
    node_deleted,
    node_position_updated,
    node_created,
    connection_created,
};

struct publication_event_t
{
    publication_kind_t kind;
    NodeId node_id = InvalidNodeId;
    ConnectionId connection_id{};
};

bool read_node_id(QJsonValue const &value, NodeId &node_id)
{
    return QtNodes::detail::read_node_id(value, node_id);
}

NodeId node_id_after(NodeId node_id) noexcept
{
    return node_id == InvalidNodeId - 1 ? InvalidNodeId : node_id + 1;
}

bool read_port_count(QJsonValue const &value, PortCount &port_count)
{
    if (!value.isString()) {
        return false;
    }

    bool ok = false;
    qulonglong const parsed = value.toString().toULongLong(&ok);
    if (!ok || value.toString() != QString::number(parsed)
        || parsed > DynamicPortsModel::s_max_serialized_ports_per_node) {
        return false;
    }

    port_count = static_cast<PortCount>(parsed);
    return true;
}

std::optional<restored_node_t> parse_node(QJsonObject const &node_json)
{
    restored_node_t node{};
    if (!read_node_id(node_json["id"], node.id)
        || !read_port_count(node_json["inPortCount"], node.in_port_count)
        || !read_port_count(node_json["outPortCount"], node.out_port_count)) {
        return std::nullopt;
    }

    QJsonValue const position_value = node_json["position"];
    if (!position_value.isObject()) {
        return std::nullopt;
    }

    QJsonObject const position_json = position_value.toObject();
    QJsonValue const x_value = position_json["x"];
    QJsonValue const y_value = position_json["y"];
    if (!x_value.isDouble() || !y_value.isDouble()) {
        return std::nullopt;
    }

    double const x = x_value.toDouble();
    double const y = y_value.toDouble();
    if (!std::isfinite(x) || !std::isfinite(y)) {
        return std::nullopt;
    }

    node.position = QPointF(x, y);
    return node;
}

std::optional<restored_graph_t> parse_graph(QJsonObject const &json_document)
{
    QJsonValue const nodes_value = json_document["nodes"];
    QJsonValue const connections_value = json_document["connections"];
    if (!nodes_value.isArray() || !connections_value.isArray()) {
        return std::nullopt;
    }

    restored_graph_t graph;
    QJsonArray const nodes_json = nodes_value.toArray();
    QJsonArray const connections_json = connections_value.toArray();
    if (nodes_json.size() > DynamicPortsModel::s_max_serialized_nodes
        || connections_json.size() > DynamicPortsModel::s_max_serialized_connections) {
        return std::nullopt;
    }

    graph.nodes.reserve(nodes_json.size());

    std::unordered_map<NodeId, port_counts_t> port_counts;
    port_counts.reserve(nodes_json.size());

    std::size_t total_ports = 0;
    for (QJsonValue const &node_value : nodes_json) {
        if (!node_value.isObject()) {
            return std::nullopt;
        }

        auto node = parse_node(node_value.toObject());
        if (!node) {
            return std::nullopt;
        }

        if (!port_counts.emplace(node->id, port_counts_t{node->in_port_count, node->out_port_count})
                 .second) {
            return std::nullopt;
        }

        std::size_t const node_ports = static_cast<std::size_t>(node->in_port_count)
                                       + static_cast<std::size_t>(node->out_port_count);
        if (node_ports > DynamicPortsModel::s_max_serialized_ports - total_ports) {
            return std::nullopt;
        }
        total_ports += node_ports;

        graph.nodes.push_back(*node);
    }

    graph.connections.reserve(connections_json.size());

    for (QJsonValue const &connection_value : connections_json) {
        if (!connection_value.isObject()) {
            return std::nullopt;
        }

        ConnectionId connection_id;
        if (!QtNodes::tryFromJson(connection_value.toObject(), connection_id)) {
            return std::nullopt;
        }

        auto const out_node = port_counts.find(connection_id.outNodeId);
        auto const in_node = port_counts.find(connection_id.inNodeId);
        if (out_node == port_counts.end() || in_node == port_counts.end()
            || connection_id.outPortIndex >= out_node->second.out
            || connection_id.inPortIndex >= in_node->second.in
            || graph.connection_index.contains(connection_id)
            || !graph.connection_index
                    .connections(connection_id.outNodeId, PortType::Out, connection_id.outPortIndex)
                    .empty()
            || !graph.connection_index
                    .connections(connection_id.inNodeId, PortType::In, connection_id.inPortIndex)
                    .empty()) {
            return std::nullopt;
        }

        graph.connection_index.add(connection_id);
        graph.connections.push_back(connection_id);
    }

    return graph;
}

} // namespace

DynamicPortsModel::DynamicPortsModel()
    : _nextNodeId{0}
{}

DynamicPortsModel::~DynamicPortsModel()
{
    for (auto const &[node_id, node_widget] : _nodeWidgets) {
        Q_UNUSED(node_id);
        if (node_widget) {
            delete node_widget.data();
        }
    }
}

QtNodes::AbstractGraphModel::NodeIdSet const &DynamicPortsModel::allNodeIds() const
{
    return _nodeIds;
}

QtNodes::AbstractGraphModel::ConnectionIdSet const &DynamicPortsModel::allConnectionIds(
    NodeId const nodeId) const
{
    return _connectionIndex.allConnectionIds(nodeId);
}

QtNodes::AbstractGraphModel::ConnectionIdSet const &DynamicPortsModel::connections(
    NodeId nodeId, PortType portType, PortIndex portIndex) const
{
    return _connectionIndex.connections(nodeId, portType, portIndex);
}

bool DynamicPortsModel::connectionExists(ConnectionId const connectionId) const
{
    return _connectionIndex.contains(connectionId);
}

NodeId DynamicPortsModel::addNode(QString const nodeType)
{
    Q_UNUSED(nodeType);

    if (_nodeIds.size() >= s_max_serialized_nodes) {
        return InvalidNodeId;
    }

    NodeId newId = newNodeId();

    // Create new node.
    _nodeIds.insert(newId);

    Q_EMIT nodeCreated(newId);

    return newId;
}

bool DynamicPortsModel::try_add_node(QPointF const &position, NodeId *node_id) noexcept
{
    try {
        if (!position_allowed(position) || _nodeIds.size() >= s_max_serialized_nodes
            || _nextNodeId == InvalidNodeId) {
            return false;
        }

        NodeId const prepared_node_id = _nextNodeId;
        NodeIdSet prepared_node_ids = _nodeIds;
        auto prepared_geometry = _nodeGeometryData;
        auto prepared_port_counts = _nodePortCounts;
        auto prepared_widgets = _nodeWidgets;
        auto node_widget = std::make_unique<PortAddRemoveWidget>(0, 0, prepared_node_id, *this);

        prepared_node_ids.insert(prepared_node_id);
        prepared_geometry.emplace(prepared_node_id, NodeGeometryData{QSize{}, position});
        prepared_port_counts.emplace(prepared_node_id, NodePortCount{});
        prepared_widgets.emplace(prepared_node_id, node_widget.get());

        _nodeIds.swap(prepared_node_ids);
        _nodeGeometryData.swap(prepared_geometry);
        _nodePortCounts.swap(prepared_port_counts);
        _nodeWidgets.swap(prepared_widgets);
        _nextNodeId = node_id_after(prepared_node_id);
        node_widget.release();

        if (node_id) {
            *node_id = prepared_node_id;
        }
        publishConnectionReplacementNotification([&] { Q_EMIT nodeCreated(prepared_node_id); });
        publishConnectionReplacementNotification(
            [&] { Q_EMIT nodePositionUpdated(prepared_node_id); });
        return true;
    } catch (...) {
        return false;
    }
}

bool DynamicPortsModel::connectionPossible(
    ConnectionId const connectionId, std::vector<ConnectionId> const &replacedConnectionIds) const
{
    auto const out_count = _nodePortCounts.find(connectionId.outNodeId);
    auto const in_count = _nodePortCounts.find(connectionId.inNodeId);
    if (out_count == _nodePortCounts.end() || in_count == _nodePortCounts.end()
        || connectionId.outPortIndex >= out_count->second.out
        || connectionId.inPortIndex >= in_count->second.in) {
        return false;
    }

    auto isReplaced = [&](ConnectionId const candidate) {
        return std::find(replacedConnectionIds.begin(), replacedConnectionIds.end(), candidate)
               != replacedConnectionIds.end();
    };
    auto vacant = [&](NodeId nodeId, PortType portType, PortIndex portIndex) {
        auto const &attached = connections(nodeId, portType, portIndex);
        return std::all_of(attached.begin(), attached.end(), isReplaced);
    };

    std::size_t removed_count = 0;
    for (auto replaced = replacedConnectionIds.begin(); replaced != replacedConnectionIds.end();
         ++replaced) {
        if (_connectionIndex.contains(*replaced)
            && std::find(replacedConnectionIds.begin(), replaced, *replaced) == replaced) {
            ++removed_count;
        }
    }
    std::size_t const retained_count = _connectionIndex.connectivity().size() - removed_count;
    std::size_t const added_count = !_connectionIndex.contains(connectionId)
                                            || isReplaced(connectionId)
                                        ? 1
                                        : 0;

    return retained_count + added_count <= s_max_serialized_connections
           && (!_connectionIndex.contains(connectionId) || isReplaced(connectionId))
           && vacant(connectionId.outNodeId, PortType::Out, connectionId.outPortIndex)
           && vacant(connectionId.inNodeId, PortType::In, connectionId.inPortIndex);
}

std::unique_ptr<QtNodes::ConnectionReplacementTransaction>
DynamicPortsModel::prepareConnectionReplacement(
    std::vector<ConnectionId> const &removedConnectionIds,
    std::vector<ConnectionId> const &addedConnectionIds) noexcept
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

        auto preparedIndex = _connectionIndex.preparedReplacement(removedConnectionIds,
                                                                  addedConnectionIds);
        if (!preparedIndex) {
            return {};
        }

        auto publisher = [this](std::vector<ConnectionId> const &removedIds,
                                std::vector<ConnectionId> const &addedIds) {
            for (ConnectionId const connectionId : removedIds) {
                publishConnectionReplacementNotification(
                    [&] { Q_EMIT connectionDeleted(connectionId); });
            }
            for (ConnectionId const connectionId : addedIds) {
                publishConnectionReplacementNotification(
                    [&] { Q_EMIT connectionCreated(connectionId); });
            }
        };
        using Transaction = QtNodes::ConnectionIdIndexReplacementTransaction<decltype(publisher)>;
        return std::make_unique<Transaction>(_connectionIndex,
                                             std::move(*preparedIndex),
                                             removedConnectionIds,
                                             addedConnectionIds,
                                             std::move(publisher));
    } catch (...) {
        return {};
    }
}

void DynamicPortsModel::addConnection(ConnectionId const connectionId)
{
    auto const out_count = _nodePortCounts.find(connectionId.outNodeId);
    auto const in_count = _nodePortCounts.find(connectionId.inNodeId);
    if (_connectionIndex.connectivity().size() >= s_max_serialized_connections
        || out_count == _nodePortCounts.end() || in_count == _nodePortCounts.end()
        || connectionId.outPortIndex >= out_count->second.out
        || connectionId.inPortIndex >= in_count->second.in
        || !connectionPossible(connectionId, {})) {
        return;
    }

    _connectionIndex.add(connectionId);

    Q_EMIT connectionCreated(connectionId);
}

bool DynamicPortsModel::nodeExists(NodeId const nodeId) const
{
    return (_nodeIds.find(nodeId) != _nodeIds.end());
}

PortAddRemoveWidget *DynamicPortsModel::widget(NodeId nodeId) const
{
    auto it = _nodeWidgets.find(nodeId);
    if (it == _nodeWidgets.end() || !it->second) {
        NodePortCount const counts = _nodePortCounts[nodeId];
        auto node_widget = std::make_unique<PortAddRemoveWidget>(counts.in,
                                                                 counts.out,
                                                                 nodeId,
                                                                 *const_cast<DynamicPortsModel *>(
                                                                     this));
        PortAddRemoveWidget *const result = node_widget.get();
        _nodeWidgets.insert_or_assign(nodeId, result);
        node_widget.release();
        return result;
    }

    return it->second.data();
}

QVariant DynamicPortsModel::nodeData(NodeId nodeId, NodeRole role) const
{
    Q_UNUSED(nodeId);

    QVariant result;

    switch (role) {
    case NodeRole::Type:
        result = QString("Default Node Type");
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
        result = QString("Node");
        break;

    case NodeRole::Style: {
        auto style = StyleCollection::nodeStyle();
        result = style.toJson().toVariantMap();
    } break;

    case NodeRole::InternalData:
        break;

    case NodeRole::InPortCount:
        result = _nodePortCounts[nodeId].in;
        break;

    case NodeRole::OutPortCount:
        result = _nodePortCounts[nodeId].out;
        break;

    case NodeRole::Widget: {
        result = QVariant::fromValue(widget(nodeId));
        break;
    }
    }

    return result;
}

bool DynamicPortsModel::setNodeData(NodeId nodeId, NodeRole role, QVariant value)
{
    if (!nodeExists(nodeId)) {
        return false;
    }

    bool result = false;

    switch (role) {
    case NodeRole::Type:
        break;
    case NodeRole::Position: {
        QPointF const position = value.value<QPointF>();
        if (!position_allowed(position)) {
            break;
        }
        _nodeGeometryData[nodeId].pos = position;

        Q_EMIT nodePositionUpdated(nodeId);

        result = true;
    } break;

    case NodeRole::Size: {
        _nodeGeometryData[nodeId].size = value.value<QSize>();
        result = true;
    } break;

    case NodeRole::CaptionVisible:
        break;

    case NodeRole::Caption:
        break;

    case NodeRole::Style:
        break;

    case NodeRole::InternalData:
        break;

    case NodeRole::InPortCount:
    case NodeRole::OutPortCount: {
        bool ok = false;
        qulonglong const parsed = value.toULongLong(&ok);
        if (!ok || parsed > s_max_serialized_ports_per_node) {
            break;
        }

        PortType const port_type = role == NodeRole::InPortCount ? PortType::In : PortType::Out;
        PortCount const count = static_cast<PortCount>(parsed);
        if (!port_count_allowed(nodeId, port_type, count)) {
            break;
        }

        PortCount const current = port_type == PortType::In ? _nodePortCounts[nodeId].in
                                                            : _nodePortCounts[nodeId].out;
        for (PortIndex port_index = count; port_index < current; ++port_index) {
            if (!connections(nodeId, port_type, port_index).empty()) {
                return false;
            }
        }

        if (port_type == PortType::In) {
            _nodePortCounts[nodeId].in = count;
        } else {
            _nodePortCounts[nodeId].out = count;
        }
        widget(nodeId)->populateButtons(port_type, count);
        result = true;
    } break;

    case NodeRole::Widget:
        break;
    }

    return result;
}

QVariant DynamicPortsModel::portData(NodeId nodeId,
                                     PortType portType,
                                     PortIndex portIndex,
                                     PortRole role) const
{
    switch (role) {
    case PortRole::Data:
        return QVariant();
        break;

    case PortRole::DataType:
        return QVariant();
        break;

    case PortRole::ConnectionPolicy:
        return QVariant::fromValue(ConnectionPolicy::One);
        break;

    case PortRole::CaptionVisible:
        return true;
        break;

    case PortRole::Caption:
        if (portType == PortType::In)
            return QString::fromUtf8("Port In");
        else
            return QString::fromUtf8("Port Out");

        break;
    }

    return QVariant();
}

bool DynamicPortsModel::setPortData(
    NodeId nodeId, PortType portType, PortIndex portIndex, QVariant const &value, PortRole role)
{
    Q_UNUSED(nodeId);
    Q_UNUSED(portType);
    Q_UNUSED(portIndex);
    Q_UNUSED(value);
    Q_UNUSED(role);

    return false;
}

bool DynamicPortsModel::deleteConnection(ConnectionId const connectionId)
{
    bool const disconnected = _connectionIndex.remove(connectionId);

    if (disconnected)
        Q_EMIT connectionDeleted(connectionId);

    return disconnected;
}

bool DynamicPortsModel::deleteNode(NodeId const nodeId)
{
    if (!nodeExists(nodeId)) {
        return false;
    }

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
    _nodePortCounts.erase(nodeId);
    QPointer<PortAddRemoveWidget> const node_widget = _nodeWidgets[nodeId];
    _nodeWidgets.erase(nodeId);

    Q_EMIT nodeDeleted(nodeId);
    delete_widget(node_widget);

    return true;
}

QJsonObject DynamicPortsModel::saveNode(NodeId const nodeId) const
{
    QJsonObject nodeJson;

    nodeJson["id"] = static_cast<qint64>(nodeId);

    {
        QPointF const pos = nodeData(nodeId, NodeRole::Position).value<QPointF>();

        QJsonObject posJson;
        posJson["x"] = pos.x();
        posJson["y"] = pos.y();
        nodeJson["position"] = posJson;

        nodeJson["inPortCount"] = QString::number(_nodePortCounts[nodeId].in);
        nodeJson["outPortCount"] = QString::number(_nodePortCounts[nodeId].out);
    }

    return nodeJson;
}

QJsonObject DynamicPortsModel::save() const
{
    QJsonObject sceneJson;

    QJsonArray nodesJsonArray;
    for (auto const nodeId : allNodeIds()) {
        nodesJsonArray.append(saveNode(nodeId));
    }
    sceneJson["nodes"] = nodesJsonArray;

    QJsonArray connJsonArray;
    for (auto const &cid : _connectionIndex.connectivity()) {
        connJsonArray.append(QtNodes::toJson(cid));
    }
    sceneJson["connections"] = connJsonArray;

    return sceneJson;
}

void DynamicPortsModel::loadNode(QJsonObject const &nodeJson)
{
    auto const node = parse_node(nodeJson);
    if (!node) {
        throw std::logic_error("Serialized dynamic-ports graph contains invalid node");
    }

    std::size_t existing_ports = 0;
    for (auto const &[existing_id, counts] : _nodePortCounts) {
        Q_UNUSED(existing_id);
        existing_ports += static_cast<std::size_t>(counts.in) + counts.out;
    }
    if (nodeExists(node->id) || _nodeIds.size() >= s_max_serialized_nodes
        || static_cast<std::size_t>(node->in_port_count) + node->out_port_count
               > s_max_serialized_ports - existing_ports) {
        throw std::logic_error("Serialized dynamic-ports graph contains invalid node");
    }

    auto node_widget = std::make_unique<PortAddRemoveWidget>(node->in_port_count,
                                                             node->out_port_count,
                                                             node->id,
                                                             *this);
    NodeIdSet prepared_node_ids = _nodeIds;
    auto prepared_geometry = _nodeGeometryData;
    auto prepared_port_counts = _nodePortCounts;
    auto prepared_widgets = _nodeWidgets;

    prepared_node_ids.insert(node->id);
    prepared_geometry.emplace(node->id, NodeGeometryData{QSize{}, node->position});
    prepared_port_counts.emplace(node->id, NodePortCount{node->in_port_count, node->out_port_count});
    prepared_widgets.emplace(node->id, node_widget.get());
    NodeId const prepared_next_node_id = std::max(_nextNodeId, node_id_after(node->id));

    _nodeIds.swap(prepared_node_ids);
    _nodeGeometryData.swap(prepared_geometry);
    _nodePortCounts.swap(prepared_port_counts);
    _nodeWidgets.swap(prepared_widgets);
    _nextNodeId = prepared_next_node_id;
    node_widget.release();

    publishConnectionReplacementNotification([&] { Q_EMIT nodePositionUpdated(node->id); });
    publishConnectionReplacementNotification([&] { Q_EMIT nodeCreated(node->id); });
}

bool DynamicPortsModel::load(QJsonObject const &jsonDocument)
{
    try {
        auto graph = parse_graph(jsonDocument);
        if (!graph) {
            return false;
        }

        NodeIdSet prepared_node_ids;
        std::unordered_map<NodeId, NodeGeometryData> prepared_geometry;
        std::unordered_map<NodeId, NodePortCount> prepared_port_counts;
        std::unordered_map<NodeId, QPointer<PortAddRemoveWidget>> prepared_widgets;
        std::vector<std::unique_ptr<PortAddRemoveWidget>> prepared_widget_owners;
        std::vector<publication_event_t> publication_events;
        std::unordered_set<ConnectionId> deleted_connections;

        prepared_node_ids.reserve(graph->nodes.size());
        prepared_geometry.reserve(graph->nodes.size());
        prepared_port_counts.reserve(graph->nodes.size());
        prepared_widgets.reserve(graph->nodes.size());
        prepared_widget_owners.reserve(graph->nodes.size());
        deleted_connections.reserve(_connectionIndex.connectivity().size());
        publication_events.reserve(_connectionIndex.connectivity().size() + _nodeIds.size()
                                   + graph->nodes.size() * 2 + graph->connections.size());

        NodeId prepared_next_node_id = 0;
        for (restored_node_t const &node : graph->nodes) {
            auto node_widget = std::make_unique<PortAddRemoveWidget>(node.in_port_count,
                                                                     node.out_port_count,
                                                                     node.id,
                                                                     *this);
            prepared_node_ids.insert(node.id);
            prepared_geometry.emplace(node.id, NodeGeometryData{QSize{}, node.position});
            prepared_port_counts.emplace(node.id,
                                         NodePortCount{node.in_port_count, node.out_port_count});
            prepared_widgets.emplace(node.id, node_widget.get());
            prepared_widget_owners.push_back(std::move(node_widget));
            prepared_next_node_id = std::max(prepared_next_node_id, node_id_after(node.id));
        }

        for (NodeId const node_id : _nodeIds) {
            for (ConnectionId const &connection_id : allConnectionIds(node_id)) {
                if (deleted_connections.insert(connection_id).second) {
                    publication_events.push_back(
                        publication_event_t{publication_kind_t::connection_deleted,
                                            InvalidNodeId,
                                            connection_id});
                }
            }
            publication_events.push_back(
                publication_event_t{publication_kind_t::node_deleted, node_id, {}});
        }
        for (restored_node_t const &node : graph->nodes) {
            publication_events.push_back(
                publication_event_t{publication_kind_t::node_position_updated, node.id, {}});
            publication_events.push_back(
                publication_event_t{publication_kind_t::node_created, node.id, {}});
        }
        for (ConnectionId const connection_id : graph->connections) {
            publication_events.push_back(publication_event_t{publication_kind_t::connection_created,
                                                             InvalidNodeId,
                                                             connection_id});
        }

        _nodeIds.swap(prepared_node_ids);
        _connectionIndex.swap(graph->connection_index);
        _nodeGeometryData.swap(prepared_geometry);
        _nodePortCounts.swap(prepared_port_counts);
        _nodeWidgets.swap(prepared_widgets);
        _nextNodeId = prepared_next_node_id;
        for (auto &node_widget : prepared_widget_owners) {
            node_widget.release();
        }

        for (publication_event_t const &event : publication_events) {
            switch (event.kind) {
            case publication_kind_t::connection_deleted:
                publishConnectionReplacementNotification(
                    [&] { Q_EMIT connectionDeleted(event.connection_id); });
                break;
            case publication_kind_t::node_deleted:
                publishConnectionReplacementNotification([&] { Q_EMIT nodeDeleted(event.node_id); });
                break;
            case publication_kind_t::node_position_updated:
                publishConnectionReplacementNotification(
                    [&] { Q_EMIT nodePositionUpdated(event.node_id); });
                break;
            case publication_kind_t::node_created:
                publishConnectionReplacementNotification([&] { Q_EMIT nodeCreated(event.node_id); });
                break;
            case publication_kind_t::connection_created:
                publishConnectionReplacementNotification(
                    [&] { Q_EMIT connectionCreated(event.connection_id); });
                break;
            }
        }

        for (auto const &[node_id, old_widget] : prepared_widgets) {
            Q_UNUSED(node_id);
            delete_widget(old_widget);
        }
    } catch (...) {
        return false;
    }

    return true;
}

bool DynamicPortsModel::load_from_json(QByteArray const &serialized)
{
    if (serialized.size() > s_max_serialized_bytes) {
        return false;
    }

    QJsonParseError parse_error{};
    QJsonDocument const document = QJsonDocument::fromJson(serialized, &parse_error);
    return parse_error.error == QJsonParseError::NoError && document.isObject()
           && load(document.object());
}

bool DynamicPortsModel::addPort(NodeId nodeId, PortType portType, PortIndex portIndex)
{
    auto const counts = _nodePortCounts.find(nodeId);
    if (counts == _nodePortCounts.end() || portType == PortType::None) {
        return false;
    }
    PortCount const current = portType == PortType::In ? counts->second.in : counts->second.out;
    if (portIndex > current || current == std::numeric_limits<PortCount>::max()
        || !port_count_allowed(nodeId, portType, current + 1)) {
        return false;
    }

    // STAGE 1.
    // Compute new addresses for the existing connections that are shifted and
    // placed after the new ones
    PortIndex first = portIndex;
    PortIndex last = first;
    portsAboutToBeInserted(nodeId, portType, first, last);

    // STAGE 2. Change the number of connections in your model
    if (portType == PortType::In)
        _nodePortCounts[nodeId].in++;
    else
        _nodePortCounts[nodeId].out++;

    // STAGE 3. Re-create previouly existed and now shifted connections
    portsInserted();

    Q_EMIT nodeUpdated(nodeId);
    return true;
}

void DynamicPortsModel::removePort(NodeId nodeId, PortType portType, PortIndex portIndex)
{
    auto const counts = _nodePortCounts.find(nodeId);
    if (counts == _nodePortCounts.end() || portType == PortType::None) {
        return;
    }
    PortCount const current = portType == PortType::In ? counts->second.in : counts->second.out;
    if (current == 0 || portIndex >= current) {
        return;
    }

    // STAGE 1.
    // Compute new addresses for the existing connections that are shifted upwards
    // instead of the deleted ports.
    PortIndex first = portIndex;
    PortIndex last = first;
    portsAboutToBeDeleted(nodeId, portType, first, last);

    // STAGE 2. Change the number of connections in your model
    if (portType == PortType::In)
        _nodePortCounts[nodeId].in--;
    else
        _nodePortCounts[nodeId].out--;

    portsDeleted();

    Q_EMIT nodeUpdated(nodeId);
}

NodeId DynamicPortsModel::newNodeId()
{
    if (_nextNodeId == InvalidNodeId) {
        throw std::overflow_error("Dynamic-ports node id space exhausted");
    }

    NodeId const node_id = _nextNodeId;
    _nextNodeId = node_id_after(node_id);
    return node_id;
}

bool DynamicPortsModel::port_count_allowed(NodeId node_id, PortType port_type, PortCount count) const
{
    if (port_type == PortType::None || count > s_max_serialized_ports_per_node) {
        return false;
    }

    std::size_t total_ports = 0;
    for (auto const &[existing_id, counts] : _nodePortCounts) {
        PortCount const in_count = existing_id == node_id && port_type == PortType::In ? count
                                                                                       : counts.in;
        PortCount const out_count = existing_id == node_id && port_type == PortType::Out
                                        ? count
                                        : counts.out;
        total_ports += static_cast<std::size_t>(in_count) + out_count;
        if (total_ports > s_max_serialized_ports) {
            return false;
        }
    }

    if (_nodePortCounts.find(node_id) == _nodePortCounts.end()) {
        total_ports += count;
    }
    return total_ports <= s_max_serialized_ports;
}

bool DynamicPortsModel::position_allowed(QPointF const &position) const
{
    return std::isfinite(position.x()) && std::isfinite(position.y());
}

void DynamicPortsModel::delete_widget(QPointer<PortAddRemoveWidget> const &node_widget) const
{
    if (node_widget) {
        delete node_widget.data();
    }
}
