#include "locateNode.hpp"


#include "NodeGraphicsObject.hpp"

#include <QtCore/QList>
#include <QtWidgets/QGraphicsScene>


namespace QtNodes {

NodeGraphicsObject *locateNodeAt(QPointF scenePoint,
                                 QGraphicsScene &scene,
                                 QTransform const &viewTransform)
{
    QList<QGraphicsItem *> const items = scene.items(scenePoint,
                                                     Qt::IntersectsItemShape,
                                                     Qt::DescendingOrder,
                                                     viewTransform);

    for (QGraphicsItem *item : items) {
        if (auto *node = qgraphicsitem_cast<NodeGraphicsObject *>(item)) {
            return node;
        }
    }

    return nullptr;
}

} // namespace QtNodes
