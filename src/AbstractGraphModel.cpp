#include "AbstractGraphModel.hpp"

#include <QtNodes/ConnectionIdUtils>

#include <vector>

namespace QtNodes {

void AbstractGraphModel::portsAboutToBeDeleted(NodeId const nodeId,
                                               PortType const portType,
                                               PortIndex const first,
                                               PortIndex const last)
{
    _shiftedByDynamicPortsConnections.clear();

    unsigned int portCount = nodeData(nodeId, portCountRole(portType)).toUInt();

    if (portCount == 0 || first >= portCount)
        return;

    if (last < first)
        return;

    auto clampedLast = std::min(last, portCount - 1);

    for (PortIndex portIndex = first; portIndex <= clampedLast; ++portIndex) {
        std::vector<ConnectionId> conns;
        auto const &attachedConnections = connections(nodeId, portType, portIndex);
        conns.reserve(attachedConnections.size());
        for (auto const &connectionId : attachedConnections) {
            conns.push_back(connectionId);
        }

        for (auto const connectionId : conns) {
            deleteConnection(connectionId);
        }
    }

    std::size_t const nRemovedPorts = clampedLast - first + 1;

    for (PortIndex portIndex = clampedLast + 1; portIndex < portCount; ++portIndex) {
        std::vector<ConnectionId> conns;
        auto const &attachedConnections = connections(nodeId, portType, portIndex);
        conns.reserve(attachedConnections.size());
        for (auto const &connectionId : attachedConnections) {
            conns.push_back(connectionId);
        }

        for (auto const connectionId : conns) {
            // Erases the information about the port on one side;
            auto c = makeIncompleteConnectionId(connectionId, portType);

            c = makeCompleteConnectionId(c,
                                         nodeId,
                                         portIndex - static_cast<QtNodes::PortIndex>(nRemovedPorts));

            _shiftedByDynamicPortsConnections.push_back(c);

            deleteConnection(connectionId);
        }
    }
}

void AbstractGraphModel::portsDeleted()
{
    for (auto const connectionId : _shiftedByDynamicPortsConnections) {
        addConnection(connectionId);
    }

    _shiftedByDynamicPortsConnections.clear();
}

void AbstractGraphModel::portsAboutToBeInserted(NodeId const nodeId,
                                                PortType const portType,
                                                PortIndex const first,
                                                PortIndex const last)
{
    _shiftedByDynamicPortsConnections.clear();

    unsigned int portCount = nodeData(nodeId, portCountRole(portType)).toUInt();

    if (first > portCount)
        return;

    if (last < first)
        return;

    std::size_t const nNewPorts = last - first + 1;

    for (PortIndex portIndex = first; portIndex < portCount; ++portIndex) {
        std::vector<ConnectionId> conns;
        auto const &attachedConnections = connections(nodeId, portType, portIndex);
        conns.reserve(attachedConnections.size());
        for (auto const &connectionId : attachedConnections) {
            conns.push_back(connectionId);
        }

        for (auto const connectionId : conns) {
            // Erases the information about the port on one side;
            auto c = makeIncompleteConnectionId(connectionId, portType);

            c = makeCompleteConnectionId(c,
                                         nodeId,
                                         portIndex + static_cast<QtNodes::PortIndex>(nNewPorts));

            _shiftedByDynamicPortsConnections.push_back(c);

            deleteConnection(connectionId);
        }
    }
}

void AbstractGraphModel::portsInserted()
{
    for (auto const connectionId : _shiftedByDynamicPortsConnections) {
        addConnection(connectionId);
    }

    _shiftedByDynamicPortsConnections.clear();
}

} // namespace QtNodes
