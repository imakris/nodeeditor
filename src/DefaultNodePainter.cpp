#include "DefaultNodePainter.hpp"

#include "AbstractGraphModel.hpp"
#include "AbstractNodeGeometry.hpp"
#include "BasicGraphicsScene.hpp"
#include "ConnectionGraphicsObject.hpp"
#include "ConnectionIdUtils.hpp"
#include "DataFlowGraphModel.hpp"
#include "GraphicsView.hpp"
#include "NodeRenderingUtils.hpp"
#include "NodeShadowAtlas.hpp"
#include "NodeDelegateModel.hpp"
#include "NodeGraphicsObject.hpp"
#include "NodeState.hpp"
#include "StyleCollection.hpp"

#include <QtCore/QHash>
#include <QtGui/QImage>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace QtNodes {

namespace {

GraphicsView *graphics_view(NodeGraphicsObject &ngo)
{
    if (auto *view = ngo.currentGraphicsView()) {
        return view;
    }

    if (!ngo.scene()) {
        return nullptr;
    }

    QList<QGraphicsView *> const views = ngo.scene()->views();
    for (QGraphicsView *view : views) {
        if (auto *graphicsView = qobject_cast<GraphicsView *>(view)) {
            return graphicsView;
        }
    }

    return nullptr;
}

bool should_draw_text_as_path(GraphicsView *view)
{
    if (!view) {
        return false;
    }

    switch (view->textRenderingPolicy()) {
    case GraphicsView::TextRenderingPolicy::QtText:
        return false;
    case GraphicsView::TextRenderingPolicy::PathWhenZooming:
        return view->isZoomAnimating();
    case GraphicsView::TextRenderingPolicy::PathAlways:
        return true;
    }

    return false;
}

void configure_text_painter(QPainter *painter, GraphicsView *view)
{
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    if (should_draw_text_as_path(view)) {
        return;
    }

    if (!view || !view->isZoomAnimating()) {
        return;
    }

    QFont font = painter->font();
    font.setHintingPreference(QFont::PreferNoHinting);
    painter->setFont(font);
}

// Paths are cached at the origin and the painter is translated to the
// draw position.  The cache key combines QFont::key() (which encodes
// family, size, weight, style, hinting, etc.) with the text string.
// For typical node scenes the cache holds ~15 entries and never evicts.
QHash<QString, QPainterPath> s_text_path_cache;
std::mutex s_text_path_cache_mutex;

struct ColorDprKey
{
    QRgb color;
    int dpr_micro;

    bool operator==(ColorDprKey const &o) const
    {
        return color == o.color && dpr_micro == o.dpr_micro;
    }
};

struct ColorDprKeyHash
{
    std::size_t operator()(ColorDprKey const &k) const
    {
        return std::hash<uint64_t>()(
            (static_cast<uint64_t>(k.color) << 32) | static_cast<uint32_t>(k.dpr_micro));
    }
};

std::unordered_map<ColorDprKey, QImage, ColorDprKeyHash> s_validation_icon_cache;
std::mutex s_validation_icon_cache_mutex;

QImage validation_icon(QIcon const &icon, QColor const &color, qreal dpr)
{
    std::lock_guard<std::mutex> lock(s_validation_icon_cache_mutex);

    ColorDprKey key{color.rgba(), static_cast<int>(dpr * 1000000.0)};
    auto it = s_validation_icon_cache.find(key);
    if (it != s_validation_icon_cache.end()) {
        return it->second;
    }

    QImage image = node_rendering::render_icon_image(icon, QSize(16, 16), dpr);
    if (image.isNull()) {
        return image;
    }

    QPainter imgPainter(&image);
    imgPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    imgPainter.fillRect(QRect(QPoint(0, 0), QSize(16, 16)), color);
    imgPainter.end();

    if (s_validation_icon_cache.size() >= 32) {
        s_validation_icon_cache.erase(s_validation_icon_cache.begin());
    }

    return s_validation_icon_cache.emplace(key, std::move(image)).first->second;
}

void draw_text(
    QPainter *painter,
    GraphicsView *view,
    QPointF const &position,
    QString const &text,
    QColor const &color,
    QFont const &font)
{
    if (should_draw_text_as_path(view)) {
        QString const key = font.key() + text;
        QPainterPath path;

        {
            std::lock_guard<std::mutex> lock(s_text_path_cache_mutex);

            auto it = s_text_path_cache.constFind(key);
            if (it == s_text_path_cache.constEnd()) {
                if (s_text_path_cache.size() >= 500) {
                    // Arbitrary eviction keeps insertion cost predictable without LRU bookkeeping.
                    s_text_path_cache.erase(s_text_path_cache.begin());
                }

                QPainterPath new_path;
                new_path.addText(QPointF(0, 0), font, text);
                it = s_text_path_cache.insert(key, std::move(new_path));
            }

            path = *it;
        }

        painter->setPen(Qt::NoPen);
        painter->translate(position);
        painter->fillPath(path, color);
        painter->translate(-position);
        return;
    }

    painter->setFont(font);
    painter->setPen(color);
    painter->drawText(position, text);
}

} // namespace

