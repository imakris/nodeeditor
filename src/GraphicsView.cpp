#include "GraphicsView.hpp"

#include "BasicGraphicsScene.hpp"
#include "ConnectionGraphicsObject.hpp"
#include "DataFlowGraphModel.hpp"
#include "Definitions.hpp"
#include "GroupGraphicsObject.hpp"
#include "NodeDelegateModel.hpp"
#include "NodeGraphicsObject.hpp"
#include "StyleCollection.hpp"
#include "UndoCommands.hpp"

#include <QtWidgets/QGraphicsScene>

#include <QtGui/QBrush>
#include <QtGui/QPen>

#include <QtWidgets/QMenu>

#include <QtCore/QDebug>
#include <QtCore/QPointF>
#include <QtCore/QRectF>

#include <QtOpenGL>
#include <QtWidgets>

#include <QtCore/QTimerEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr double zoom_friction = 0.75;
constexpr double zoom_impulse_per_step = 1.0;
constexpr double zoom_max_velocity = 5.0;
constexpr double zoom_per_notch = 1.05;
constexpr int zoom_timer_interval_ms = 16;
constexpr double zoom_velocity_epsilon = 0.001;
} // namespace

using QtNodes::BasicGraphicsScene;
using QtNodes::DataFlowGraphModel;
using QtNodes::GraphicsView;
using QtNodes::NodeDelegateModel;
using QtNodes::NodeGraphicsObject;

GraphicsView::GraphicsView(QWidget *parent)
    : QGraphicsView(parent)
    , _clearSelectionAction(Q_NULLPTR)
    , _deleteSelectionAction(Q_NULLPTR)
    , _cutSelectionAction(Q_NULLPTR)
    , _duplicateSelectionAction(Q_NULLPTR)
    , _copySelectionAction(Q_NULLPTR)
    , _pasteAction(Q_NULLPTR)
{
    setDragMode(QGraphicsView::ScrollHandDrag);
    setRenderHint(QPainter::Antialiasing);

    auto const &flowViewStyle = StyleCollection::flowViewStyle();

    setBackgroundBrush(flowViewStyle.BackgroundColor);

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

    setCacheMode(QGraphicsView::CacheBackground);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);

    setScaleRange(0.3, 2);

    // Sets the scene rect to its maximum possible ranges to avoid autu scene range
    // re-calculation when expanding the all QGraphicsItems common rect.
    int maxSize = 32767;
    setSceneRect(-maxSize, -maxSize, (maxSize * 2), (maxSize * 2));
}

GraphicsView::GraphicsView(BasicGraphicsScene *scene, QWidget *parent)
    : GraphicsView(parent)
{
    setScene(scene);
}

QAction *GraphicsView::clearSelectionAction() const
{
    return _clearSelectionAction;
}

QAction *GraphicsView::deleteSelectionAction() const
{
    return _deleteSelectionAction;
}

