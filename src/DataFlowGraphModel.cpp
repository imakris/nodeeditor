#include "DataFlowGraphModel.hpp"

#include "ConnectionIdUtils.hpp"
#include "Definitions.hpp"

#include <QDebug>
#include <QJsonArray>
#include <QJsonValue>

#include <stack>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace QtNodes {

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

void reportConnectionReplacementDataFailure(std::exception const *error)
{
    if (error) {
        qWarning() << "Connection replacement data publication failed:" << error->what();
    } else {
        qWarning() << "Connection replacement data publication failed with an unknown exception";
    }
}

} // namespace

DataFlowGraphModel::DataFlowGraphModel(std::shared_ptr<NodeDelegateModelRegistry> registry)
    : _registry(std::move(registry))
    , _nextNodeId{0}
{}

AbstractGraphModel::NodeIdSet const &DataFlowGraphModel::allNodeIds() const
{
    return _nodeIds;
}

AbstractGraphModel::ConnectionIdSet const &DataFlowGraphModel::allConnectionIds(
    NodeId const nodeId) const
{
    return _connectionIndex.allConnectionIds(nodeId);
}

AbstractGraphModel::ConnectionIdSet const &DataFlowGraphModel::connections(NodeId nodeId,
                                                                           PortType portType,
                                                                           PortIndex portIndex) const
{
    return _connectionIndex.connections(nodeId, portType, portIndex);
}

bool DataFlowGraphModel::connectionExists(ConnectionId const connectionId) const
{
    return _connectionIndex.contains(connectionId);
}

NodeId DataFlowGraphModel::addNode(QString const nodeType)
{
    std::unique_ptr<NodeDelegateModel> model = _registry->create(nodeType);

    if (model) {
        NodeId newId = newNodeId();

        connectDelegateModel(model.get(), newId);

        _nodeIds.insert(newId);
        _models[newId] = std::move(model);
        _loopReachabilityCache.clear();

        Q_EMIT nodeCreated(newId);

        return newId;
    }

    return InvalidNodeId;
}

bool DataFlowGraphModel::connectionPossible(
    ConnectionId const connectionId, std::vector<ConnectionId> const &replacedConnectionIds) const
{
    auto isReplaced = [&](ConnectionId const &candidate) {
        return std::find(replacedConnectionIds.begin(), replacedConnectionIds.end(), candidate)
               != replacedConnectionIds.end();
    };

    // Check if nodes exist
    if (!nodeExists(connectionId.outNodeId) || !nodeExists(connectionId.inNodeId)) {
        return false;
    }

    if (connectionExists(connectionId) && !isReplaced(connectionId)) {
        return false;
    }

    // Check port bounds, i.e. that we do not connect non-existing port numbers
    auto checkPortBounds = [&](PortType const portType) {
        NodeId const nodeId = connectionNodeId(portType, connectionId);
        std::size_t const portCount = nodeData(nodeId, portCountRole(portType)).toUInt();

        return connectionPortIndex(portType, connectionId) < portCount;
    };

    auto getDataType = [&](PortType const portType) {
        return portData(connectionNodeId(portType, connectionId),
                        portType,
                        connectionPortIndex(portType, connectionId),
                        PortRole::DataType)
            .value<NodeDataType>();
    };

    auto portVacant = [&](PortType const portType) {
        NodeId const nodeId = connectionNodeId(portType, connectionId);
        PortIndex const portIndex = connectionPortIndex(portType, connectionId);
        auto const &connected = connections(nodeId, portType, portIndex);

        auto policy = portData(nodeId, portType, portIndex, PortRole::ConnectionPolicy)
                          .value<ConnectionPolicy>();

        if (policy == ConnectionPolicy::Many) {
            return true;
        }

        return std::all_of(connected.begin(), connected.end(), isReplaced);
    };

    bool const portsValid = checkPortBounds(PortType::Out) && checkPortBounds(PortType::In);
    if (!portsValid) {
        return false;
    }

    bool const basicChecks = getDataType(PortType::Out).id == getDataType(PortType::In).id
                             && portVacant(PortType::Out) && portVacant(PortType::In);

    // In data-flow mode (this class) it's important to forbid graph loops.
    // We perform depth-first graph traversal starting from the "Input" port of
    // the given connection. We should never encounter the starting "Out" node.

    auto hasLoops = [this, &connectionId, &isReplaced]() -> bool {
        std::stack<NodeId> filo;
        std::unordered_set<NodeId> visited;
        filo.push(connectionId.inNodeId);

        while (!filo.empty()) {
            auto id = filo.top();
            filo.pop();

            if (!visited.insert(id).second) {
                continue;
            }

            if (id == connectionId.outNodeId) { // LOOP!
                return true;
            }

            // Add out-connections to continue interations
            std::size_t const nOutPorts = nodeData(id, NodeRole::OutPortCount).toUInt();

            for (PortIndex index = 0; index < nOutPorts; ++index) {
                auto const &outConnectionIds = connections(id, PortType::Out, index);

                for (auto cid : outConnectionIds) {
                    if (isReplaced(cid)) {
                        continue;
                    }
                    filo.push(cid.inNodeId);
                }
            }
        }

        return false;
    };

    // The DFS depends only on the (out, in) node pair and the current topology, so
    // memoize it: during a connection drag the painter asks connectionPossible() for
    // every in-port of the hovered node on every repaint, all with the same node pair.
    auto hasLoopsCached = [&]() -> bool {
        if (!replacedConnectionIds.empty()) {
            return hasLoops();
        }

        std::uint64_t const key = (static_cast<std::uint64_t>(connectionId.outNodeId) << 32)
                                  | static_cast<std::uint64_t>(connectionId.inNodeId);

        auto it = _loopReachabilityCache.find(key);
        if (it != _loopReachabilityCache.end()) {
            return it->second;
        }

        bool const result = hasLoops();
        _loopReachabilityCache.emplace(key, result);
        return result;
    };

    return basicChecks && (loopsEnabled() || !hasLoopsCached());
}

