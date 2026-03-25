#pragma once

#include <QIcon>
#include <QtGui/QPainter>

#include "AbstractNodePainter.hpp"
#include "Definitions.hpp"
#include "NodeStyle.hpp"

namespace QtNodes {

class GraphicsView;
class NodeGraphicsObject;

/// @ Lightweight class incapsulating paint code.
///
/// NOTE: Some draw helpers accept GraphicsView* for zoom-aware text rendering.
/// That paint-time coupling is intentional for now, but custom painters need to
/// account for it.
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