void GraphicsView::setScene(BasicGraphicsScene *scene)
{
    QGraphicsView::setScene(scene);
    if (!scene) {
        // Clear actions.
        delete _clearSelectionAction;
        delete _deleteSelectionAction;
        delete _duplicateSelectionAction;
        delete _copySelectionAction;
        delete _pasteAction;
        _clearSelectionAction = nullptr;
        _deleteSelectionAction = nullptr;
        _duplicateSelectionAction = nullptr;
        _copySelectionAction = nullptr;
        _pasteAction = nullptr;
        return;
    }

    {
        // setup actions
        delete _clearSelectionAction;
        _clearSelectionAction = new QAction(QStringLiteral("Clear Selection"), this);
        _clearSelectionAction->setShortcut(Qt::Key_Escape);

        connect(_clearSelectionAction, &QAction::triggered, scene, &QGraphicsScene::clearSelection);

        addAction(_clearSelectionAction);
    }

    {
        delete _deleteSelectionAction;
        _deleteSelectionAction = new QAction(QStringLiteral("Delete Selection"), this);
        _deleteSelectionAction->setShortcutContext(Qt::ShortcutContext::WidgetShortcut);
        _deleteSelectionAction->setShortcut(QKeySequence(QKeySequence::Delete));
        _deleteSelectionAction->setAutoRepeat(false);
        connect(_deleteSelectionAction,
                &QAction::triggered,
                this,
                &GraphicsView::onDeleteSelectedObjects);

        addAction(_deleteSelectionAction);
    }

    {
        delete _cutSelectionAction;
        _cutSelectionAction = new QAction(QStringLiteral("Cut Selection"), this);
        _cutSelectionAction->setShortcutContext(Qt::ShortcutContext::WidgetShortcut);
        _cutSelectionAction->setShortcut(QKeySequence(QKeySequence::Cut));
        _cutSelectionAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_X));
        _cutSelectionAction->setAutoRepeat(false);
        connect(_cutSelectionAction, &QAction::triggered, [this] {
            onCopySelectedObjects();
            onDeleteSelectedObjects();
        });

        addAction(_cutSelectionAction);
    }

    {
        delete _duplicateSelectionAction;
        _duplicateSelectionAction = new QAction(QStringLiteral("Duplicate Selection"), this);
        _duplicateSelectionAction->setShortcutContext(Qt::ShortcutContext::WidgetShortcut);
        _duplicateSelectionAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
        _duplicateSelectionAction->setAutoRepeat(false);
        connect(_duplicateSelectionAction,
                &QAction::triggered,
                this,
                &GraphicsView::onDuplicateSelectedObjects);

        addAction(_duplicateSelectionAction);
    }

    {
        delete _copySelectionAction;
        _copySelectionAction = new QAction(QStringLiteral("Copy Selection"), this);
        _copySelectionAction->setShortcutContext(Qt::ShortcutContext::WidgetShortcut);
        _copySelectionAction->setShortcut(QKeySequence(QKeySequence::Copy));
        _copySelectionAction->setAutoRepeat(false);
        connect(_copySelectionAction,
                &QAction::triggered,
                this,
                &GraphicsView::onCopySelectedObjects);

        addAction(_copySelectionAction);
    }

    {
        delete _pasteAction;
        _pasteAction = new QAction(QStringLiteral("Paste Selection"), this);
        _pasteAction->setShortcutContext(Qt::ShortcutContext::WidgetShortcut);
        _pasteAction->setShortcut(QKeySequence(QKeySequence::Paste));
        _pasteAction->setAutoRepeat(false);
        connect(_pasteAction, &QAction::triggered, this, &GraphicsView::onPasteObjects);

        addAction(_pasteAction);
    }

    auto undoAction = scene->undoStack().createUndoAction(this, tr("&Undo"));
    undoAction->setShortcuts(QKeySequence::Undo);
    addAction(undoAction);

    auto redoAction = scene->undoStack().createRedoAction(this, tr("&Redo"));
    redoAction->setShortcuts(QKeySequence::Redo);
    addAction(redoAction);
}

void GraphicsView::centerScene()
{
    if (scene()) {
        scene()->setSceneRect(QRectF());

        QRectF sceneRect = scene()->sceneRect();

        if (sceneRect.width() > this->rect().width() || sceneRect.height() > this->rect().height()) {
            fitInView(sceneRect, Qt::KeepAspectRatio);
        }

        centerOn(sceneRect.center());
    }
}

void GraphicsView::contextMenuEvent(QContextMenuEvent *event)
{
    QGraphicsView::contextMenuEvent(event);
    QMenu *menu = nullptr;
    const QPointF scenePos = mapToScene(event->pos());

    auto clickedItems = items(event->pos());

    for (QGraphicsItem *item : clickedItems) {
        if (auto *nodeItem = qgraphicsitem_cast<NodeGraphicsObject *>(item)) {
            Q_UNUSED(nodeItem);
            menu = nodeScene()->createStdMenu(scenePos);
            break;
        }

        if (auto *groupItem = qgraphicsitem_cast<GroupGraphicsObject *>(item)) {
            menu = nodeScene()->createGroupMenu(scenePos, groupItem);
            break;
        }
    }

    if (!menu) {
        if (!clickedItems.empty()) {
            menu = nodeScene()->createStdMenu(scenePos);
        } else {
            menu = nodeScene()->createSceneMenu(scenePos);
        }
    }

    if (menu) {
        menu->exec(event->globalPos());
    }

    return;
}

void GraphicsView::wheelEvent(QWheelEvent *event)
{
    QPoint delta = event->angleDelta();

    if (delta.y() == 0) {
        event->ignore();
        return;
    }

    double const steps = delta.y() / 120.0;
    _zoomVelocity = std::clamp(_zoomVelocity + steps * zoom_impulse_per_step,
                               -zoom_max_velocity, zoom_max_velocity);
    _zoomPivot = event->position();

    if (_zoomTimerId == 0) {
        if (scene()) {
            for (QGraphicsItem *item : scene()->items()) {
                if (qgraphicsitem_cast<NodeGraphicsObject *>(item)) {
                    item->setCacheMode(QGraphicsItem::NoCache);
                }
            }
        }
        _zoomTimerId = startTimer(zoom_timer_interval_ms);
    }
}

void GraphicsView::timerEvent(QTimerEvent *event)
{
    if (event->timerId() == _zoomTimerId) {
        applyZoomStep();
    } else {
        QGraphicsView::timerEvent(event);
    }
}

