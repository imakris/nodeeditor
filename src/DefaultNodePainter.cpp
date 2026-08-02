#include "DefaultNodePainter.hpp"

#include "AbstractGraphModel.hpp"
#include "AbstractNodeGeometry.hpp"
#include "BasicGraphicsScene.hpp"
#include "ConnectionGraphicsObject.hpp"
#include "ConnectionIdUtils.hpp"
#include "DataFlowGraphModel.hpp"
#include "GraphicsView.hpp"
#include "NodeRenderingUtils.hpp"
#include "NodeDelegateModel.hpp"
#include "NodeGraphicsObject.hpp"
#include "NodeConnectionInteraction.hpp"
#include "NodeState.hpp"
#include "StyleCollection.hpp"
#include "node_shadow_atlas.hpp"

#include <QtCore/QHash>
#include <QtGui/QImage>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace QtNodes {

namespace {

constexpr qreal k_node_accent_width = 5.0;
constexpr qreal k_node_title_icon_frame_size = 22.0;
constexpr qreal k_node_title_icon_frame_radius = 3.0;
constexpr qreal k_node_title_icon_size = 14.0;
constexpr qreal k_node_title_icon_margin = 10.0;

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
//
// All painter caches in this file are reached only from QGraphicsItem::paint,
// which Qt always invokes on the GUI thread, so they need no synchronization.
QHash<QString, QPainterPath> s_text_path_cache;

struct Color_dpr_key
{
    QRgb color;
    int dpr_micro;

    bool operator==(Color_dpr_key const& other) const
    {
        return color == other.color && dpr_micro == other.dpr_micro;
    }
};

struct Color_dpr_key_hash
{
    std::size_t operator()(Color_dpr_key const& key) const
    {
        return std::hash<uint64_t>()(
            (static_cast<uint64_t>(key.color) << 32) | static_cast<uint32_t>(key.dpr_micro));
    }
};

std::unordered_map<Color_dpr_key, QImage, Color_dpr_key_hash> s_validation_icon_cache;

QImage validation_icon(QIcon const &icon, QColor const &color, qreal dpr)
{
    Color_dpr_key key{color.rgba(), static_cast<int>(dpr * 1000000.0)};
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

    if (nodeStyle.AccentColor.isValid() && nodeStyle.AccentColor.alpha() > 0) {
        QPainterPath boundary_path;
        boundary_path.addRoundedRect(boundary, radius, radius);

        painter->save();
        painter->setClipPath(boundary_path);
        painter->setPen(Qt::NoPen);
        painter->setBrush(nodeStyle.AccentColor);
        painter->drawRect(QRectF(0.0, 0.0, k_node_accent_width, size.height()));
        painter->restore();

        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(boundary, radius, radius);
    }
}

namespace {

template<typename Body>
void for_each_port(AbstractGraphModel& model, NodeId const nodeId, Body&& body)
{
    for (PortType portType : {PortType::Out, PortType::In}) {
        size_t const n = model.nodeData(nodeId, portCountRole(portType)).toUInt();
        for (PortIndex portIndex = 0; portIndex < n; ++portIndex) {
            body(portType, portIndex);
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

    for_each_port(model, nodeId, [&](PortType portType, PortIndex portIndex) {
        QPointF const p = geometry.portPosition(nodeId, portType, portIndex);

        auto const& dataType = model.portData(nodeId, portType, portIndex, PortRole::DataType)
                                   .value<NodeDataType>();

        double r = 1.0;

        if (auto const *cgo = ngo.nodeState().connectionForReaction()) {
            PortType requiredPort = cgo->connectionState().requiredPort();

            if (requiredPort == portType) {
                ConnectionId const possibleConnectionId
                    = makeCompleteConnectionId(cgo->connectionId(), nodeId, portIndex);

                NodeConnectionInteraction interaction(ngo, *cgo, *ngo.nodeScene());
                bool const possible = interaction.connectionPossible(possibleConnectionId);

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

    for_each_port(model, nodeId, [&](PortType portType, PortIndex portIndex) {
        auto const& connected = model.connections(nodeId, portType, portIndex);
        if (connected.empty()) {
            return;
        }

        QColor color;
        if (connectionStyle.useDataDefinedColors()) {
            auto const& dataType = model.portData(nodeId, portType, portIndex, PortRole::DataType)
                                       .value<NodeDataType>();
            color = connectionStyle.normalColor(dataType.id);
        } else {
            color = nodeStyle.FilledConnectionPointColor;
        }

        QPointF const p = geometry.portPosition(nodeId, portType, portIndex);
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
    QRectF const caption_rect = geometry.captionRect(nodeId);
    qreal const caption_top = position.y() - caption_rect.height();

    QRectF icon_frame_rect;
    QRectF icon_rect;
    if (!nodeStyle.titleIcon.isNull()) {
        icon_frame_rect = QRectF(
            k_node_title_icon_margin,
            caption_top - 1.0,
            k_node_title_icon_frame_size,
            k_node_title_icon_frame_size);
        icon_rect = QRectF(
            icon_frame_rect.center().x() - k_node_title_icon_size / 2.0,
            icon_frame_rect.center().y() - k_node_title_icon_size / 2.0,
            k_node_title_icon_size,
            k_node_title_icon_size);
        position.setX(icon_frame_rect.right() + 6.0);
    }

    painter->setRenderHint(QPainter::TextAntialiasing, true);
    if (!icon_rect.isNull()) {
        QColor icon_frame_color = nodeStyle.GradientColor0;
        icon_frame_color.setAlpha(110);

        QColor icon_frame_border = nodeStyle.FontColorFaded;
        icon_frame_border.setAlpha(130);

        painter->setPen(QPen(icon_frame_border, 1.0));
        painter->setBrush(icon_frame_color);
        painter->drawRoundedRect(
            icon_frame_rect,
            k_node_title_icon_frame_radius,
            k_node_title_icon_frame_radius);

        nodeStyle.titleIcon.paint(painter, icon_rect.toRect());
    }
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

    for_each_port(model, nodeId, [&](PortType portType, PortIndex portIndex) {
        auto const &connected = model.connections(nodeId, portType, portIndex);

        QPointF const p = geometry.portTextPosition(nodeId, portType, portIndex);

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
    });
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
