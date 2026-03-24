#include "DefaultNodePainter.hpp"

#include "AbstractGraphModel.hpp"
#include "AbstractNodeGeometry.hpp"
#include "BasicGraphicsScene.hpp"
#include "ConnectionGraphicsObject.hpp"
#include "ConnectionIdUtils.hpp"
#include "DataFlowGraphModel.hpp"
#include "GraphicsView.hpp"
#include "NodeDelegateModel.hpp"
#include "NodeGraphicsObject.hpp"
#include "NodeState.hpp"
#include "StyleCollection.hpp"

#include <QtCore/QHash>
#include <QtGui/QImage>
#include <QtGui/QPainterPath>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace QtNodes {

namespace {

// ============================================================================
// 9-slice shadow atlas
// ============================================================================

// Fixed geometry for the shadow.
constexpr double k_shadow_offset_x = 2.0;
constexpr double k_shadow_offset_y = 2.0;
constexpr double k_node_radius     = 3.0;
// Box blur: radius per pass, number of passes.  3 passes of radius 5
// approximate a gaussian with sigma ~5.
constexpr int    k_blur_radius     = 5;
constexpr int    k_blur_passes     = 3;
// The 9-slice margin must cover the FULL blur transition: both the
// outer decay to zero AND the inward ramp from the shape edge up to
// the plateau.  With 3 passes of radius 5 the effective spread is
// ~15px in each direction.  The tile boundary is placed at
// (outer + inner) from the atlas edge, i.e. well inside the plateau.
constexpr int    k_outer_margin    = 18;  // outer decay to zero
constexpr int    k_inner_margin    = 16;  // inward ramp to plateau
constexpr int    k_shadow_margin   = k_outer_margin + k_inner_margin;
// Interior body: must be large relative to the blur spread so the
// plateau reaches near-full alpha after blur.  With ~15px spread,
// a 64px body ensures the center is well above the blur threshold.
constexpr int    k_body_size       = 64;
constexpr int    k_atlas_size      = k_body_size + 2 * k_shadow_margin;
// Global shadow opacity (0-255) applied after blur.
constexpr int    k_shadow_opacity  = 210;

void box_blur_alpha(QImage &img, int radius)
{
    const int w = img.width();
    const int h = img.height();
    if (w == 0 || h == 0 || radius <= 0) {
        return;
    }

    const int span = 2 * radius + 1;
    std::vector<uint8_t> buf(static_cast<std::size_t>(w) * h);

    // Read alpha from premultiplied ARGB scanlines.
    auto alpha_at = [&](int x, int y) -> int {
        return qAlpha(reinterpret_cast<QRgb const *>(img.constScanLine(y))[x]);
    };

    // Horizontal pass → buf
    for (int y = 0; y < h; ++y) {
        int sum = 0;
        for (int x = -radius; x <= radius; ++x) {
            sum += alpha_at(std::clamp(x, 0, w - 1), y);
        }
        buf[y * w] = static_cast<uint8_t>(sum / span);
        for (int x = 1; x < w; ++x) {
            sum += alpha_at(std::min(x + radius, w - 1), y);
            sum -= alpha_at(std::max(x - radius - 1, 0), y);
            buf[y * w + x] = static_cast<uint8_t>(sum / span);
        }
    }

    // Vertical pass → back into image
    for (int x = 0; x < w; ++x) {
        int sum = 0;
        for (int y = -radius; y <= radius; ++y) {
            sum += buf[std::clamp(y, 0, h - 1) * w + x];
        }
        reinterpret_cast<QRgb *>(img.scanLine(0))[x] = qPremultiply(qRgba(0, 0, 0, sum / span));
        for (int y = 1; y < h; ++y) {
            sum += buf[std::min(y + radius, h - 1) * w + x];
            sum -= buf[std::max(y - radius - 1, 0) * w + x];
            reinterpret_cast<QRgb *>(img.scanLine(y))[x] = qPremultiply(qRgba(0, 0, 0, sum / span));
        }
    }
}

QPixmap generate_shadow_atlas(QColor shadow_color, qreal dpr)
{
    const int phys = static_cast<int>(std::ceil(k_atlas_size * dpr));

    QImage img(phys, phys, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);

    // Draw an opaque rounded rect in the center of the atlas.
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::white);
        QRectF body(k_shadow_margin, k_shadow_margin, k_body_size, k_body_size);
        p.drawRoundedRect(body, k_node_radius, k_node_radius);
    }

    // Multi-pass box blur on the alpha channel.
    const int phys_blur = std::max(1, static_cast<int>(std::round(k_blur_radius * dpr)));
    for (int pass = 0; pass < k_blur_passes; ++pass) {
        box_blur_alpha(img, phys_blur);
    }

    // Tint with shadow color and scale alpha by shadow opacity.
    const int sr = shadow_color.red();
    const int sg = shadow_color.green();
    const int sb = shadow_color.blue();
    for (int y = 0; y < phys; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < phys; ++x) {
            const int a = (qAlpha(line[x]) * k_shadow_opacity) / 255;
            line[x] = qPremultiply(qRgba(sr, sg, sb, a));
        }
    }

    return QPixmap::fromImage(std::move(img));
}

