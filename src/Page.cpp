#include "Page.h"

#include <algorithm>
#include <cmath>

namespace {

double length(const QPointF &v)
{
    return std::hypot(v.x(), v.y());
}

// Unit perpendicular to edge a->b, flipped to point toward `interior`.
QPointF inwardNormal(const QPointF &a, const QPointF &b, const QPointF &interior)
{
    const QPointF d = b - a;
    const double len = length(d);
    if (len < 1e-9)
        return QPointF(0, 0);
    QPointF n(-d.y() / len, d.x() / len);
    if (QPointF::dotProduct(n, interior - (a + b) / 2.0) < 0)
        n = -n;
    return n;
}

// Intersection of line (p1 + t*d1) with line (p2 + s*d2); `fallback` if parallel.
QPointF lineIntersect(const QPointF &p1, const QPointF &d1,
                      const QPointF &p2, const QPointF &d2,
                      const QPointF &fallback)
{
    const double denom = d1.x() * d2.y() - d1.y() * d2.x();
    if (std::abs(denom) < 1e-9)
        return fallback;
    const QPointF w = p2 - p1;
    const double t = (w.x() * d2.y() - w.y() * d2.x()) / denom;
    return p1 + t * d1;
}

// New position of `cur` after its two edges (prev->cur and cur->next) are each
// offset inward (toward `interior`) by `d`.
QPointF insetCorner(const QPointF &prev, const QPointF &cur, const QPointF &next,
                    const QPointF &interior, double d)
{
    const QPointF n1 = inwardNormal(prev, cur, interior);
    const QPointF n2 = inwardNormal(cur, next, interior);
    return lineIntersect(prev + n1 * d, cur - prev,    // edge prev->cur, offset
                         cur + n2 * d, next - cur,      // edge cur->next, offset
                         cur + (n1 + n2) * (d / 2.0));  // fallback when ~parallel
}

QPointF centroid(std::initializer_list<QPointF> pts)
{
    QPointF c;
    for (const QPointF &p : pts)
        c += p;
    return c / double(pts.size());
}

} // namespace

QVector<QPointF> defaultInsetPoints(const QSize &imageSize, Page::Mode mode)
{
    const double w = imageSize.width();
    const double h = imageSize.height();
    const double mx = 0.12 * w;
    const double my = 0.12 * h;
    const double cx = w / 2.0;

    if (mode == Page::Four) {
        return {
            {mx,      my},        // TL
            {w - mx,  my},        // TR
            {w - mx,  h - my},    // BR
            {mx,      h - my},    // BL
        };
    }

    // Six-point: outer corners + spine midpoints on the vertical centre line.
    return {
        {mx,     my},        // TL
        {cx,     my},        // TopSpine
        {w - mx, my},        // TR
        {w - mx, h - my},    // BR
        {cx,     h - my},    // BottomSpine
        {mx,     h - my},    // BL
    };
}

QVector<QPointF> convertPoints(const QVector<QPointF> &pts,
                               Page::Mode from, Page::Mode to)
{
    if (from == to || pts.size() != (from == Page::Four ? 4 : 6))
        return pts;

    if (from == Page::Four) {
        // [TL, TR, BR, BL] -> [TL, TopSpine, TR, BR, BottomSpine, BL]
        const QPointF &tl = pts[0], &tr = pts[1], &br = pts[2], &bl = pts[3];
        return {tl, (tl + tr) / 2.0, tr, br, (br + bl) / 2.0, bl};
    }

    // [TL, TopSpine, TR, BR, BottomSpine, BL] -> [TL, TR, BR, BL]
    return {pts[0], pts[2], pts[3], pts[5]};
}

QVector<QPointF> insetPoints(const QVector<QPointF> &pts, Page::Mode mode,
                             double inset)
{
    if (inset <= 0.0 || pts.size() != (mode == Page::Four ? 4 : 6))
        return pts;

    if (mode == Page::Four) {
        const QPointF &tl = pts[0], &tr = pts[1], &br = pts[2], &bl = pts[3];
        // Clamp so the inset can never cross the quad's centre and invert it.
        const double d = std::min(inset, 0.45 * std::min({length(tr - tl),
                                                          length(br - tr),
                                                          length(bl - br),
                                                          length(tl - bl)}));
        const QPointF c = centroid({tl, tr, br, bl});
        return {insetCorner(bl, tl, tr, c, d),   // TL
                insetCorner(tl, tr, br, c, d),   // TR
                insetCorner(tr, br, bl, c, d),   // BR
                insetCorner(br, bl, tl, c, d)};  // BL
    }

    // Six-point: [TL, TopSpine, TR, BR, BottomSpine, BL].
    const QPointF &tl = pts[0], &topS = pts[1], &tr = pts[2];
    const QPointF &br = pts[3], &botS = pts[4], &bl = pts[5];

    const double spineLen = length(botS - topS);
    const double d = std::min({inset,
                               0.45 * length(topS - tl), 0.45 * length(tr - topS),
                               0.45 * length(br - botS), 0.45 * length(botS - bl),
                               0.45 * length(tl - bl),   0.45 * length(tr - br),
                               0.45 * spineLen});
    const QPointF c = centroid({tl, topS, tr, br, botS, bl});

    // Outer corners inset via their two outer edges; spine endpoints slide inward
    // along the spine toward each other, leaving the spine line itself in place.
    const QPointF spineDir = spineLen > 1e-9 ? (botS - topS) / spineLen : QPointF();
    return {insetCorner(bl, tl, topS, c, d),     // TL
            topS + spineDir * d,                 // TopSpine
            insetCorner(topS, tr, br, c, d),     // TR
            insetCorner(tr, br, botS, c, d),     // BR
            botS - spineDir * d,                 // BottomSpine
            insetCorner(botS, bl, tl, c, d)};    // BL
}
