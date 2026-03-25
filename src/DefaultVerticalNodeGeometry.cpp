#include "DefaultVerticalNodeGeometry.hpp"

#include "AbstractGraphModel.hpp"

#include <QPoint>
#include <QRect>
#include <QWidget>

namespace QtNodes {

DefaultVerticalNodeGeometry::DefaultVerticalNodeGeometry(AbstractGraphModel &graphModel)
    : DefaultNodeGeometryBase(graphModel)
{}

void DefaultVerticalNodeGeometry::recomputeSize(NodeId const nodeId) const
{
    unsigned int height = _portSpacing;

    if (auto w = _graphModel.nodeData<QWidget *>(nodeId, NodeRole::Widget)) {
        height = std::max(height, static_cast<unsigned int>(w->height()));
    }

    QRectF const capRect = captionRect(nodeId);

    height += capRect.height();

    height += _portSpacing;
    height += _portSpacing;

    PortCount nInPorts = _graphModel.nodeData<PortCount>(nodeId, NodeRole::InPortCount);
    PortCount nOutPorts = _graphModel.nodeData<PortCount>(nodeId, NodeRole::OutPortCount);

    // Adding double step (top and bottom) to reserve space for port captions.

    height += portCaptionsHeight(nodeId, PortType::In);
    height += portCaptionsHeight(nodeId, PortType::Out);

    unsigned int inPortWidth = maxPortsTextAdvance(nodeId, PortType::In);
    unsigned int outPortWidth = maxPortsTextAdvance(nodeId, PortType::Out);

    unsigned int totalInPortsWidth = nInPorts > 0
                                         ? inPortWidth * nInPorts + _portSpacing * (nInPorts - 1)
                                         : 0;

    unsigned int totalOutPortsWidth = nOutPorts > 0 ? outPortWidth * nOutPorts
                                                          + _portSpacing * (nOutPorts - 1)
                                                    : 0;

    unsigned int width = std::max(totalInPortsWidth, totalOutPortsWidth);

    if (auto w = _graphModel.nodeData<QWidget *>(nodeId, NodeRole::Widget)) {
        width = std::max(width, static_cast<unsigned int>(w->width()));
    }

    width = std::max(width, static_cast<unsigned int>(capRect.width()));

    width += _portSpacing;
    width += _portSpacing;

    QSize size(width, height);

    _graphModel.setNodeData(nodeId, NodeRole::Size, size);
}

QPointF DefaultVerticalNodeGeometry::portPosition(NodeId const nodeId,
                                                  PortType const portType,
                                                  PortIndex const portIndex) const
{
    QPointF result;

    QSize size = _graphModel.nodeData<QSize>(nodeId, NodeRole::Size);

    switch (portType) {
    case PortType::In: {
        unsigned int inPortWidth = maxPortsTextAdvance(nodeId, PortType::In) + _portSpacing;

        PortCount nInPorts = _graphModel.nodeData<PortCount>(nodeId, NodeRole::InPortCount);

        double x = (size.width() - (nInPorts - 1) * inPortWidth) / 2.0 + portIndex * inPortWidth;

        double y = 0.0;

        result = QPointF(x, y);

        break;
    }

    case PortType::Out: {
        unsigned int outPortWidth = maxPortsTextAdvance(nodeId, PortType::Out) + _portSpacing;
        PortCount nOutPorts = _graphModel.nodeData<PortCount>(nodeId, NodeRole::OutPortCount);

        double x = (size.width() - (nOutPorts - 1) * outPortWidth) / 2.0 + portIndex * outPortWidth;

        double y = size.height();

        result = QPointF(x, y);

        break;
    }

    default:
        break;
    }

    return result;
}

QPointF DefaultVerticalNodeGeometry::portTextPosition(NodeId const nodeId,
                                                      PortType const portType,
                                                      PortIndex const portIndex) const
{
    QPointF p = portPosition(nodeId, portType, portIndex);

    QRectF rect = portTextRect(nodeId, portType, portIndex);

    p.setX(p.x() - rect.width() / 2.0);

    QSize size = _graphModel.nodeData<QSize>(nodeId, NodeRole::Size);

    switch (portType) {
    case PortType::In:
        p.setY(5.0 + rect.height());
        break;

    case PortType::Out:
        p.setY(size.height() - 5.0);
        break;

    default:
        break;
    }

    return p;
}

QPointF DefaultVerticalNodeGeometry::captionPosition(NodeId const nodeId) const
{
    QSize size = _graphModel.nodeData<QSize>(nodeId, NodeRole::Size);

    unsigned int step = portCaptionsHeight(nodeId, PortType::In);
    step += _portSpacing;

    auto rect = captionRect(nodeId);

    return QPointF(0.5 * (size.width() - rect.width()), step + rect.height());
}

QPointF DefaultVerticalNodeGeometry::widgetPosition(NodeId const nodeId) const
{
    QSize size = _graphModel.nodeData<QSize>(nodeId, NodeRole::Size);

    unsigned int captionHeight = captionRect(nodeId).height();

    if (auto w = _graphModel.nodeData<QWidget *>(nodeId, NodeRole::Widget)) {
        // If the widget wants to use as much vertical space as possible,
        // place it immediately after the caption.
        if (w->sizePolicy().verticalPolicy() & QSizePolicy::ExpandFlag) {
            return QPointF(_portSpacing + maxPortsTextAdvance(nodeId, PortType::In), captionHeight);
        } else {
            return QPointF(_portSpacing + maxPortsTextAdvance(nodeId, PortType::In),
                           (captionHeight + size.height() - w->height()) / 2.0);
        }
    }
    return QPointF();
}

QRect DefaultVerticalNodeGeometry::resizeHandleRect(NodeId const nodeId) const
{
    QSize size = _graphModel.nodeData<QSize>(nodeId, NodeRole::Size);

    unsigned int rectSize = 7;

    return QRect(size.width() - rectSize, size.height() - rectSize, rectSize, rectSize);
}

unsigned int DefaultVerticalNodeGeometry::portCaptionsHeight(NodeId const nodeId,
                                                             PortType const portType) const
{
    PortCount const n = _graphModel.nodeData<PortCount>(nodeId, portCountRole(portType));
    for (PortIndex i = 0; i < n; ++i) {
        if (_graphModel.portData<bool>(nodeId, portType, i, PortRole::CaptionVisible)) {
            return _portSpacing;
        }
    }
    return 0;
}

} // namespace QtNodes
