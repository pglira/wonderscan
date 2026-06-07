#pragma once

#include <QString>
#include <QVector>

struct Page;

namespace PdfExporter {

// Render the given pages (in order) and write a single PDF to `outPath`.
// Each page image is embedded as a baseline-JPEG (DCTDecode) XObject, so file
// size is controlled by `jpegQuality` (0..100). Page physical size is the
// dewarped pixel size divided by `dpi`.
//
// Returns false and sets *error on failure. `pages` must already be filtered
// to export-ready pages. `equalPageWidths` is forwarded to the spread renderer
// (see Warp::renderPages). `insetPx` shrinks each marked quad inward by that many
// pixels before dewarping (see insetPoints), so only the offset area is exported.
bool exportPdf(const QString &outPath,
               const QVector<const Page *> &pages,
               int dpi,
               int jpegQuality,
               QString *error,
               bool equalPageWidths = false,
               int insetPx = 0);

} // namespace PdfExporter
