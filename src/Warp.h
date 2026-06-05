#pragma once

#include <QImage>
#include <QVector>
#include <QPointF>

#include "Page.h"

namespace Warp {

// Rotate a (already EXIF-corrected) image by a multiple of 90 degrees.
QImage applyRotation(const QImage &src, int rotation);

// Dewarp the page(s) described by `points` (in `rotated`'s pixel space) to
// upright rectangles. Returns the output page images in PDF order:
//   - Four-point        -> 1 image
//   - Six-point, split  -> 2 images (left, right)
//   - Six-point, stitch -> 1 image (left|right stitched at the spine)
// Returns empty if the point count does not match the mode.
QVector<QImage> renderPages(const QImage &rotated,
                            const QVector<QPointF> &points,
                            Page::Mode mode,
                            bool splitSpread);

} // namespace Warp
