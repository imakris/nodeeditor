#include "DefaultHorizontalNodeGeometry.hpp"

#include "AbstractGraphModel.hpp"

#include <QPoint>
#include <QRect>
#include <QWidget>

namespace QtNodes {

DefaultHorizontalNodeGeometry::DefaultHorizontalNodeGeometry(AbstractGraphModel &graphModel)
    : DefaultNodeGeometryBase(graphModel)
{}

void DefaultHorizontalNodeGeometry::recomputeSize(NodeId const nodeId) const
{
    unsigned int height = maxVerticalPortsExtent(nodeId);

    if (auto w = _graphModel.nodeData<QWidget *>(nodeId, NodeRole::Widget)) {
        height = std::max(height, static_cast<unsigned int>(w->height()));
    }

    QRectF const capRect = captionRect(nodeId);

    height += capRect.height();

    height += _portSpacing; // space above caption
    height += _portSpacing; // space below caption

    QVariant var = _graphModel.nodeData(nodeId, NodeRole::ProcessingStatus);
    auto processingStatusValue = var.value<int>();

    if (processingStatusValue != 0)
        height += 20;

    unsigned int inPortWidth = maxPortsTextAdvance(nodeId, PortType::In);
    unsigned int outPortWidth = maxPortsTextAdvance(nodeId, PortType::Out);

    unsigned int width = inPortWidth + outPortWidth + 4 * _portSpacing;

    if (auto w = _graphModel.nodeData<QWidget *>(nodeId, NodeRole::Widget)) {
        width += w->width();
    }

    width = std::max(width, static_cast<unsigned int>(capRect.width()) + 2 * _portSpacing);

    QSize size(width, height);

    _graphModel.setNodeData(nodeId, NodeRole::Size, size);
}

QPointF DefaultHorizontalNodeGeometry::portPosition(NodeId const nodeId,
                                                    PortType const portType,
                                                    PortIndex const portIndex) const
{
    unsigned int const step = _portSize + _portSpacing;

    QPointF result;

    double totalHeight = 0.0;

    totalHeight += captionRect(nodeId).height();
    totalHeight += _portSpacing;

    totalHeight += step * portIndex;
    totalHeight += step / 2.0;

    QSize size = _graphModel.nodeData<QSize>(nodeId, NodeRole::Size);

    switch (portType) {
    case PortType::In: {
        double x = 0.0;

        result = QPointF(x, totalHeight);
        break;
    }

    case PortType::Out: {
        double x = size.width();

        result = QPointF(x, totalHeight);
        break;
    }

    default:
        break;
    }

    return result;
}

QPointF DefaultHorizontalNodeGeometry::portTextPosition(NodeId const nodeId,
                                                        PortType const portType,
                                                        PortIndex const portIndex) const
{
    QPointF p = portPosition(nodeId, portType, portIndex);

    QRectF rect = portTextRect(nodeId, portType, portIndex);

    p.setY(p.y() + rect.height() / 4.0);

    QSize size = _graphModel.nodeData<QSize>(nodeId, NodeRole::Size);

    switch (portType) {
    case PortType::In:
        p.setX(_portSpacing);
        break;

    case PortType::Out:
        p.setX(size.width() - _portSpacing - rect.width());
        break;

    default:
        break;
    }

    return p;
}

QPointF DefaultHorizontalNodeGeometry::captionPosition(NodeId const nodeId) const
{
    QSize size = _graphModel.nodeData<QSize>(nodeId, NodeRole::Size);
    return QPointF(0.5 * (size.width() - captionRect(nodeId).width()),
                   0.5 * _portSpacing + captionRect(nodeId).height());
}

QPointF DefaultHorizontalNodeGeometry::widgetPosition(NodeId const nodeId) const
{
    QSize size = _graphModel.nodeData<QSize>(nodeId, NodeRole::Size);

    unsigned int captionHeight = captionRect(nodeId).height();

    if (auto w = _graphModel.nodeData<QWidget *>(nodeId, NodeRole::Widget)) {
        // If the widget wants to use as much vertical space as possible,
        // place it immediately after the caption.
        if (w->sizePolicy().verticalPolicy() & QSizePolicy::ExpandFlag) {
            return QPointF(2.0 * _portSpacing + maxPortsTextAdvance(nodeId, PortType::In),
                           _portSpacing + captionHeight);
        } else {
            return QPointF(2.0 * _portSpacing + maxPortsTextAdvance(nodeId, PortType::In),
                           (captionHeight + size.height() - w->height()) / 2.0);
        }
    }
    return QPointF();
}

QRect DefaultHorizontalNodeGeometry::resizeHandleRect(NodeId const nodeId) const
{
    QSize size = _graphModel.nodeData<QSize>(nodeId, NodeRole::Size);

    unsigned int rectSize = 7;

    return QRect(size.width() - _portSpacing, size.height() - _portSpacing, rectSize, rectSize);
}

unsigned int DefaultHorizontalNodeGeometry::maxVerticalPortsExtent(NodeId const nodeId) const
{
    return maxPortsExtent(nodeId);
}

} // namespace QtNodes
