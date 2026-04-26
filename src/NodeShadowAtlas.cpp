#include "NodeShadowAtlas.hpp"

#include "NodeRenderingUtils.hpp"

#include <QtGui/QImage>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QTransform>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace QtNodes::node_rendering {

namespace {

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

QImage generate_shadow_atlas(QColor shadow_color, qreal dpr)
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

    // Tint with shadow color while preserving the style alpha and applying the
    // configured global strength multiplier.
    const int sr = shadow_color.red();
    const int sg = shadow_color.green();
    const int sb = shadow_color.blue();
    const int sa = shadow_color.alpha();
    for (int y = 0; y < phys; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < phys; ++x) {
            const int blurred_alpha = (qAlpha(line[x]) * sa) / 255;
            const int a = (blurred_alpha * k_shadow_opacity) / 255;
            line[x] = qPremultiply(qRgba(sr, sg, sb, a));
        }
    }

    return img;
}

struct CacheKey
{
    QRgb color;
    int dpr_micro; // dpr * 1e6 as int for reliable comparison

    bool operator==(CacheKey const &o) const
    {
        return color == o.color && dpr_micro == o.dpr_micro;
    }
};

struct CacheKeyHash
{
    std::size_t operator()(CacheKey const &k) const
    {
        return std::hash<uint64_t>()(
            (static_cast<uint64_t>(k.color) << 32) | static_cast<uint32_t>(k.dpr_micro));
    }
};

std::unordered_map<CacheKey, QImage, CacheKeyHash> s_shadow_cache;
std::mutex s_shadow_cache_mutex;

QImage cached_shadow_atlas(QColor shadow_color, qreal dpr)
{
    std::lock_guard<std::mutex> lock(s_shadow_cache_mutex);

    CacheKey key{shadow_color.rgba(), static_cast<int>(dpr * 1000000.0)};
    auto it = s_shadow_cache.find(key);
    if (it != s_shadow_cache.end()) {
        return it->second;
    }

    if (s_shadow_cache.size() >= 32) {
        // Arbitrary eviction is sufficient here; the cache is tiny.
        s_shadow_cache.erase(s_shadow_cache.begin());
    }

    return s_shadow_cache.emplace(key, generate_shadow_atlas(shadow_color, dpr)).first->second;
}

} // namespace

void draw_nine_slice_shadow(QPainter *painter, QColor shadow_color, QRectF const &node_rect)
{
    const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    QImage const atlas = cached_shadow_atlas(shadow_color, dpr);
    if (atlas.isNull()) {
        return;
    }

    // Margin in logical coords — covers the full blur transition
    // (outer falloff + inward ramp to plateau).
    const qreal m = k_shadow_margin;

    // Destination rect: node rect shifted by shadow offset, expanded by margin.
    const qreal dx = node_rect.x() + k_shadow_offset_x - m;
    const qreal dy = node_rect.y() + k_shadow_offset_y - m;
    const qreal dw = node_rect.width() + 2.0 * m;
    const qreal dh = node_rect.height() + 2.0 * m;
    const qreal inner_w = dw - 2.0 * m;
    const qreal inner_h = dh - 2.0 * m;

    if (inner_w <= 0.0 || inner_h <= 0.0) {
        return;
    }

    // Source margin/body in logical atlas coords (atlas has DPR set).
    const qreal sm = m;                                 // source margin
    const qreal sb = static_cast<qreal>(k_body_size); // source body

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
    const QRectF s_tl(0, 0, sm, sm);
    const QRectF s_tc(sm, 0, sb, sm);
    const QRectF s_tr(sm + sb, 0, sm, sm);
    const QRectF s_ml(0, sm, sm, sb);
    const QRectF s_mc(sm, sm, sb, sb);
    const QRectF s_mr(sm + sb, sm, sm, sb);
    const QRectF s_bl(0, sm + sb, sm, sm);
    const QRectF s_bc(sm, sm + sb, sb, sm);
    const QRectF s_br(sm + sb, sm + sb, sm, sm);

    // 9 target rects (snapped to device pixels).
    painter->drawImage(snap(dx,               dy,               m,       m),       atlas, s_tl);
    painter->drawImage(snap(dx + m,           dy,               inner_w, m),       atlas, s_tc);
    painter->drawImage(snap(dx + m + inner_w, dy,               m,       m),       atlas, s_tr);
    painter->drawImage(snap(dx,               dy + m,           m,       inner_h), atlas, s_ml);
    painter->drawImage(snap(dx + m,           dy + m,           inner_w, inner_h), atlas, s_mc);
    painter->drawImage(snap(dx + m + inner_w, dy + m,           m,       inner_h), atlas, s_mr);
    painter->drawImage(snap(dx,               dy + m + inner_h, m,       m),       atlas, s_bl);
    painter->drawImage(snap(dx + m,           dy + m + inner_h, inner_w, m),       atlas, s_bc);
    painter->drawImage(snap(dx + m + inner_w, dy + m + inner_h, m,       m),       atlas, s_br);
}

} // namespace QtNodes::node_rendering
