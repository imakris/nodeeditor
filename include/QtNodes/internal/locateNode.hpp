#pragma once

#include "Export.hpp"

#include <QtCore/QPointF>
#include <QtGui/QTransform>

class QGraphicsScene;

namespace QtNodes {

class NodeGraphicsObject;

NODE_EDITOR_PUBLIC NodeGraphicsObject *locateNodeAt(QPointF scenePoint,
                                                    QGraphicsScene &scene,
                                                    QTransform const &viewTransform);

} // namespace QtNodes
