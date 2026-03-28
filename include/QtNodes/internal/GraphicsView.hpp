#pragma once

#include <QtWidgets/QGraphicsView>

#include "Export.hpp"

namespace QtNodes {

class BasicGraphicsScene;

/**
 * @brief A central view able to render objects from `BasicGraphicsScene`.
 */
class NODE_EDITOR_PUBLIC GraphicsView : public QGraphicsView
{
    Q_OBJECT
public:
    struct ScaleRange
    {
        double minimum = 0;
        double maximum = 0;
    };

    enum class TextRenderingPolicy
    {
        QtText,
        PathWhenZooming,
        PathAlways,
    };

    enum class RasterizationPolicy
    {
        Crisp,
        Consistent,
    };

public:
    GraphicsView(QWidget *parent = Q_NULLPTR);
    GraphicsView(BasicGraphicsScene *scene, QWidget *parent = Q_NULLPTR);

    GraphicsView(const GraphicsView &) = delete;
    GraphicsView operator=(const GraphicsView &) = delete;

    QAction *clearSelectionAction() const;

    QAction *deleteSelectionAction() const;

    void setScene(BasicGraphicsScene *scene);

    void centerScene();

    /// @brief max=0/min=0 indicates infinite zoom in/out
    void setScaleRange(double minimum = 0, double maximum = 0);

    void setScaleRange(ScaleRange range);

    double getScale() const;

    bool isZoomAnimating() const;

    TextRenderingPolicy textRenderingPolicy() const;

    void setTextRenderingPolicy(TextRenderingPolicy policy);

    RasterizationPolicy rasterizationPolicy() const;

    void setRasterizationPolicy(RasterizationPolicy policy);

    void stopZoomAnimation();

    static double zoomAnimationScaleFactor(double velocity, double elapsedTimerSteps);

    static double zoomAnimationVelocityAfter(double velocity, double elapsedTimerSteps);

public Q_SLOTS:
    void scaleUp();

    void scaleDown();

    void setupScale(double scale);

    virtual void onDeleteSelectedObjects();

    virtual void onDuplicateSelectedObjects();

    virtual void onCopySelectedObjects();

    virtual void onPasteObjects();

    void zoomFitAll();

    void zoomFitSelected();

Q_SIGNALS:
    void scaleChanged(double scale);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;

    void keyReleaseEvent(QKeyEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void drawBackground(QPainter *painter, const QRectF &r) override;

    void showEvent(QShowEvent *event) override;

    void timerEvent(QTimerEvent *event) override;

protected:
    BasicGraphicsScene *nodeScene();

    /// Computes scene position for pasting the copied/duplicated node groups.
    QPointF scenePastePosition();

private:
    QAction *_clearSelectionAction = nullptr;
    QAction *_deleteSelectionAction = nullptr;
    QAction *_cutSelectionAction = nullptr;
    QAction *_duplicateSelectionAction = nullptr;
    QAction *_copySelectionAction = nullptr;
    QAction *_pasteAction = nullptr;

    QPointF _clickPos;
    ScaleRange _scaleRange;

    void applyZoomStep();
    void advanceZoomToTime(double targetTimeMs);
    void advanceZoomAnimation(double elapsedTimerSteps);
    void applyZoomFactor(double factor);
    void stopZoomTimer();
    void applyRasterizationPolicy();

    double _zoomVelocity = 0.0;
    QPointF _zoomPivot;
    int _zoomTimerId = 0;
    double _lastZoomStepTimeMs = 0.0;
    bool _hasZoomStepTime = false;
    double _zoomTimestampOffsetMs = 0.0;
    bool _hasZoomTimestampOffset = false;
    TextRenderingPolicy _textRenderingPolicy = TextRenderingPolicy::PathAlways;
    RasterizationPolicy _rasterizationPolicy = RasterizationPolicy::Consistent;
};
} // namespace QtNodes
