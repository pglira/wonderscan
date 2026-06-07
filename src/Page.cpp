#include "Page.h"

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