void DefaultNodePainter::paint(QPainter *painter, NodeGraphicsObject &ngo) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    GraphicsView *view = graphics_view(ngo);

    std::optional<NodeStyle> fallback_style;
    NodeStyle const &style = node_rendering::resolved_node_style(model, nodeId, fallback_style);

    drawNodeRect(painter, ngo, style);

    drawConnectionPoints(painter, ngo, style);

    drawFilledConnectionPoints(painter, ngo, style);

    drawNodeCaption(painter, ngo, style, view);

    drawEntryLabels(painter, ngo, style, view);

    drawProcessingIndicator(painter, ngo);

    drawResizeRect(painter, ngo);

    drawValidationIcon(painter, ngo, style);
}

void DefaultNodePainter::drawNodeRect(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &nodeStyle) const
{
    AbstractGraphModel &model = ngo.graphModel();

    NodeId const nodeId = ngo.nodeId();

    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    QSize size = geometry.size(nodeId);

    QVariant var = model.nodeData(nodeId, NodeRole::ValidationState);
    bool invalid = false;

    QColor color = ngo.isSelected() ? nodeStyle.SelectedBoundaryColor
                                    : nodeStyle.NormalBoundaryColor;

    if (var.canConvert<NodeValidationState>()) {
        auto state = var.value<NodeValidationState>();
        switch (state.state()) {
        case NodeValidationState::State::Error: {
            invalid = true;
            color = nodeStyle.ErrorColor;
        } break;
        case NodeValidationState::State::Warning: {
            invalid = true;
            color = nodeStyle.WarningColor;
        } break;
        default:
            break;
        }
    }

    QRectF boundary(0, 0, size.width(), size.height());

    double const radius = 3.0;

    // 9-slice shadow: a precomputed blurred atlas is sliced into 9 tiles
    // and stretched to fit the node.  One atlas per (color, DPR), size-
    // independent.  Much faster than QGraphicsDropShadowEffect and smoother
    // than stacked translucent rounded rects.
    if (nodeStyle.ShadowEnabled) {
        node_rendering::draw_nine_slice_shadow(painter, nodeStyle.ShadowColor, boundary);
    }

    if (ngo.nodeState().hovered()) {
        painter->setPen(QPen(color, nodeStyle.HoveredPenWidth));
    }
    else {
        painter->setPen(QPen(color, nodeStyle.PenWidth));
    }

    if (invalid) {
        painter->setBrush(color);
    }
    else {
        QLinearGradient gradient(QPointF(0.0, 0.0), QPointF(2.0, size.height()));
        gradient.setColorAt(0.0, nodeStyle.GradientColor0);
        gradient.setColorAt(0.10, nodeStyle.GradientColor1);
        gradient.setColorAt(0.90, nodeStyle.GradientColor2);
        gradient.setColorAt(1.0, nodeStyle.GradientColor3);
        painter->setBrush(gradient);
    }

    painter->drawRoundedRect(boundary, radius, radius);
}

