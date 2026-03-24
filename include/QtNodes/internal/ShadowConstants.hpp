#pragma once

namespace QtNodes {

/// Shared shadow-geometry constants used by both the node painter and the
/// node geometry classes so that the painted shadow never exceeds the
/// reported bounding rect.
///
/// The 9-slice shadow atlas is built once per (color, DPR) combination and
/// drawn as 9 stretched tiles.  The margin must cover the full blur
/// transition (outer decay to zero + inward ramp to plateau).
struct ShadowConstants
{
    static constexpr double offsetX = 2.0;
    static constexpr double offsetY = 2.0;

    // Blur spread: 3 passes of radius 5 give an effective spread of ~15 px.
    static constexpr int outerMargin = 18; // outer decay to zero
    static constexpr int innerMargin = 16; // inward ramp to plateau
    static constexpr int margin = outerMargin + innerMargin; // 34

    // How far beyond the node rect the shadow extends in each direction.
    // Left/top: the shadow is shifted right/down by offsetX/Y, then
    //           expanded by `margin` in every direction.
    static constexpr double extentLeft = margin - offsetX;   // 32
    static constexpr double extentTop = margin - offsetY;    // 32
    static constexpr double extentRight = margin + offsetX;  // 36
    static constexpr double extentBottom = margin + offsetY; // 36
};

} // namespace QtNodes