std::unique_ptr<ConnectionReplacementTransaction> DataFlowGraphModel::prepareConnectionReplacement(
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
            _loopReachabilityCache.clear();

            // The complete topology is committed before any observer callback.
            for (ConnectionId const connectionId : removedIds) {
                publishConnectionReplacementNotification(
                    [&] { sendConnectionDeletion(connectionId); });
                try {
                    propagateEmptyDataTo(connectionId.inNodeId, connectionId.inPortIndex);
                } catch (std::exception const &error) {
                    reportConnectionReplacementDataFailure(&error);
                } catch (...) {
                    reportConnectionReplacementDataFailure(nullptr);
                }
            }

            for (ConnectionId const connectionId : addedIds) {
                publishConnectionReplacementNotification(
                    [&] { sendConnectionCreation(connectionId); });

                try {
                    QVariant const currentData = portData(connectionId.outNodeId,
                                                          PortType::Out,
                                                          connectionId.outPortIndex,
                                                          PortRole::Data);
                    setPortData(connectionId.inNodeId,
                                PortType::In,
                                connectionId.inPortIndex,
                                currentData,
                                PortRole::Data);
                } catch (std::exception const &error) {
                    reportConnectionReplacementDataFailure(&error);
                } catch (...) {
                    reportConnectionReplacementDataFailure(nullptr);
                }
            }
        };

        using Transaction = ConnectionIdIndexReplacementTransaction<decltype(publisher)>;
        return std::make_unique<Transaction>(_connectionIndex,
                                             std::move(*preparedIndex),
                                             removedConnectionIds,
                                             addedConnectionIds,
                                             std::move(publisher));
    } catch (...) {
        return {};
    }
}

void DataFlowGraphModel::addConnection(ConnectionId const connectionId)
{
    if (connectionExists(connectionId) || !connectionPossible(connectionId, {})) {
        return;
    }

    _connectionIndex.add(connectionId);
    _loopReachabilityCache.clear();

    sendConnectionCreation(connectionId);

    QVariant const portDataToPropagate = portData(connectionId.outNodeId,
                                                  PortType::Out,
                                                  connectionId.outPortIndex,
                                                  PortRole::Data);

    setPortData(connectionId.inNodeId,
                PortType::In,
                connectionId.inPortIndex,
                portDataToPropagate,
                PortRole::Data);
}

