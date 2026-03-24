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
#include <QtCore/QMargins>
#include <QtGui/QPainterPath>

#include <cmath>

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

    if (ngo.nodeState().hovered()) {
        QPen p(color, nodeStyle.HoveredPenWidth);
        painter->setPen(p);
    }
    else {
        QPen p(color, nodeStyle.PenWidth);
        painter->setPen(p);
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
    QRectF boundary(0, 0, size.width(), size.height());

    double const radius = 3.0;

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