struct Shadow_cache_key
{
    QRgb color;
    int dpr_micro;  // dpr * 1e6 as int for reliable comparison

    bool operator==(Shadow_cache_key const &o) const
    {
        return color == o.color && dpr_micro == o.dpr_micro;
    }
};

struct Shadow_cache_key_hash
{
    std::size_t operator()(Shadow_cache_key const &k) const
    {
        return std::hash<uint64_t>()(
            (static_cast<uint64_t>(k.color) << 32) |
            static_cast<uint32_t>(k.dpr_micro));
    }
};

std::unordered_map<Shadow_cache_key, QPixmap, Shadow_cache_key_hash> s_shadow_cache;

QPixmap const &cached_shadow_atlas(QColor shadow_color, qreal dpr)
{
    Shadow_cache_key key{shadow_color.rgba(),
                         static_cast<int>(dpr * 1000000.0)};
    auto it = s_shadow_cache.find(key);
    if (it != s_shadow_cache.end()) {
        return it->second;
    }

    if (s_shadow_cache.size() > 16) {
        s_shadow_cache.clear();
    }

    return s_shadow_cache.emplace(key, generate_shadow_atlas(shadow_color, dpr))
        .first->second;
}

void draw_nine_slice_shadow(
    QPainter *painter,
    QColor shadow_color,
    QRectF const &node_rect)
{
    const qreal dpr = painter->device()
        ? static_cast<qreal>(painter->device()->devicePixelRatio())
        : 1.0;
    QPixmap const &atlas = cached_shadow_atlas(shadow_color, dpr);
    if (atlas.isNull()) {
        return;
    }

    // Margin in logical coords — covers the full blur transition
    // (outer falloff + inward ramp to plateau).
    const qreal m = k_shadow_margin;

    // Destination rect: node rect shifted by shadow offset, expanded by margin.
    const qreal dx = node_rect.x() + k_shadow_offset_x - m;
    const qreal dy = node_rect.y() + k_shadow_offset_y - m;
    const qreal dw = node_rect.width()  + 2.0 * m;
    const qreal dh = node_rect.height() + 2.0 * m;
    const qreal inner_w = dw - 2.0 * m;
    const qreal inner_h = dh - 2.0 * m;

    if (inner_w <= 0.0 || inner_h <= 0.0) {
        return;
    }

    // Source margin/body in logical atlas coords (atlas has DPR set).
    const qreal sm = m;                          // source margin
    const qreal sb = static_cast<qreal>(k_body_size);  // source body

    // Snap target rects to device pixels to prevent hairline gaps.
    QTransform const &dt = painter->deviceTransform();
    bool inv_ok = false;
    QTransform const inv = dt.inverted(&inv_ok);
    auto snap = [&](qreal lx, qreal ly, qreal lw, qreal lh) -> QRectF {
        if (!inv_ok) {
            return QRectF(lx, ly, lw, lh);
        }
        QPointF p0 = dt.map(QPointF(lx, ly));
        QPointF p1 = dt.map(QPointF(lx + lw, ly + lh));
        p0 = QPointF(std::round(p0.x()), std::round(p0.y()));
        p1 = QPointF(std::round(p1.x()), std::round(p1.y()));
        return QRectF(inv.map(p0), inv.map(p1));
    };

    // 9 source rects (logical coords in the atlas).
    const QRectF s_tl(0,        0,        sm, sm);
    const QRectF s_tc(sm,       0,        sb, sm);
    const QRectF s_tr(sm + sb,  0,        sm, sm);
    const QRectF s_ml(0,        sm,       sm, sb);
    const QRectF s_mc(sm,       sm,       sb, sb);
    const QRectF s_mr(sm + sb,  sm,       sm, sb);
    const QRectF s_bl(0,        sm + sb,  sm, sm);
    const QRectF s_bc(sm,       sm + sb,  sb, sm);
    const QRectF s_br(sm + sb,  sm + sb,  sm, sm);

    // 9 target rects (snapped to device pixels).
    painter->drawPixmap(snap(dx,                dy,                m,       m),       atlas, s_tl);
    painter->drawPixmap(snap(dx + m,            dy,                inner_w, m),       atlas, s_tc);
    painter->drawPixmap(snap(dx + m + inner_w,  dy,                m,       m),       atlas, s_tr);
    painter->drawPixmap(snap(dx,                dy + m,            m,       inner_h), atlas, s_ml);
    painter->drawPixmap(snap(dx + m,            dy + m,            inner_w, inner_h), atlas, s_mc);
    painter->drawPixmap(snap(dx + m + inner_w,  dy + m,            m,       inner_h), atlas, s_mr);
    painter->drawPixmap(snap(dx,                dy + m + inner_h,  m,       m),       atlas, s_bl);
    painter->drawPixmap(snap(dx + m,            dy + m + inner_h,  inner_w, m),       atlas, s_bc);
    painter->drawPixmap(snap(dx + m + inner_w,  dy + m + inner_h,  m,       m),       atlas, s_br);
}