void DataFlowGraphModel::connectDelegateModel(NodeDelegateModel *model, NodeId nodeId)
{
    connect(model, &NodeDelegateModel::dataUpdated, [nodeId, this](PortIndex const portIndex) {
        onOutPortDataUpdated(nodeId, portIndex);
    });

    connect(model,
            &NodeDelegateModel::portsAboutToBeDeleted,
            this,
            [nodeId, this](PortType const portType, PortIndex const first, PortIndex const last) {
                portsAboutToBeDeleted(nodeId, portType, first, last);
            });

    connect(model, &NodeDelegateModel::portsDeleted, this, &DataFlowGraphModel::portsDeleted);

    connect(model,
            &NodeDelegateModel::portsAboutToBeInserted,
            this,
            [nodeId, this](PortType const portType, PortIndex const first, PortIndex const last) {
                portsAboutToBeInserted(nodeId, portType, first, last);
            });

    connect(model, &NodeDelegateModel::portsInserted, this, &DataFlowGraphModel::portsInserted);

    connect(model, &NodeDelegateModel::requestNodeUpdate, this, [nodeId, this]() {
        Q_EMIT nodeUpdated(nodeId);
    });
}

void DataFlowGraphModel::sendConnectionCreation(ConnectionId const connectionId)
{
    Q_EMIT connectionCreated(connectionId);

    auto iti = _models.find(connectionId.inNodeId);
    auto ito = _models.find(connectionId.outNodeId);
    if (iti != _models.end() && ito != _models.end()) {
        auto &modeli = iti->second;
        auto &modelo = ito->second;
        modeli->inputConnectionCreated(connectionId);
        modelo->outputConnectionCreated(connectionId);
    }
}

void DataFlowGraphModel::sendConnectionDeletion(ConnectionId const connectionId)
{
    Q_EMIT connectionDeleted(connectionId);

    auto iti = _models.find(connectionId.inNodeId);
    auto ito = _models.find(connectionId.outNodeId);
    if (iti != _models.end() && ito != _models.end()) {
        auto &modeli = iti->second;
        auto &modelo = ito->second;
        modeli->inputConnectionDeleted(connectionId);
        modelo->outputConnectionDeleted(connectionId);
    }
}

bool DataFlowGraphModel::nodeExists(NodeId const nodeId) const
{
    return (_models.find(nodeId) != _models.end());
}

QVariant DataFlowGraphModel::nodeData(NodeId nodeId, NodeRole role) const
{
    QVariant result;

    auto it = _models.find(nodeId);
    if (it == _models.end())
        return result;

    auto &model = it->second;

    switch (role) {
    case NodeRole::Type:
        result = model->name();
        break;

    case NodeRole::Position:
        if (auto geometryIt = _nodeGeometryData.find(nodeId);
            geometryIt != _nodeGeometryData.end()) {
            result = geometryIt->second.pos;
        } else {
            result = QPointF{};
        }
        break;

    case NodeRole::Size:
        if (auto geometryIt = _nodeGeometryData.find(nodeId);
            geometryIt != _nodeGeometryData.end()) {
            result = geometryIt->second.size;
        } else {
            result = QSize{};
        }
        break;

    case NodeRole::CaptionVisible:
        result = model->captionVisible();
        break;

    case NodeRole::Caption:
        result = model->caption();
        break;

    case NodeRole::Style: {
        auto style = model->nodeStyle();
        result = style.toJson().toVariantMap();
    } break;

    case NodeRole::InternalData: {
        QJsonObject nodeJson;

        nodeJson["internal-data"] = model->save();

        result = nodeJson.toVariantMap();
        break;
    }

    case NodeRole::InPortCount:
        result = model->nPorts(PortType::In);
        break;

    case NodeRole::OutPortCount:
        result = model->nPorts(PortType::Out);
        break;

    case NodeRole::Widget: {
        auto *w = model->embeddedWidget();
        result = QVariant::fromValue(w);
    } break;

    case NodeRole::ValidationState: {
        auto validationState = model->validationState();
        result = QVariant::fromValue(validationState);
    } break;

    case NodeRole::ProcessingStatus: {
        auto processingStatus = model->processingStatus();
        result = QVariant::fromValue(processingStatus);
    } break;
    }

    return result;
}

