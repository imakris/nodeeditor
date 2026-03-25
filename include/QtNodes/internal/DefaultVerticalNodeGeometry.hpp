#pragma once

#include "DefaultNodeGeometryBase.hpp"

namespace QtNodes {

class AbstractGraphModel;

class NODE_EDITOR_PUBLIC DefaultVerticalNodeGeometry : public DefaultNodeGeometryBase
{
public:
    DefaultVerticalNodeGeometry(AbstractGraphModel &graphModel);

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

private:
    unsigned int portCaptionsHeight(NodeId const nodeId, PortType const portType) const;
};

} // namespace QtNodes
