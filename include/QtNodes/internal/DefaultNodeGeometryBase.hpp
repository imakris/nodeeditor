#pragma once

#include "AbstractNodeGeometry.hpp"

#include <QtGui/QFontMetrics>

namespace QtNodes {

class AbstractGraphModel;

/**
 * Shared base for DefaultHorizontalNodeGeometry and DefaultVerticalNodeGeometry.
 *
 * Contains common member variables and methods that are identical in both
 * orientations: boundingRect, size, captionRect, portTextRect,
 * maxPortsTextAdvance, and maxPortsExtent.
 */
class NODE_EDITOR_PUBLIC DefaultNodeGeometryBase : public AbstractNodeGeometry
{
public:
    DefaultNodeGeometryBase(AbstractGraphModel &graphModel);

    QRectF boundingRect(NodeId const nodeId) const override;

    QSize size(NodeId const nodeId) const override;

    QRectF captionRect(NodeId const nodeId) const override;

protected:
    QRectF portTextRect(NodeId const nodeId,
                        PortType const portType,
                        PortIndex const portIndex) const;

    unsigned int maxPortsExtent(NodeId const nodeId) const;

    unsigned int maxPortsTextAdvance(NodeId const nodeId, PortType const portType) const;

protected:
    mutable unsigned int _portSize;
    unsigned int _portSpasing;
    mutable QFontMetrics _fontMetrics;
    mutable QFontMetrics _boldFontMetrics;
};

} // namespace QtNodes
