#include "DefaultHorizontalNodeGeometry.hpp"

#include "AbstractGraphModel.hpp"
#include "NodeRenderingUtils.hpp"

#include <QPoint>
#include <QRect>
#include <QWidget>

#include <optional>

namespace QtNodes {

namespace {

constexpr unsigned int k_title_icon_width_overhead = 48u;

}

DefaultHorizontalNodeGeometry::DefaultHorizontalNodeGeometry(AbstractGraphModel &graphModel)
    : DefaultNodeGeometryBase(graphModel)
{}

void DefaultHorizontalNodeGeometry::recomputeSize(NodeId const nodeId) const
{
    unsigned int height = maxPortsExtent(nodeId);

    QWidget *const widget = widgetOf(nodeId);
    if (widget) {
        height = std::max(height, static_cast<unsigned int>(widget->height()));
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

    if (widget) {
        width += widget->width();
    }

    width = std::max(width, static_cast<unsigned int>(capRect.width()) + 2 * _portSpacing);

    std::optional<NodeStyle> fallback_style;
    NodeStyle const &style = node_rendering::resolved_node_style(_graphModel, nodeId, fallback_style);
    bool const caption_visible = _graphModel.nodeData<bool>(nodeId, NodeRole::CaptionVisible);
    if (caption_visible && !style.titleIcon.isNull()) {
        width = std::max(width, static_cast<unsigned int>(capRect.width()) + k_title_icon_width_overhead);
    }

    _graphModel.setNodeData(nodeId, NodeRole::Size, QSize(width, height));
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

    QSize const nodeSize = size(nodeId);

    switch (portType) {
    case PortType::In: {
        double x = 0.0;

        result = QPointF(x, totalHeight);
        break;
    }

    case PortType::Out: {
        double x = nodeSize.width();

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

    QSize const nodeSize = size(nodeId);

    switch (portType) {
    case PortType::In:
        p.setX(_portSpacing);
        break;

    case PortType::Out:
        p.setX(nodeSize.width() - _portSpacing - rect.width());
        break;

    default:
        break;
    }

    return p;
}

QPointF DefaultHorizontalNodeGeometry::captionPosition(NodeId const nodeId) const
{
    QSize const nodeSize = size(nodeId);
    return QPointF(0.5 * (nodeSize.width() - captionRect(nodeId).width()),
                   0.5 * _portSpacing + captionRect(nodeId).height());
}

QPointF DefaultHorizontalNodeGeometry::widgetPosition(NodeId const nodeId) const
{
    QWidget *const widget = widgetOf(nodeId);
    if (!widget) {
        return QPointF();
    }

    unsigned int const captionHeight = captionRect(nodeId).height();
    double const xPos = 2.0 * _portSpacing + maxPortsTextAdvance(nodeId, PortType::In);

    // If the widget wants to use as much vertical space as possible,
    // place it immediately after the caption.
    if (widget->sizePolicy().verticalPolicy() & QSizePolicy::ExpandFlag) {
        return QPointF(xPos, _portSpacing + captionHeight);
    }

    QSize const nodeSize = size(nodeId);
    return QPointF(xPos, (captionHeight + nodeSize.height() - widget->height()) / 2.0);
}

QRect DefaultHorizontalNodeGeometry::resizeHandleRect(NodeId const nodeId) const
{
    QSize const nodeSize = size(nodeId);

    unsigned int rectSize = 7;

    return QRect(nodeSize.width() - _portSpacing,
                 nodeSize.height() - _portSpacing,
                 rectSize,
                 rectSize);
}

} // namespace QtNodes
