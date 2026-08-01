#pragma once

#include "AbstractGraphModel.hpp"
#include "Definitions.hpp"
#include "Export.hpp"

#include <QUndoCommand>
#include <QtCore/QJsonObject>
#include <QtCore/QPointF>
#include <QtCore/QString>

#include <memory>
#include <unordered_set>
#include <vector>

namespace QtNodes {

class BasicGraphicsScene;

class NODE_EDITOR_PUBLIC CreateCommand : public QUndoCommand
{
public:
    CreateCommand(BasicGraphicsScene *scene, QString const name, QPointF const &mouseScenePos);

    void undo() override;
    void redo() override;

private:
    BasicGraphicsScene *_scene;
    QString _name;
    QPointF _mouseScenePos;
    NodeId _nodeId = InvalidNodeId;
    QJsonObject _sceneJson;
};

/**
 * Selected scene objects are serialized and then removed from the scene.
 * The deleted elements could be restored in `undo`.
 */
class NODE_EDITOR_PUBLIC DeleteCommand : public QUndoCommand
{
public:
    DeleteCommand(BasicGraphicsScene *scene);

    void undo() override;
    void redo() override;

private:
    BasicGraphicsScene *_scene;
    QJsonObject _sceneJson;
};

class NODE_EDITOR_PUBLIC CopyCommand : public QUndoCommand
{
public:
    CopyCommand(BasicGraphicsScene *scene);
};

class NODE_EDITOR_PUBLIC PasteCommand : public QUndoCommand
{
public:
    PasteCommand(BasicGraphicsScene *scene, QPointF const &mouseScenePos);

    void undo() override;
    void redo() override;

private:
    QJsonObject takeSceneJsonFromClipboard();
    QJsonObject makeNewNodeIdsInScene(QJsonObject const &sceneJson);

private:
    BasicGraphicsScene *_scene;
    QPointF _mouseScenePos;
    QJsonObject _newSceneJson;
};

class NODE_EDITOR_PUBLIC DisconnectCommand : public QUndoCommand
{
public:
    DisconnectCommand(BasicGraphicsScene *scene, ConnectionId const);

    void undo() override;
    void redo() override;

private:
    BasicGraphicsScene *_scene;

    ConnectionId _connId;
};

class NODE_EDITOR_PUBLIC ConnectCommand : public QUndoCommand
{
public:
    ConnectCommand(BasicGraphicsScene *scene, ConnectionId const);

    void undo() override;
    void redo() override;

private:
    BasicGraphicsScene *_scene;

    ConnectionId _connId;
};

/** Replaces one exact connection set with a candidate as a single history entry. */
class NODE_EDITOR_PUBLIC ReplaceConnectionCommand : public QUndoCommand
{
public:
    /// Fully prepares the model-owned transaction before history admission.
    ReplaceConnectionCommand(BasicGraphicsScene *scene,
                             ConnectionId const candidateConnectionId,
                             std::vector<ConnectionId> replacedConnectionIds);

    /// Exchanges the committed state with the exact prepared prior state.
    void undo() noexcept override;

    /// Exchanges the prior state with the exact prepared committed state.
    void redo() noexcept override;

private:
    std::unique_ptr<ConnectionReplacementTransaction> _transaction;
};

class NODE_EDITOR_PUBLIC MoveNodeCommand : public QUndoCommand
{
public:
    MoveNodeCommand(BasicGraphicsScene *scene, QPointF const &diff);

    void undo() override;
    void redo() override;

    /**
   * A command ID is used in command compression. It must be an integer unique to
   * this command's class, or -1 if the command doesn't support compression.
   */
    int id() const override;

    /**
   * Several sequential movements could be merged into one command.
   */
    bool mergeWith(QUndoCommand const *c) override;

private:
    BasicGraphicsScene *_scene;
    std::unordered_set<NodeId> _selectedNodes;
    QPointF _diff;
};

} // namespace QtNodes
