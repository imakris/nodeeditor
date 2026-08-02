#pragma once

#include "AbstractGraphModel.hpp"
#include "ConnectionIdHash.hpp"

#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace QtNodes {

class ConnectionIdIndex
{
public:
    using ConnectionSet = std::unordered_set<ConnectionId>;
    using ConnectionsByPort = std::unordered_map<PortIndex, ConnectionSet>;

public:
    ConnectionSet const &connectivity() const noexcept { return _connectivity; }

    ConnectionSet const &allConnectionIds(NodeId const nodeId) const
    {
        auto it = _nodeConnections.find(nodeId);
        if (it == _nodeConnections.end()) {
            return emptyConnections();
        }

        return it->second;
    }

    ConnectionSet const &connections(NodeId nodeId, PortType portType, PortIndex portIndex) const
    {
        if (portType == PortType::None) {
            return emptyConnections();
        }

        auto const &connectionsByPort = (portType == PortType::In) ? _inConnectionsByPort
                                                                   : _outConnectionsByPort;
        auto nodeIt = connectionsByPort.find(nodeId);
        if (nodeIt == connectionsByPort.end()) {
            return emptyConnections();
        }

        auto portIt = nodeIt->second.find(portIndex);
        if (portIt == nodeIt->second.end()) {
            return emptyConnections();
        }

        return portIt->second;
    }

    bool contains(ConnectionId const connectionId) const noexcept
    {
        return _connectivity.find(connectionId) != _connectivity.end();
    }

    void add(ConnectionId const connectionId)
    {
        if (_connectivity.insert(connectionId).second) {
            indexConnection(connectionId);
        }
    }

    bool remove(ConnectionId const connectionId)
    {
        auto it = _connectivity.find(connectionId);
        if (it == _connectivity.end()) {
            return false;
        }

        _connectivity.erase(it);
        unindexConnection(connectionId);
        return true;
    }

    /**
     * Builds a complete replacement state without changing this index.
     *
     * Every removed id must exist. Every added id must be absent after the
     * removals. Duplicate ids in either argument are rejected. Allocation or
     * indexing failure returns no state and leaves this index unchanged.
     */
    [[nodiscard]] std::optional<ConnectionIdIndex> preparedReplacement(
        std::vector<ConnectionId> const &removedConnectionIds,
        std::vector<ConnectionId> const &addedConnectionIds) const noexcept
    {
        try {
            ConnectionSet uniqueRemoved;
            ConnectionSet uniqueAdded;

            for (ConnectionId const connectionId : removedConnectionIds) {
                if (!uniqueRemoved.insert(connectionId).second || !contains(connectionId)) {
                    return std::nullopt;
                }
            }

            for (ConnectionId const connectionId : addedConnectionIds) {
                if (!uniqueAdded.insert(connectionId).second) {
                    return std::nullopt;
                }
            }

            ConnectionIdIndex prepared(*this);
            for (ConnectionId const connectionId : removedConnectionIds) {
                if (!prepared.remove(connectionId)) {
                    return std::nullopt;
                }
            }

            for (ConnectionId const connectionId : addedConnectionIds) {
                if (prepared.contains(connectionId)) {
                    return std::nullopt;
                }
                prepared.add(connectionId);
            }

            return prepared;
        } catch (...) {
            return std::nullopt;
        }
    }

    /// Exchanges complete index states without allocating.
    void swap(ConnectionIdIndex &other) noexcept
    {
        using std::swap;
        swap(_connectivity, other._connectivity);
        swap(_nodeConnections, other._nodeConnections);
        swap(_inConnectionsByPort, other._inConnectionsByPort);
        swap(_outConnectionsByPort, other._outConnectionsByPort);
    }

private:
    static ConnectionSet const &emptyConnections() noexcept
    {
        static ConnectionSet const empty{};
        return empty;
    }

    void indexConnection(ConnectionId const connectionId)
    {
        _nodeConnections[connectionId.inNodeId].insert(connectionId);
        _nodeConnections[connectionId.outNodeId].insert(connectionId);
        _inConnectionsByPort[connectionId.inNodeId][connectionId.inPortIndex].insert(connectionId);
        _outConnectionsByPort[connectionId.outNodeId][connectionId.outPortIndex].insert(
            connectionId);
    }

    void unindexConnection(ConnectionId const connectionId)
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

        auto eraseFromPortMap = [&](std::unordered_map<NodeId, ConnectionsByPort> &connectionsByPort,
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

private:
    ConnectionSet _connectivity;
    std::unordered_map<NodeId, ConnectionSet> _nodeConnections;
    std::unordered_map<NodeId, ConnectionsByPort> _inConnectionsByPort;
    std::unordered_map<NodeId, ConnectionsByPort> _outConnectionsByPort;
};

/**
 * Replays a prepared index replacement by swapping complete states.
 *
 * The inactive state belongs to this transaction; the model keeps a single
 * active topology index. Publication is an explicit post-swap phase because
 * observers and data delivery may be fallible.
 */
template<typename Publisher>
class ConnectionIdIndexReplacementTransaction final : public ConnectionReplacementTransaction
{
public:
    ConnectionIdIndexReplacementTransaction(ConnectionIdIndex &activeIndex,
                                            ConnectionIdIndex preparedIndex,
                                            std::vector<ConnectionId> removedConnectionIds,
                                            std::vector<ConnectionId> addedConnectionIds,
                                            Publisher publisher)
        : _activeIndex(activeIndex)
        , _preparedIndex(std::move(preparedIndex))
        , _removedConnectionIds(std::move(removedConnectionIds))
        , _addedConnectionIds(std::move(addedConnectionIds))
        , _publisher(std::move(publisher))
    {}

    void undo() noexcept override { _activeIndex.swap(_preparedIndex); }

    void redo() noexcept override { _activeIndex.swap(_preparedIndex); }

    void publishUndo() override { _publisher(_addedConnectionIds, _removedConnectionIds); }

    void publishRedo() override { _publisher(_removedConnectionIds, _addedConnectionIds); }

private:
    ConnectionIdIndex &_activeIndex;
    ConnectionIdIndex _preparedIndex;
    std::vector<ConnectionId> _removedConnectionIds;
    std::vector<ConnectionId> _addedConnectionIds;
    Publisher _publisher;
};

} // namespace QtNodes