namespace {

template<typename Body>
void for_each_port(AbstractGraphModel &model,
                   AbstractNodeGeometry &geometry,
                   NodeId const nodeId,
                   Body &&body)
{
    for (PortType portType : {PortType::Out, PortType::In}) {
        size_t const n = model.nodeData(nodeId, portCountRole(portType)).toUInt();
        for (PortIndex portIndex = 0; portIndex < n; ++portIndex) {
            body(portType, portIndex, geometry.portPosition(nodeId, portType, portIndex));
        }
    }
}

} // namespace

void DefaultNodePainter::drawConnectionPoints(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &nodeStyle) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    auto const &connectionStyle = StyleCollection::connectionStyle();
    double const reducedDiameter = nodeStyle.ConnectionPointDiameter * 0.6;

    for_each_port(model, geometry, nodeId,
                  [&](PortType portType, PortIndex portIndex, QPointF const &p) {
        auto const &dataType = model.portData(nodeId, portType, portIndex, PortRole::DataType)
                                   .value<NodeDataType>();

        double r = 1.0;

        if (auto const *cgo = ngo.nodeState().connectionForReaction()) {
            PortType requiredPort = cgo->connectionState().requiredPort();

            if (requiredPort == portType) {
                ConnectionId const possibleConnectionId
                    = makeCompleteConnectionId(cgo->connectionId(), nodeId, portIndex);

                bool const possible = model.connectionPossible(possibleConnectionId);

                QPointF cp = cgo->sceneTransform().map(cgo->endPoint(requiredPort));
                cp = ngo.sceneTransform().inverted().map(cp);

                QPointF const diff = cp - p;
                double const dist = std::sqrt(QPointF::dotProduct(diff, diff));

                double const thres = possible ? 40.0 : 80.0;
                r = (dist < thres) ? (possible ? (2.0 - dist / thres) : (dist / thres)) : 1.0;
            }
        }

        if (connectionStyle.useDataDefinedColors()) {
            painter->setBrush(connectionStyle.normalColor(dataType.id));
        } else {
            painter->setBrush(nodeStyle.ConnectionPointColor);
        }

        painter->drawEllipse(p, reducedDiameter * r, reducedDiameter * r);
    });

    if (ngo.nodeState().connectionForReaction()) {
        ngo.nodeState().resetConnectionForReaction();
    }
}

void DefaultNodePainter::drawFilledConnectionPoints(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &nodeStyle) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    auto const &connectionStyle = StyleCollection::connectionStyle();
    double const radius = nodeStyle.ConnectionPointDiameter * 0.4;

    for_each_port(model, geometry, nodeId,
                  [&](PortType portType, PortIndex portIndex, QPointF const &p) {
        auto const &connected = model.connections(nodeId, portType, portIndex);
        if (connected.empty()) {
            return;
        }

        QColor color;
        if (connectionStyle.useDataDefinedColors()) {
            auto const &dataType = model.portData(nodeId, portType, portIndex, PortRole::DataType)
                                       .value<NodeDataType>();
            color = connectionStyle.normalColor(dataType.id);
        } else {
            color = nodeStyle.FilledConnectionPointColor;
        }

        painter->setPen(color);
        painter->setBrush(color);
        painter->drawEllipse(p, radius, radius);
    });
}

void DefaultNodePainter::drawNodeCaption(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &nodeStyle, GraphicsView *view) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    if (!model.nodeData(nodeId, NodeRole::CaptionVisible).toBool())
        return;

    QString const name = model.nodeData(nodeId, NodeRole::Caption).toString();

    QFont f = painter->font();
    f.setBold(true);
    if (!should_draw_text_as_path(view) && view && view->isZoomAnimating()) {
        f.setHintingPreference(QFont::PreferNoHinting);
    }
    else {
        f.setHintingPreference(QFont::PreferDefaultHinting);
    }

    QPointF position = geometry.captionPosition(nodeId);

    painter->setRenderHint(QPainter::TextAntialiasing, true);
    draw_text(painter, view, position, name, nodeStyle.FontColor, f);

    f.setBold(false);
    painter->setFont(f);
}