NodeFlags DataFlowGraphModel::nodeFlags(NodeId nodeId) const
{
    auto it = _models.find(nodeId);

    if (it != _models.end() && it->second->resizable())
        return NodeFlag::Resizable;

    return NodeFlag::NoFlags;
}

bool DataFlowGraphModel::setNodeData(NodeId nodeId, NodeRole role, QVariant value)
{
    if (!nodeExists(nodeId)) {
        return false;
    }

    bool result = false;

    switch (role) {
    case NodeRole::Type:
        break;
    case NodeRole::Position: {
        _nodeGeometryData[nodeId].pos = value.value<QPointF>();

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
        break;

    case NodeRole::OutPortCount:
        break;

    case NodeRole::Widget:
        break;

    case NodeRole::ValidationState: {
        if (value.canConvert<NodeValidationState>()) {
            auto state = value.value<NodeValidationState>();
            if (auto node = delegateModel<NodeDelegateModel>(nodeId); node != nullptr) {
                node->setValidationState(state);
                result = true;
            }
        }
        Q_EMIT nodeUpdated(nodeId);
    } break;

    case NodeRole::ProcessingStatus: {
        if (value.canConvert<QtNodes::NodeProcessingStatus>()) {
            auto status = value.value<QtNodes::NodeProcessingStatus>();
            if (auto node = delegateModel<NodeDelegateModel>(nodeId); node != nullptr) {
                node->setNodeProcessingStatus(status);
                result = true;
            }
        }
        Q_EMIT nodeUpdated(nodeId);
    } break;
    }

    return result;
}

QVariant DataFlowGraphModel::portData(NodeId nodeId,
                                      PortType portType,
                                      PortIndex portIndex,
                                      PortRole role) const
{
    QVariant result;

    auto it = _models.find(nodeId);
    if (it == _models.end())
        return result;

    if (portType == PortType::None) {
        return result;
    }

    PortCount const portCount = nodeData(nodeId, portCountRole(portType)).toUInt();
    if (portIndex >= portCount) {
        return result;
    }

    auto &model = it->second;

    switch (role) {
    case PortRole::Data:
        if (portType == PortType::Out) {
            result = QVariant::fromValue(model->outData(portIndex));
        }
        break;

    case PortRole::DataType:
        result = QVariant::fromValue(model->dataType(portType, portIndex));
        break;

    case PortRole::ConnectionPolicy:
        result = QVariant::fromValue(model->portConnectionPolicy(portType, portIndex));
        break;

    case PortRole::CaptionVisible:
        result = model->portCaptionVisible(portType, portIndex);
        break;

    case PortRole::Caption:
        result = model->portCaption(portType, portIndex);

        break;
    }

    return result;
}

bool DataFlowGraphModel::setPortData(
    NodeId nodeId, PortType portType, PortIndex portIndex, QVariant const &value, PortRole role)
{
    auto it = _models.find(nodeId);
    if (it == _models.end())
        return false;

    if (portType == PortType::None) {
        return false;
    }

    PortCount const portCount = nodeData(nodeId, portCountRole(portType)).toUInt();
    if (portIndex >= portCount) {
        return false;
    }

    auto &model = it->second;

    switch (role) {
    case PortRole::Data:
        if (portType == PortType::In) {
            if (model->frozen())
                return false;

            model->setInData(value.value<std::shared_ptr<NodeData>>(), portIndex);

            // Triggers repainting on the scene.
            Q_EMIT inPortDataWasSet(nodeId, portType, portIndex);
            return true;
        }
        break;

    default:
        break;
    }

    return false;
}

bool DataFlowGraphModel::deleteConnection(ConnectionId const connectionId)
{
    bool const disconnected = _connectionIndex.remove(connectionId);

    if (disconnected) {
        _loopReachabilityCache.clear();

        sendConnectionDeletion(connectionId);

        propagateEmptyDataTo(connectionNodeId(PortType::In, connectionId),
                             connectionPortIndex(PortType::In, connectionId));
    }

    return disconnected;
}

bool DataFlowGraphModel::deleteNode(NodeId const nodeId)
{
    if (!nodeExists(nodeId))
        return false;

    // Delete connections to this node first.
    std::vector<ConnectionId> connectionIds;
    auto const &attachedConnections = allConnectionIds(nodeId);
    connectionIds.reserve(attachedConnections.size());
    for (auto const &cId : attachedConnections) {
        connectionIds.push_back(cId);
    }

    for (auto const &cId : connectionIds) {
        deleteConnection(cId);
    }

    _nodeIds.erase(nodeId);
    _nodeGeometryData.erase(nodeId);
    _models.erase(nodeId);
    _loopReachabilityCache.clear();

    Q_EMIT nodeDeleted(nodeId);

    return true;
}

QJsonObject DataFlowGraphModel::saveNode(NodeId const nodeId) const
{
    QJsonObject nodeJson;

    nodeJson["id"] = static_cast<qint64>(nodeId);

    nodeJson["internal-data"] = _models.at(nodeId)->save();

    {
        QPointF const pos = nodeData(nodeId, NodeRole::Position).value<QPointF>();

        QJsonObject posJson;
        posJson["x"] = pos.x();
        posJson["y"] = pos.y();
        nodeJson["position"] = posJson;
    }

    return nodeJson;
}

QJsonObject DataFlowGraphModel::save() const
{
    QJsonObject sceneJson;

    QJsonArray nodesJsonArray;
    for (auto const nodeId : allNodeIds()) {
        nodesJsonArray.append(saveNode(nodeId));
    }
    sceneJson["nodes"] = nodesJsonArray;

    QJsonArray connJsonArray;
    for (auto const &cid : _connectionIndex.connectivity()) {
        connJsonArray.append(toJson(cid));
    }
    sceneJson["connections"] = connJsonArray;

    return sceneJson;
}

void DataFlowGraphModel::loadNode(QJsonObject const &nodeJson)
{
    // Possibility of the id clash when reading it from json and not generating a
    // new value.
    // 1. When restoring a scene from a file.
    // Conflict is not possible because the scene must be cleared by the time of
    // loading.
    // 2. When undoing the deletion command.  Conflict is not possible
    // because all the new ids were created past the removed nodes.
    NodeId restoredNodeId = detail::read_node_id_or_throw(nodeJson["id"],
                                                          "Invalid node id in serialized node");

    if (_models.find(restoredNodeId) != _models.end()) {
        throw std::logic_error("Node identifier collision in serialized node");
    }

    quint64 const nextNodeIdCandidate = static_cast<quint64>(restoredNodeId) + 1ull;
    _nextNodeId = std::max(_nextNodeId, static_cast<NodeId>(nextNodeIdCandidate));

    QJsonObject internalDataJson;
    if (!detail::read_required_object(nodeJson, "internal-data", internalDataJson)) {
        throw std::logic_error("Missing internal-data object in serialized node");
    }

    QString delegateModelName;
    if (!detail::read_required_string(internalDataJson, "model-name", delegateModelName)
        || delegateModelName.isEmpty()) {
        throw std::logic_error("Missing model-name in serialized node");
    }

    QPointF const pos
        = detail::read_required_point_or_throw(nodeJson,
                                               "position",
                                               "Invalid node position in serialized node");

    std::unique_ptr<NodeDelegateModel> model = _registry->create(delegateModelName);

    if (model) {
        connectDelegateModel(model.get(), restoredNodeId);

        _nodeIds.insert(restoredNodeId);
        _models[restoredNodeId] = std::move(model);
        _loopReachabilityCache.clear();

        Q_EMIT nodeCreated(restoredNodeId);

        setNodeData(restoredNodeId, NodeRole::Position, pos);

        try {
            _models[restoredNodeId]->load(internalDataJson);
        } catch (...) {
            deleteNode(restoredNodeId);
            throw;
        }
    } else {
        throw std::logic_error(std::string("No registered model with name ")
                               + delegateModelName.toLocal8Bit().data());
    }
}

void DataFlowGraphModel::load(QJsonObject const &jsonDocument)
{
    // load() appends into the model and assumes a clean slate; callers must
    // clear first. Enforce that precondition explicitly rather than relying on
    // an incidental node-id collision later in the load.
    if (!_nodeIds.empty()) {
        throw std::logic_error("DataFlowGraphModel::load requires an empty model; clear it first");
    }

    QJsonArray nodesJsonArray;
    if (!detail::read_required_array(jsonDocument, "nodes", nodesJsonArray)) {
        throw std::logic_error("Serialized graph is missing nodes array");
    }

    QJsonArray connectionJsonArray;
    if (!detail::read_required_array(jsonDocument, "connections", connectionJsonArray)) {
        throw std::logic_error("Serialized graph is missing connections array");
    }

    for (QJsonValue const &nodeJson : nodesJsonArray) {
        if (!nodeJson.isObject()) {
            throw std::logic_error("Serialized graph contains invalid node entry");
        }
    }

    std::vector<ConnectionId> parsedConnections;
    parsedConnections.reserve(connectionJsonArray.size());

    for (QJsonValue const &connection : connectionJsonArray) {
        if (!connection.isObject()) {
            throw std::logic_error("Serialized graph contains invalid connection entry");
        }

        ConnectionId connId;
        if (!tryFromJson(connection.toObject(), connId)) {
            throw std::logic_error("Serialized graph contains invalid connection id");
        }

        parsedConnections.push_back(connId);
    }

    std::vector<NodeId> loadedNodeIds;
    loadedNodeIds.reserve(nodesJsonArray.size());
    std::vector<ConnectionId> loadedConnections;
    loadedConnections.reserve(parsedConnections.size());

    // Snapshot the monotonic id counter so a failed load is fully transactional:
    // loadNode() advances _nextNodeId past restored ids, so rolling back the nodes
    // must also roll back the counter.
    NodeId const savedNextNodeId = _nextNodeId;

    try {
        for (QJsonValueRef nodeJson : nodesJsonArray) {
            QJsonObject const nodeObj = nodeJson.toObject();
            NodeId nodeId = InvalidNodeId;
            detail::read_node_id(nodeObj["id"], nodeId);
            loadNode(nodeObj);
            loadedNodeIds.push_back(nodeId);
        }

        for (ConnectionId const connId : parsedConnections) {
            if (!connectionPossible(connId, {})) {
                throw std::logic_error("Serialized graph contains invalid connection");
            }

            addConnection(connId);
            loadedConnections.push_back(connId);
        }
    } catch (...) {
        for (auto it = loadedConnections.rbegin(); it != loadedConnections.rend(); ++it) {
            deleteConnection(*it);
        }

        for (auto it = loadedNodeIds.rbegin(); it != loadedNodeIds.rend(); ++it) {
            if (nodeExists(*it)) {
                deleteNode(*it);
            }
        }

        _nextNodeId = savedNextNodeId;

        throw;
    }
}

void DataFlowGraphModel::onOutPortDataUpdated(NodeId const nodeId, PortIndex const portIndex)
{
    std::unordered_set<ConnectionId> const &connected = connections(nodeId,
                                                                    PortType::Out,
                                                                    portIndex);

    QVariant const portDataToPropagate = portData(nodeId, PortType::Out, portIndex, PortRole::Data);

    for (auto const &cn : connected) {
        setPortData(cn.inNodeId, PortType::In, cn.inPortIndex, portDataToPropagate, PortRole::Data);
    }
}

void DataFlowGraphModel::propagateEmptyDataTo(NodeId const nodeId, PortIndex const portIndex)
{
    QVariant emptyData{};

    setPortData(nodeId, PortType::In, portIndex, emptyData, PortRole::Data);
}

} // namespace QtNodes