// ============================================================================

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
QHash<QRgb, QPixmap> s_validation_icon_cache;

QPixmap validation_icon(QIcon const &icon, QColor const &color)
{
    auto it = s_validation_icon_cache.constFind(color.rgba());
    if (it != s_validation_icon_cache.constEnd()) {
        return *it;
    }

    QPixmap pixmap = icon.pixmap(QSize(16, 16));

    QPainter imgPainter(&pixmap);
    imgPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    imgPainter.fillRect(pixmap.rect(), color);
    imgPainter.end();

    return s_validation_icon_cache.insert(color.rgba(), std::move(pixmap)).value();
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

        auto it = s_text_path_cache.constFind(key);
        if (it == s_text_path_cache.constEnd()) {
            QPainterPath path;
            path.addText(QPointF(0, 0), font, text);
            it = s_text_path_cache.insert(key, std::move(path));

            // Prevent unbounded growth for highly dynamic scenes.
            if (s_text_path_cache.size() > 500) {
                QPainterPath keep = *it;
                s_text_path_cache.clear();
                it = s_text_path_cache.insert(key, std::move(keep));
            }
        }

        painter->setPen(Qt::NoPen);
        painter->translate(position);
        painter->fillPath(*it, color);
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

    // Fast path: get NodeStyle directly from the delegate model, avoiding
    // the NodeStyle -> JSON -> QVariant -> JSON -> NodeStyle round-trip.
    NodeStyle const *stylePtr = nullptr;
    if (auto *dfModel = dynamic_cast<DataFlowGraphModel *>(&model)) {
        if (auto *delegate = dfModel->delegateModel<NodeDelegateModel>(nodeId)) {
            stylePtr = &delegate->nodeStyle();
        }
    }
    // Fallback: only constructed when the fast path above cannot resolve the
    // style.  The default NodeStyle() constructor is expensive (loads SVG
    // icons and parses JSON from resources), so it must not run on every paint.
    NodeStyle fallbackStorage(QJsonObject{});
    if (!stylePtr) {
        QJsonDocument json = QJsonDocument::fromVariant(model.nodeData(nodeId, NodeRole::Style));
        fallbackStorage = NodeStyle(json.object());
        stylePtr = &fallbackStorage;
    }
    NodeStyle const &style = *stylePtr;

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
        switch (state._state) {
        case NodeValidationState::State::Error: {
            invalid = true;
            color = nodeStyle.ErrorColor;
        } break;
        case NodeValidationState::State::Warning: {
            invalid = true;
            color = nodeStyle.WarningColor;
            break;
        default:
            break;
        }
        }
    }

    QRectF boundary(0, 0, size.width(), size.height());

    double const radius = 3.0;

    // 9-slice shadow: a precomputed blurred atlas is sliced into 9 tiles
    // and stretched to fit the node.  One atlas per (color, DPR), size-
    // independent.  Much faster than QGraphicsDropShadowEffect and smoother
    // than stacked translucent rounded rects.
    if (nodeStyle.ShadowEnabled) {
        draw_nine_slice_shadow(painter, nodeStyle.ShadowColor, boundary);
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

void DefaultNodePainter::drawConnectionPoints(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &nodeStyle) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    auto const &connectionStyle = StyleCollection::connectionStyle();

    float diameter = nodeStyle.ConnectionPointDiameter;
    auto reducedDiameter = diameter * 0.6;

    for (PortType portType : {PortType::Out, PortType::In}) {
        auto portCountRole = (portType == PortType::Out) ? NodeRole::OutPortCount
                                                         : NodeRole::InPortCount;
        size_t const n = model.nodeData(nodeId, portCountRole).toUInt();

        for (PortIndex portIndex = 0; portIndex < n; ++portIndex) {
            QPointF p = geometry.portPosition(nodeId, portType, portIndex);

            auto const &dataType = model.portData(nodeId, portType, portIndex, PortRole::DataType)
                                       .value<NodeDataType>();

            double r = 1.0;

            NodeState const &state = ngo.nodeState();

            if (auto const *cgo = state.connectionForReaction()) {
                PortType requiredPort = cgo->connectionState().requiredPort();

                if (requiredPort == portType) {
                    ConnectionId possibleConnectionId = makeCompleteConnectionId(cgo->connectionId(),
                                                                                 nodeId,
                                                                                 portIndex);

                    bool const possible = model.connectionPossible(possibleConnectionId);

                    auto cp = cgo->sceneTransform().map(cgo->endPoint(requiredPort));
                    cp = ngo.sceneTransform().inverted().map(cp);

                    auto diff = cp - p;
                    double dist = std::sqrt(QPointF::dotProduct(diff, diff));

                    if (possible) {
                        double const thres = 40.0;
                        r = (dist < thres) ? (2.0 - dist / thres) : 1.0;
                    } else {
                        double const thres = 80.0;
                        r = (dist < thres) ? (dist / thres) : 1.0;
                    }
                }
            }

            if (connectionStyle.useDataDefinedColors()) {
                painter->setBrush(connectionStyle.normalColor(dataType.id));
            } else {
                painter->setBrush(nodeStyle.ConnectionPointColor);
            }

            painter->drawEllipse(p, reducedDiameter * r, reducedDiameter * r);
        }
    }

    if (ngo.nodeState().connectionForReaction()) {
        ngo.nodeState().resetConnectionForReaction();
    }
}