void DefaultNodePainter::drawEntryLabels(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &nodeStyle, GraphicsView *view) const
{
    configure_text_painter(painter, view);

    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    for (PortType portType : {PortType::Out, PortType::In}) {
        unsigned int n = model.nodeData<unsigned int>(nodeId, portCountRole(portType));

        for (PortIndex portIndex = 0; portIndex < n; ++portIndex) {
            auto const &connected = model.connections(nodeId, portType, portIndex);

            QPointF p = geometry.portTextPosition(nodeId, portType, portIndex);

            if (connected.empty())
                painter->setPen(nodeStyle.FontColorFaded);
            else
                painter->setPen(nodeStyle.FontColor);

            QString s;

            if (model.portData<bool>(nodeId, portType, portIndex, PortRole::CaptionVisible)) {
                s = model.portData<QString>(nodeId, portType, portIndex, PortRole::Caption);
            } else {
                auto portData = model.portData(nodeId, portType, portIndex, PortRole::DataType);

                s = portData.value<NodeDataType>().name;
            }

            QColor const textColor = connected.empty() ? nodeStyle.FontColorFaded
                                                       : nodeStyle.FontColor;
            draw_text(painter, view, p, s, textColor, painter->font());
        }
    }
}

void DefaultNodePainter::drawResizeRect(QPainter *painter, NodeGraphicsObject &ngo) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    if (model.nodeFlags(nodeId) & NodeFlag::Resizable) {
        painter->setBrush(Qt::gray);

        painter->drawEllipse(geometry.resizeHandleRect(nodeId));
    }
}

void DefaultNodePainter::drawProcessingIndicator(QPainter *painter, NodeGraphicsObject &ngo) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();

    auto *dfModel = dynamic_cast<DataFlowGraphModel *>(&model);
    if (!dfModel)
        return;

    auto *delegate = dfModel->delegateModel<NodeDelegateModel>(nodeId);
    if (!delegate)
        return;

    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    QSize size = geometry.size(nodeId);

    qreal const dpr = painter->device()
        ? painter->device()->devicePixelRatioF()
        : 1.0;
    QImage const image = delegate->processingStatusImage(dpr);
    if (image.isNull())
        return;

    ProcessingIconStyle const iconStyle = delegate->processingIconStyle();

    qreal iconSize = iconStyle._size;
    qreal margin = iconStyle._margin;

    // Determine position, avoiding conflict with resize handle
    ProcessingIconPos pos = iconStyle._pos;
    bool isResizable = model.nodeFlags(nodeId) & NodeFlag::Resizable;
    if (isResizable && pos == ProcessingIconPos::BottomRight) {
        pos = ProcessingIconPos::BottomLeft;
    }

    qreal x = margin;
    if (pos == ProcessingIconPos::BottomRight) {
        x = size.width() - iconSize - margin;
    }

    QRectF const targetRect(x, size.height() - iconSize - margin, iconSize, iconSize);
    qreal const image_dpr = image.devicePixelRatio();
    QRectF const sourceRect(QPointF(0, 0),
                            QSizeF(image.width() / image_dpr,
                                   image.height() / image_dpr));
    painter->drawImage(targetRect, image, sourceRect);
}

void DefaultNodePainter::drawValidationIcon(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &nodeStyle) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    QVariant var = model.nodeData(nodeId, NodeRole::ValidationState);
    if (!var.canConvert<NodeValidationState>())
        return;

    auto state = var.value<NodeValidationState>();
    if (state.isValid())
        return;

    QSize size = geometry.size(nodeId);

    QSize const iconSize(16, 16);

    QColor color = (state.state() == NodeValidationState::State::Error) ? nodeStyle.ErrorColor
                                                                        : nodeStyle.WarningColor;
    qreal const dpr = painter->device()
        ? painter->device()->devicePixelRatioF()
        : 1.0;
    QImage const image = validation_icon(_toolTipIcon, color, dpr);
    if (image.isNull()) {
        return;
    }

    QPointF center(size.width(), 0.0);
    center += QPointF(iconSize.width() / 2.0, -iconSize.height() / 2.0);

    QRectF const targetRect(center.x() - iconSize.width() / 2.0,
                            center.y() - iconSize.height() / 2.0,
                            iconSize.width(),
                            iconSize.height());
    painter->drawImage(targetRect, image, QRectF(QPointF(0, 0), QSizeF(iconSize)));
}

} // namespace QtNodes
