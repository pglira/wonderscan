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
