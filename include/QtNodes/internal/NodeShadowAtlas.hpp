#pragma once

#include <QtCore/QRectF>
#include <QtGui/QColor>

class QPainter;

namespace QtNodes::node_rendering {

/// Draws a precomputed blurred rounded-rect atlas as 9-slice tiles into
/// `node_rect`. The atlas is cached per (color, DPR) and snapped to device
/// pixels to avoid hairline gaps. Faster and smoother than
/// QGraphicsDropShadowEffect or stacked translucent rounded rects.
void draw_nine_slice_shadow(QPainter *painter, QColor shadow_color, QRectF const &node_rect);

} // namespace QtNodes::node_rendering
