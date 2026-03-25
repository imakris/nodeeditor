#pragma once

#include "AbstractGraphModel.hpp"
#include "NodeStyle.hpp"

#include <QtCore/QMarginsF>

#include <algorithm>
#include <optional>

class QIcon;
class QImage;
class QSize;

namespace QtNodes::node_rendering {

inline constexpr qreal k_shadow_offset_x = 2.0;
inline constexpr qreal k_shadow_offset_y = 2.0;
inline constexpr qreal k_node_radius     = 3.0;
inline constexpr int   k_blur_radius     = 5;
inline constexpr int   k_blur_passes     = 3;
inline constexpr int   k_outer_margin    = 18;
inline constexpr int   k_inner_margin    = 16;
inline constexpr int   k_shadow_margin   = k_outer_margin + k_inner_margin;
inline constexpr int   k_body_size       = 64;
inline constexpr int   k_atlas_size      = k_body_size + 2 * k_shadow_margin;
inline constexpr int   k_shadow_opacity  = 210;
inline constexpr qreal k_port_margin     = 20.0;

inline constexpr qreal shadow_left_extent() { return k_shadow_margin - k_shadow_offset_x; }
inline constexpr qreal shadow_top_extent() { return k_shadow_margin - k_shadow_offset_y; }
inline constexpr qreal shadow_right_extent() { return k_shadow_margin + k_shadow_offset_x; }
inline constexpr qreal shadow_bottom_extent() { return k_shadow_margin + k_shadow_offset_y; }

inline QMarginsF node_visual_margins(bool shadow_enabled)
{
    if (!shadow_enabled) {
        return QMarginsF(k_port_margin, k_port_margin, k_port_margin, k_port_margin);
    }

    return QMarginsF(std::max(k_port_margin, shadow_left_extent()),
                     std::max(k_port_margin, shadow_top_extent()),
                     std::max(k_port_margin, shadow_right_extent()),
                     std::max(k_port_margin, shadow_bottom_extent()));
}

NodeStyle const &resolved_node_style(
    AbstractGraphModel &model,
    NodeId node_id,
    std::optional<NodeStyle> &fallback_storage);

QImage render_icon_image(QIcon const &icon, QSize const &logical_size, qreal dpr);

} // namespace QtNodes::node_rendering
