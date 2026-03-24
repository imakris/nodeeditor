#pragma once

#include <QtGui/QIcon>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>

#include "AbstractConnectionPainter.hpp"
#include "Definitions.hpp"

namespace QtNodes {

class ConnectionGeometry;
class ConnectionGraphicsObject;

class DefaultConnectionPainter : public AbstractConnectionPainter
{
public:
    void paint(QPainter *painter, ConnectionGraphicsObject const &cgo) const override;
    QPainterPath getPainterStroke(ConnectionGraphicsObject const &cgo) const override;
private:
    QPainterPath cubicPath(ConnectionGraphicsObject const &connection) const;
    void drawSketchLine(QPainter *painter, ConnectionGraphicsObject const &cgo) const;
    void drawHoveredOrSelected(QPainter *painter, ConnectionGraphicsObject const &cgo) const;
    void drawNormalLine(QPainter *painter, ConnectionGraphicsObject const &cgo) const;
#ifdef NODE_DEBUG_DRAWING
    void debugDrawing(QPainter *painter, ConnectionGraphicsObject const &cgo) const;
#endif

private:
    // Lazily initialized on first use to avoid depending on Qt resource
    // system availability at static-init / early construction time.
    mutable QPixmap _convertPixmap;
    mutable bool _convertPixmapInitialized = false;

    QPixmap const &convertPixmap() const
    {
        if (Q_UNLIKELY(!_convertPixmapInitialized)) {
            _convertPixmap = QIcon(QStringLiteral(":/convert.png")).pixmap(QSize(22, 22));
            _convertPixmapInitialized = true;
        }
        return _convertPixmap;
    }
};

} // namespace QtNodes