void GraphicsView::applyZoomStep()
{
    if (std::abs(_zoomVelocity) < zoom_velocity_epsilon) {
        stopZoomTimer();
        return;
    }

    static double const base_k = std::pow(zoom_per_notch,
                                          (1.0 - zoom_friction) / zoom_impulse_per_step);
    double const factor = std::pow(base_k, _zoomVelocity);
    double const current_scale = transform().m11();
    double const new_scale = current_scale * factor;

    if (_scaleRange.maximum > 0 && new_scale > _scaleRange.maximum) {
        applyZoomFactor(_scaleRange.maximum / current_scale);
        stopZoomTimer();
        return;
    }
    if (_scaleRange.minimum > 0 && new_scale < _scaleRange.minimum) {
        applyZoomFactor(_scaleRange.minimum / current_scale);
        stopZoomTimer();
        return;
    }

    applyZoomFactor(factor);
    _zoomVelocity *= zoom_friction;
}

void GraphicsView::applyZoomFactor(double factor)
{
    QPointF const scenePivot = mapToScene(_zoomPivot.toPoint());
    double const newScale = transform().m11() * factor;

    // Compute total offset needed so scenePivot appears at _zoomPivot.
    // Mapping: widgetPos = transform * scenePos - scrollOffset
    // We need: _zoomPivot = newScale * scenePivot + (tx,ty) - (hbar,vbar)
    // Split into integer scrollbar values and sub-pixel transform translation
    // to avoid the whole-pixel jumps that integer scrollbars cause.
    QPointF const fullOffset(newScale * scenePivot.x() - _zoomPivot.x(),
                             newScale * scenePivot.y() - _zoomPivot.y());

    int const hval = qRound(fullOffset.x());
    int const vval = qRound(fullOffset.y());
    double const tx = hval - fullOffset.x();
    double const ty = vval - fullOffset.y();

    QTransform t;
    t.translate(tx, ty);
    t.scale(newScale, newScale);

    auto const savedAnchor = transformationAnchor();
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setTransform(t, false);
    horizontalScrollBar()->setValue(hval);
    verticalScrollBar()->setValue(vval);
    setTransformationAnchor(savedAnchor);

    Q_EMIT scaleChanged(newScale);
}

void GraphicsView::stopZoomTimer()
{
    if (_zoomTimerId != 0) {
        killTimer(_zoomTimerId);
        _zoomTimerId = 0;

        // Remove sub-pixel translation from transform so normal interaction
        // (hit testing, panning) uses a clean scale-only transform.
        double const s = transform().m11();
        QTransform clean;
        clean.scale(s, s);
        setTransform(clean, false);

        if (scene()) {
            for (QGraphicsItem *item : scene()->items()) {
                if (qgraphicsitem_cast<NodeGraphicsObject *>(item)) {
                    item->setCacheMode(QGraphicsItem::DeviceCoordinateCache);
                }
            }
        }
    }
    _zoomVelocity = 0.0;
}

double GraphicsView::getScale() const
{
    return transform().m11();
}

void GraphicsView::setScaleRange(double minimum, double maximum)
{
    if (maximum < minimum)
        std::swap(minimum, maximum);
    minimum = std::max(0.0, minimum);
    maximum = std::max(0.0, maximum);

    _scaleRange = {minimum, maximum};

    setupScale(transform().m11());
}

void GraphicsView::setScaleRange(ScaleRange range)
{
    setScaleRange(range.minimum, range.maximum);
}

void GraphicsView::scaleUp()
{
    double const step = 1.2;
    double const factor = std::pow(step, 1.0);

    if (_scaleRange.maximum > 0) {
        QTransform t = transform();
        t.scale(factor, factor);
        if (t.m11() >= _scaleRange.maximum) {
            setupScale(t.m11());
            return;
        }
    }

    scale(factor, factor);
    Q_EMIT scaleChanged(transform().m11());
}

void GraphicsView::scaleDown()
{
    double const step = 1.2;
    double const factor = std::pow(step, -1.0);

    if (_scaleRange.minimum > 0) {
        QTransform t = transform();
        t.scale(factor, factor);
        if (t.m11() <= _scaleRange.minimum) {
            setupScale(t.m11());
            return;
        }
    }

    scale(factor, factor);
    Q_EMIT scaleChanged(transform().m11());
}

void GraphicsView::setupScale(double scale)
{
    scale = std::max(_scaleRange.minimum, std::min(_scaleRange.maximum, scale));

    if (scale <= 0)
        return;

    if (scale == transform().m11())
        return;

    QTransform matrix;
    matrix.scale(scale, scale);
    setTransform(matrix, false);

    Q_EMIT scaleChanged(scale);
}

void GraphicsView::onDeleteSelectedObjects()
{
    if (!nodeScene())
        return;

    nodeScene()->undoStack().push(new DeleteCommand(nodeScene()));
}

