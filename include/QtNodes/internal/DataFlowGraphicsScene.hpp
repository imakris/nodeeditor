#pragma once

#include "BasicGraphicsScene.hpp"
#include "DataFlowGraphModel.hpp"
#include "Export.hpp"

#include <QtCore/QString>

namespace QtNodes {

/**
 * @brief An advanced scene working with data-propagating graphs.
 *
 * The class represents a scene that existed in v2.x but built wit the
 * new model-view approach in mind.
 */
class NODE_EDITOR_PUBLIC DataFlowGraphicsScene : public BasicGraphicsScene
{
    Q_OBJECT
public:
    DataFlowGraphicsScene(DataFlowGraphModel &graphModel, QObject *parent = nullptr);
    ~DataFlowGraphicsScene() = default;

public:
    std::vector<NodeId> selectedNodes() const;
    QMenu *createSceneMenu(QPointF const scenePos) override;
    void updateConnectionGraphics(const std::unordered_set<ConnectionId> &connections, bool state);

    /// Writes the scene to @a fileName verbatim, without asking the user.
    bool saveToFile(QString const &fileName) const;

    /// Replaces the scene with the document in @a fileName, without asking the user.
    bool loadFromFile(QString const &fileName);

public Q_SLOTS:
    /// Asks the user for a destination, then delegates to saveToFile().
    bool save() const;

    /// Asks the user for a document, then delegates to loadFromFile().
    bool load();

Q_SIGNALS:
    void sceneLoaded();

private:
    DataFlowGraphModel &_graphModel;
};

} // namespace QtNodes
