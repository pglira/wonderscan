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