void GraphicsView::onDuplicateSelectedObjects()
{
    if (!nodeScene())
        return;

    QPointF const pastePosition = scenePastePosition();

    nodeScene()->undoStack().push(new CopyCommand(nodeScene()));
    nodeScene()->undoStack().push(new PasteCommand(nodeScene(), pastePosition));
}

void GraphicsView::onCopySelectedObjects()
{
    if (!nodeScene())
        return;

    nodeScene()->undoStack().push(new CopyCommand(nodeScene()));
}

void GraphicsView::onPasteObjects()
{
    if (!nodeScene())
        return;

    QPointF const pastePosition = scenePastePosition();
    nodeScene()->undoStack().push(new PasteCommand(nodeScene(), pastePosition));
}

void GraphicsView::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Shift:
        setDragMode(QGraphicsView::RubberBandDrag);
        break;

    default:
        break;
    }

    QGraphicsView::keyPressEvent(event);
}

void GraphicsView::keyReleaseEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Shift:
        setDragMode(QGraphicsView::ScrollHandDrag);
        break;

    default:
        break;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void GraphicsView::mousePressEvent(QMouseEvent *event)
{
    QGraphicsView::mousePressEvent(event);
    if (event->button() == Qt::LeftButton) {
        _clickPos = mapToScene(event->pos());
    }
}

void GraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    QGraphicsView::mouseMoveEvent(event);

    if (!scene())
        return;

    if (scene()->mouseGrabberItem() == nullptr && event->buttons() == Qt::LeftButton) {
        // Make sure shift is not being pressed
        if ((event->modifiers() & Qt::ShiftModifier) == 0) {
            QPointF difference = _clickPos - mapToScene(event->pos());
            setSceneRect(sceneRect().translated(difference.x(), difference.y()));
        }
    }
}

void GraphicsView::drawBackground(QPainter *painter, const QRectF &r)
{
    QGraphicsView::drawBackground(painter, r);

    painter->setRenderHint(QPainter::Antialiasing, true);

    qreal x_offset = 0.0;
    qreal y_offset = 0.0;

    QTransform const view_transform = transform();
    qreal const scale_x = std::abs(view_transform.m11());
    qreal const scale_y = std::abs(view_transform.m22());
    if (scale_x > 0.0) {
        x_offset = 0.5 / scale_x;
    }
    if (scale_y > 0.0) {
        y_offset = 0.5 / scale_y;
    }

    auto drawGrid = [&](double gridStep) {
        QRect windowRect = rect();
        QPointF tl = mapToScene(windowRect.topLeft());
        QPointF br = mapToScene(windowRect.bottomRight());

        double left = std::floor(tl.x() / gridStep - 0.5);
        double right = std::floor(br.x() / gridStep + 1.0);
        double bottom = std::floor(tl.y() / gridStep - 0.5);
        double top = std::floor(br.y() / gridStep + 1.0);

        // vertical lines
        for (int xi = int(left); xi <= int(right); ++xi) {
            qreal const x = xi * gridStep + x_offset;
            QLineF line(x, bottom * gridStep, x, top * gridStep);

            painter->drawLine(line);
        }

        // horizontal lines
        for (int yi = int(bottom); yi <= int(top); ++yi) {
            qreal const y = yi * gridStep + y_offset;
            QLineF line(left * gridStep, y, right * gridStep, y);
            painter->drawLine(line);
        }
    };

    auto const &flowViewStyle = StyleCollection::flowViewStyle();

    QPen pfine(flowViewStyle.FineGridColor, 1.0);
    pfine.setCosmetic(true);

    painter->setPen(pfine);
    drawGrid(15);

    QPen p(flowViewStyle.CoarseGridColor, 1.0);
    p.setCosmetic(true);

    painter->setPen(p);
    drawGrid(150);
}

void GraphicsView::showEvent(QShowEvent *event)
{
    QGraphicsView::showEvent(event);

    centerScene();
}

BasicGraphicsScene *GraphicsView::nodeScene()
{
    return dynamic_cast<BasicGraphicsScene *>(scene());
}

QPointF GraphicsView::scenePastePosition()
{
    QPoint origin = mapFromGlobal(QCursor::pos());

    QRect const viewRect = rect();
    if (!viewRect.contains(origin))
        origin = viewRect.center();

    return mapToScene(origin);
}

void GraphicsView::zoomFitAll()
{
    fitInView(scene()->itemsBoundingRect(), Qt::KeepAspectRatio);
}

void GraphicsView::zoomFitSelected()
{
    if (scene()->selectedItems().count() > 0) {
        QRectF unitedBoundingRect{};

        for (QGraphicsItem *item : scene()->selectedItems()) {
            unitedBoundingRect = unitedBoundingRect.united(
                item->mapRectToScene(item->boundingRect()));
        }

        fitInView(unitedBoundingRect, Qt::KeepAspectRatio);
    }
}
