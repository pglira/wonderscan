#pragma once

#include <QString>
#include <QPointF>
#include <QVector>
#include <QSize>

// Per-image editing state.
//
// `points` are stored in the coordinate space of the *rotated* image
// (the image the user actually sees and marks on), in pixels.
//
// Point ordering:
//   Four-point (single page): [TL, TR, BR, BL]
//   Six-point  (spread):      [TL, TopSpine, TR, BR, BottomSpine, BL]
//     Left page  quad = TL, TopSpine, BottomSpine, BL  -> indices 0,1,4,5
//     Right page quad = TopSpine, TR, BR, BottomSpine  -> indices 1,2,3,4
struct Page {
    enum Mode { Four = 4, Six = 6 };

    QString path;
    int rotation = 0;            // 0 / 90 / 180 / 270, applied after EXIF
    Mode mode = Four;
    bool splitSpread = false;    // 6-point only: split into two pages vs. stitch into one
    QVector<QPointF> points;     // 4 or 6 points, in rotated-image pixel coords
    bool marked = false;         // true once the user has positioned the corners

    int expectedPointCount() const { return mode == Four ? 4 : 6; }
    bool hasPoints() const { return points.size() == expectedPointCount(); }
    bool readyForExport() const { return marked && hasPoints(); }
};

// Default inset rectangle (12% margin) used when an image is first shown.
QVector<QPointF> defaultInsetPoints(const QSize &imageSize, Page::Mode mode);

// Convert an existing point set between modes, preserving the marked corners:
//   Four -> Six: keep the four corners, insert spine points at the midpoints of
//                the top and bottom edges (TopSpine = mid(TL,TR),
//                BottomSpine = mid(BR,BL)).
//   Six  -> Four: drop the two spine points, keep the four outer corners.
// `pts` must hold the point count for `from`; returns `pts` unchanged if it
// doesn't, or if `from == to`.
QVector<QPointF> convertPoints(const QVector<QPointF> &pts,
                               Page::Mode from, Page::Mode to);

// Shrink the marked quad(s) inward by `inset` pixels, in the rotated-image
// coordinate space, returning the point set that should actually be exported:
//   Four: each corner moves to the intersection of its two edges offset inward.
//   Six : the four *outer* corners move the same way, while the spine endpoints
//         move inward along the spine toward each other -- so the spine line is
//         kept (a stitched spread stays seamless), only its ends shrink.
// `inset` is clamped per quad so it can never collapse/invert the shape. Returns
// `pts` unchanged for `inset <= 0` or a point count that doesn't match `mode`.
QVector<QPointF> insetPoints(const QVector<QPointF> &pts, Page::Mode mode,
                             double inset);
