#pragma once

#include "DefaultNodeGeometryBase.hpp"

namespace QtNodes {

class AbstractGraphModel;

class NODE_EDITOR_PUBLIC DefaultHorizontalNodeGeometry : public DefaultNodeGeometryBase
{
public:
    DefaultHorizontalNodeGeometry(AbstractGraphModel &graphModel);

public:
    void recomputeSize(NodeId const nodeId) const override;

    QPointF portPosition(NodeId const nodeId,
                         PortType const portType,
                         PortIndex const index) const override;

    QPointF portTextPosition(NodeId const nodeId,
                             PortType const portType,
                             PortIndex const PortIndex) const override;
    QPointF captionPosition(NodeId const nodeId) const override;

    QPointF widgetPosition(NodeId const nodeId) const override;

    QRect resizeHandleRect(NodeId const nodeId) const override;
};

} // namespace QtNodes
