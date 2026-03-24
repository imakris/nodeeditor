#pragma once

#include <QIcon>
#include <QtGui/QPainter>

#include "AbstractNodePainter.hpp"
#include "Definitions.hpp"
#include "NodeStyle.hpp"

namespace QtNodes {

class BasicGraphicsScene;
class GraphicsView;
class GraphModel;
class NodeGeometry;
class NodeGraphicsObject;
class NodeState;

/// @ Lightweight class incapsulating paint code.
class NODE_EDITOR_PUBLIC DefaultNodePainter : public AbstractNodePainter
{
public:
    void paint(QPainter *painter, NodeGraphicsObject &ngo) const override;

    void drawNodeRect(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &style) const;

    void drawConnectionPoints(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &style) const;

    void drawFilledConnectionPoints(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &style) const;

    void drawNodeCaption(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &style, GraphicsView *view) const;

    void drawEntryLabels(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &style, GraphicsView *view) const;

    void drawResizeRect(QPainter *painter, NodeGraphicsObject &ngo) const;

    void drawProcessingIndicator(QPainter *painter, NodeGraphicsObject &ngo) const;

    void drawValidationIcon(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &style) const;

private:
    QIcon _toolTipIcon{":/info-tooltip.svg"};
};
} // namespace QtNodes