void DefaultNodePainter::drawFilledConnectionPoints(QPainter *painter, NodeGraphicsObject &ngo, NodeStyle const &nodeStyle) const
{
    AbstractGraphModel &model = ngo.graphModel();
    NodeId const nodeId = ngo.nodeId();
    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    auto diameter = nodeStyle.ConnectionPointDiameter;

    for (PortType portType : {PortType::Out, PortType::In}) {
        size_t const n = model
                             .nodeData(nodeId,
                                       (portType == PortType::Out) ? NodeRole::OutPortCount
                                                                   : NodeRole::InPortCount)
                             .toUInt();

        for (PortIndex portIndex = 0; portIndex < n; ++portIndex) {
            QPointF p = geometry.portPosition(nodeId, portType, portIndex);

            auto const &connected = model.connections(nodeId, portType, portIndex);

            if (!connected.empty()) {
                auto const &dataType = model
                                           .portData(nodeId, portType, portIndex, PortRole::DataType)
                                           .value<NodeDataType>();

                auto const &connectionStyle = StyleCollection::connectionStyle();
                if (connectionStyle.useDataDefinedColors()) {
                    QColor const c = connectionStyle.normalColor(dataType.id);
                    painter->setPen(c);
                    painter->setBrush(c);
                } else {
                    painter->setPen(nodeStyle.FilledConnectionPointColor);
                    painter->setBrush(nodeStyle.FilledConnectionPointColor);
                }

                painter->drawEllipse(p, diameter * 0.4, diameter * 0.4);
            }
        }
    }
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
        unsigned int n = model.nodeData<unsigned int>(nodeId,
                                                      (portType == PortType::Out)
                                                          ? NodeRole::OutPortCount
                                                          : NodeRole::InPortCount);

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

    // Skip if status is NoStatus
    if (delegate->processingStatus() == NodeProcessingStatus::NoStatus)
        return;

    AbstractNodeGeometry &geometry = ngo.nodeScene()->nodeGeometry();

    QSize size = geometry.size(nodeId);

    QPixmap pixmap = delegate->processingStatusIcon();
    if (pixmap.isNull())
        return;

    ProcessingIconStyle const &iconStyle = delegate->nodeStyle().processingIconStyle;

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
    painter->drawPixmap(targetRect, pixmap, QRectF(pixmap.rect()));
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
    if (state._state == NodeValidationState::State::Valid)
        return;

    QSize size = geometry.size(nodeId);

    QSize const iconSize(16, 16);

    QColor color = (state._state == NodeValidationState::State::Error) ? nodeStyle.ErrorColor
                                                                       : nodeStyle.WarningColor;
    QPixmap const pixmap = validation_icon(_toolTipIcon, color);

    QPointF center(size.width(), 0.0);
    center += QPointF(iconSize.width() / 2.0, -iconSize.height() / 2.0);

    QRectF const targetRect(center.x() - iconSize.width() / 2.0,
                            center.y() - iconSize.height() / 2.0,
                            iconSize.width(),
                            iconSize.height());
    painter->drawPixmap(targetRect, pixmap, QRectF(pixmap.rect()));
}

} // namespace QtNodes
