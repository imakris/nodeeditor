#include "NodeConnectionInteraction.hpp"

#include "AbstractNodeGeometry.hpp"
#include "BasicGraphicsScene.hpp"
#include "ConnectionGraphicsObject.hpp"
#include "ConnectionIdUtils.hpp"
#include "NodeGraphicsObject.hpp"
#include "UndoCommands.hpp"

#include <QtCore/QDebug>

#include <QUndoStack>

#include <algorithm>
#include <tuple>

namespace QtNodes {

NodeConnectionInteraction::NodeConnectionInteraction(NodeGraphicsObject &ngo,
                                                     ConnectionGraphicsObject const &cgo,
                                                     BasicGraphicsScene &scene)
    : _ngo(ngo)
    , _cgo(cgo)
    , _scene(scene)
{}

// This is the chneck from the perspective of the ConnectionGraphicsObject
bool NodeConnectionInteraction::canConnect(PortIndex *portIndex) const
{
    // 1. Connection requires a port.
    PortType const requiredPort = _cgo.connectionState().requiredPort();
    if (requiredPort == PortType::None) {
        return false;
    }

    // 2. Connection loose end is above the node port.
    QPointF const connectionPoint = _cgo.sceneTransform().map(_cgo.endPoint(requiredPort));
    *portIndex = nodePortIndexUnderScenePoint(requiredPort, connectionPoint);
    if (*portIndex == InvalidPortIndex) {
        return false;
    }

    // 3. Model permits connection.
    ConnectionId connectionId = makeCompleteConnectionId(_cgo.connectionId(), // incomplete
                                                         _ngo.nodeId(),       // missing node id
                                                         *portIndex);         // missing port index

    return connectionPossible(connectionId);
}

bool NodeConnectionInteraction::connectionPossible(ConnectionId const &connectionId) const
{
    AbstractGraphModel &model = _scene.graphModel();
    std::vector<ConnectionId> const replaced = replacedConnectionIds();

    for (ConnectionId const &replacedConnectionId : replaced) {
        if (!model.detachPossible(replacedConnectionId)) {
            return false;
        }
    }

    return model.connectionPossible(connectionId, replaced);
}

bool NodeConnectionInteraction::tryConnect() const
{
    // 1. Check conditions from 'canConnect'.

    PortIndex targetPortIndex = InvalidPortIndex;
    if (!canConnect(&targetPortIndex)) {
        return false;
    }

    // 2. Create new connection.

    ConnectionId incompleteConnectionId = _cgo.connectionId();

    ConnectionId newConnectionId = makeCompleteConnectionId(incompleteConnectionId,
                                                            _ngo.nodeId(),
                                                            targetPortIndex);

    std::vector<ConnectionId> const replaced = replacedConnectionIds();

    _scene.resetDraftConnection();

    if (replaced.empty()) {
        _scene.undoStack().push(new ConnectCommand(&_scene, newConnectionId));
    } else {
        _scene.undoStack().push(new ReplaceConnectionCommand(&_scene, newConnectionId, replaced));
    }

    return true;
}

bool NodeConnectionInteraction::disconnect(PortType portToDisconnect) const
{
    ConnectionId connectionId = _cgo.connectionId();

    _scene.undoStack().push(new DisconnectCommand(&_scene, connectionId));

    AbstractNodeGeometry &geometry = _scene.nodeGeometry();

    QPointF scenePos = geometry.portScenePosition(_ngo.nodeId(),
                                                  portToDisconnect,
                                                  connectionPortIndex(portToDisconnect, connectionId),
                                                  _ngo.sceneTransform());

    // Converted to "draft" connection with the new incomplete id.
    ConnectionId incompleteConnectionId = makeIncompleteConnectionId(connectionId, portToDisconnect);

    // Grabs the mouse
    auto const &draftConnection = 
      _scene.makeDraftConnection(incompleteConnectionId);

    QPointF const looseEndPos = draftConnection->mapFromScene(scenePos);
    draftConnection->setEndPoint(portToDisconnect, looseEndPos);

     //Repaint connection points.
    NodeId connectedNodeId = connectionNodeId(oppositePort(portToDisconnect), connectionId);
    if (auto *connectedNode = _scene.nodeGraphicsObject(connectedNodeId)) {
        connectedNode->update();
    }

    NodeId disconnectedNodeId = connectionNodeId(portToDisconnect, connectionId);
    if (auto *disconnectedNode = _scene.nodeGraphicsObject(disconnectedNodeId)) {
        disconnectedNode->update();
    }

    return true;
}

std::vector<ConnectionId> NodeConnectionInteraction::replacedConnectionIds() const
{
    ConnectionId const incompleteConnectionId = _cgo.connectionId();
    if (_cgo.connectionState().requiredPort() != PortType::In) {
        return {};
    }

    AbstractGraphModel const &model = _scene.graphModel();
    auto const policy = model
                            .portData(incompleteConnectionId.outNodeId,
                                      PortType::Out,
                                      incompleteConnectionId.outPortIndex,
                                      PortRole::ConnectionPolicy)
                            .value<ConnectionPolicy>();
    if (policy != ConnectionPolicy::One) {
        return {};
    }

    auto const &connected = model.connections(incompleteConnectionId.outNodeId,
                                              PortType::Out,
                                              incompleteConnectionId.outPortIndex);
    std::vector<ConnectionId> result(connected.begin(), connected.end());
    std::sort(result.begin(), result.end(), [](ConnectionId const &a, ConnectionId const &b) {
        return std::tie(a.outNodeId, a.outPortIndex, a.inNodeId, a.inPortIndex)
               < std::tie(b.outNodeId, b.outPortIndex, b.inNodeId, b.inPortIndex);
    });
    return result;
}

PortIndex NodeConnectionInteraction::nodePortIndexUnderScenePoint(PortType portType,
                                                                  QPointF const &scenePoint) const
{
    AbstractNodeGeometry &geometry = _scene.nodeGeometry();

    QTransform sceneTransform = _ngo.sceneTransform();

    QPointF nodePoint = sceneTransform.inverted().map(scenePoint);

    return geometry.checkPortHit(_ngo.nodeId(), portType, nodePoint);
}

} // namespace QtNodes
