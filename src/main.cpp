#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QDir>
#include <QIcon>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>

#include "Document.h"
#include "MainWindow.h"
#include "Page.h"
#include "PdfExporter.h"
#include "Warp.h"

// Largest vertical jump of a dark line's per-column centroid across adjacent
// columns. A discontinuous spine seam shows up as a large spike here.
static double seamMaxJump(const QImage &spread)
{
    double prev = 0.0, maxJump = 0.0;
    bool havePrev = false;
    for (int x = 0; x < spread.width(); ++x) {
        double num = 0, den = 0;
        for (int y = 0; y < spread.height(); ++y) {
            const double w = 255.0 - qGray(spread.pixel(x, y));
            if (w > 80) { num += w * y; den += w; }
        }
        if (den <= 0) { havePrev = false; continue; }
        const double c = num / den;
        if (havePrev)
            maxJump = std::max(maxJump, std::abs(c - prev));
        prev = c;
        havePrev = true;
    }
    return maxJump;
}

// Pure-logic checks for the Document model (collection edits + project I/O
// round-trip). No display, no real image files needed.
#define WS_CHECK(cond)                                                          \
    do {                                                                        \
        if (!(cond)) {                                                          \
            qCritical() << "MODEL TEST FAILED:" << #cond;                       \
            return 1;                                                           \
        }                                                                       \
    } while (0)

static int runModelTests()
{
    // ---- collection operations ----
    Document doc;
    WS_CHECK(doc.addImages({}, Page::Four, false) == -1); // empty -> no-op
    WS_CHECK(doc.addImages({"/ws/a.jpg", "/ws/b.jpg", "/ws/c.jpg"},
                           Page::Four, false) == 0); // first appended index
    WS_CHECK(doc.pageCount() == 3);
    WS_CHECK(doc.addImages({"/ws/d.jpg"}, Page::Six, true) == 3); // appends at end
    WS_CHECK(doc.pageCount() == 4);
    WS_CHECK(doc.page(3).mode == Page::Six && doc.page(3).splitSpread);

    // reorder: swap, and out-of-range no-ops
    WS_CHECK(doc.move(0, 1) == 1);
    WS_CHECK(doc.page(0).path == "/ws/b.jpg" && doc.page(1).path == "/ws/a.jpg");
    WS_CHECK(doc.move(0, -1) == 0);          // off the top -> unchanged
    WS_CHECK(doc.move(3, 1) == 3);           // off the bottom -> unchanged
    WS_CHECK(doc.page(1).path == "/ws/a.jpg");

    // rotation clears that page's corners and wraps mod 360
    doc.page(0).marked = true;
    doc.page(0).points = {QPointF(1, 2)};
    doc.rotateAt(0, 90);
    WS_CHECK(doc.page(0).rotation == 90);
    WS_CHECK(!doc.page(0).marked && doc.page(0).points.isEmpty());
    doc.rotateAt(0, -180);
    WS_CHECK(doc.page(0).rotation == 270);   // (90 - 180) wrapped
    doc.rotateAll(90);
    WS_CHECK(doc.page(0).rotation == 0);      // 270 + 90 -> 360 -> 0
    WS_CHECK(doc.page(1).rotation == 90);

    // markedCount / anyMarked / removeAt
    Document d2;
    d2.addImages({"/a", "/b", "/c"}, Page::Four, false);
    WS_CHECK(d2.markedCount() == 0 && !d2.anyMarked());
    d2.page(0).marked = true;
    d2.page(2).marked = true;
    WS_CHECK(d2.markedCount() == 2 && d2.anyMarked());
    d2.removeAt(1);
    WS_CHECK(d2.pageCount() == 2);
    WS_CHECK(d2.page(0).path == "/a" && d2.page(1).path == "/c");
    d2.removeAt(5); // out of range -> no-op
    WS_CHECK(d2.pageCount() == 2);

    // ---- point conversion between modes (4 <-> 6) ----
    // 4 -> 6 keeps the corners and inserts spine points at the top/bottom edge
    // midpoints: [TL,TR,BR,BL] -> [TL, mid(TL,TR), TR, BR, mid(BR,BL), BL].
    const QVector<QPointF> quad = {{10, 20}, {110, 22}, {108, 210}, {12, 208}};
    const QVector<QPointF> six = convertPoints(quad, Page::Four, Page::Six);
    WS_CHECK(six.size() == 6);
    WS_CHECK(six[0] == quad[0] && six[2] == quad[1]);          // TL, TR
    WS_CHECK(six[3] == quad[2] && six[5] == quad[3]);          // BR, BL
    WS_CHECK(six[1] == QPointF(60, 21));                       // TopSpine = mid(TL,TR)
    WS_CHECK(six[4] == QPointF(60, 209));                      // BottomSpine = mid(BR,BL)
    // 6 -> 4 drops the spine points, keeping the four outer corners; round-trips.
    WS_CHECK(convertPoints(six, Page::Six, Page::Four) == quad);
    // guards: same mode, or a point count that doesn't match `from`, return input.
    WS_CHECK(convertPoints(quad, Page::Four, Page::Four) == quad);
    WS_CHECK(convertPoints(quad, Page::Six, Page::Four) == quad);

    // ---- project I/O round-trip ----
    Document src;
    src.addImages({"/img/p1.jpg", "/img/p2.jpg"}, Page::Four, false);
    src.setDpi(600);
    src.setJpegQuality(72);
    src.page(0).rotation = 180;
    src.page(0).marked = true;
    src.page(0).points = {{10, 20}, {110, 22}, {108, 210}, {12, 208}};
    src.page(1).mode = Page::Six;
    src.page(1).splitSpread = true;
    src.page(1).marked = true;
    src.page(1).points = {{1, 1}, {50, 1}, {99, 1}, {99, 99}, {50, 99}, {1, 99}};

    const QString wsp = QDir::tempPath() + "/wonderscan_modeltest.wsp";
    QString err;
    WS_CHECK(src.save(wsp, &err)); // save succeeds...
    WS_CHECK(!src.dirty());        // ...and clears dirty
    WS_CHECK(src.path() == wsp);

    Document dst;
    dst.setDirty(true);
    WS_CHECK(dst.load(wsp, &err)); // load succeeds...
    WS_CHECK(!dst.dirty());        // ...and clears dirty
    WS_CHECK(dst.path() == wsp);
    WS_CHECK(dst.dpi() == 600 && dst.jpegQuality() == 72);
    WS_CHECK(dst.pageCount() == 2);

    for (int i = 0; i < 2; ++i) {
        const Page &a = src.page(i);
        const Page &b = dst.page(i);
        WS_CHECK(a.path == b.path && a.rotation == b.rotation && a.mode == b.mode);
        WS_CHECK(a.splitSpread == b.splitSpread && a.marked == b.marked);
        WS_CHECK(a.points.size() == b.points.size());
        for (int k = 0; k < a.points.size(); ++k) {
            WS_CHECK(std::abs(a.points[k].x() - b.points[k].x()) < 1e-6);
            WS_CHECK(std::abs(a.points[k].y() - b.points[k].y()) < 1e-6);
        }
    }

    // these fake paths don't exist on disk -> both reported missing
    WS_CHECK(dst.missingImagePaths().size() == 2);

    qInfo().noquote() << "MODEL TESTS PASS";
    return 0;
}

#undef WS_CHECK

// Headless verification of the warp + PDF pipeline (no display needed).
static int runSelfTest(const QString &outPdf)
{
    if (const int rc = runModelTests(); rc != 0)
        return rc;

    QImage src(1600, 1200, QImage::Format_RGB888);
    src.fill(QColor(40, 40, 40));
    {
        QPainter p(&src);
        QPolygonF page;
        page << QPointF(220, 160) << QPointF(1360, 250)
             << QPointF(1300, 1040) << QPointF(260, 980);
        p.setBrush(Qt::white);
        p.setPen(Qt::NoPen);
        p.drawPolygon(page);
        p.setPen(QPen(QColor(30, 60, 200), 3));
        for (int gx = 320; gx < 1300; gx += 120)
            p.drawLine(gx, 180, gx, 1010);
        for (int gy = 220; gy < 1010; gy += 120)
            p.drawLine(280, gy, 1330, gy);
        p.setPen(QColor(0, 0, 0));
        QFont f;
        f.setPixelSize(64);
        p.setFont(f);
        p.drawText(QRectF(300, 300, 900, 200), Qt::AlignCenter, "wonderscan");
    }

    const QString tmp = QDir::tempPath() + "/wonderscan_selftest_src.jpg";
    if (!src.save(tmp, "JPEG", 92)) {
        qCritical() << "selftest: failed to write source image";
        return 1;
    }

    // Four-point dewarp using the known page corners.
    Page pg;
    pg.path = tmp;
    pg.mode = Page::Four;
    pg.marked = true;
    pg.points = {QPointF(220, 160), QPointF(1360, 250),
                 QPointF(1300, 1040), QPointF(260, 980)};

    QVector<QImage> one = Warp::renderPages(src, pg.points, pg.mode, pg.splitSpread);
    if (one.size() != 1 || one[0].isNull()) {
        qCritical() << "selftest: four-point renderPages failed";
        return 1;
    }

    // Six-point split vs. stitch counts.
    QVector<QPointF> six = defaultInsetPoints(src.size(), Page::Six);
    if (Warp::renderPages(src, six, Page::Six, true).size() != 2) {
        qCritical() << "selftest: six-point split should yield 2 pages";
        return 1;
    }
    if (Warp::renderPages(src, six, Page::Six, false).size() != 1) {
        qCritical() << "selftest: six-point stitch should yield 1 page";
        return 1;
    }

    // Seam continuity: a horizontal line crossing a tilted spine must stay
    // continuous in the stitched spread (no jump at the seam).
    QImage spread(800, 650, QImage::Format_RGB888);
    spread.fill(Qt::white);
    {
        QPainter sp(&spread);
        sp.setPen(QPen(Qt::black, 6));
        sp.drawLine(120, 340, 690, 340);
    }
    const QVector<QPointF> spts = {{120, 150}, {395, 120}, {690, 150},
                                   {700, 520}, {405, 555}, {115, 520}};
    QVector<QImage> stitched = Warp::renderPages(spread, spts, Page::Six, false);
    if (stitched.size() != 1 || stitched[0].isNull()) {
        qCritical() << "selftest: six-point stitch render failed";
        return 1;
    }
    stitched[0].save(QDir::tempPath() + "/wonderscan_selftest_spread.png", "PNG");
    const double seamJump = seamMaxJump(stitched[0]);
    if (seamJump > 6.0) {
        qCritical() << "selftest: spine seam discontinuous, max column jump ="
                    << seamJump << "px";
        return 1;
    }

    // Equal-width spreads: spts is deliberately asymmetric (WL != WR), so the
    // halves differ normally but must come out the same width once forced equal.
    QVector<QImage> uneq = Warp::renderPages(spread, spts, Page::Six, true);
    QVector<QImage> eqd =
        Warp::renderPages(spread, spts, Page::Six, true, /*equalPageWidths=*/true);
    if (uneq.size() != 2 || eqd.size() != 2) {
        qCritical() << "selftest: equal-width split should yield 2 pages";
        return 1;
    }
    if (uneq[0].width() == uneq[1].width()) {
        qCritical() << "selftest: test spread is not asymmetric (weak guard)";
        return 1;
    }
    if (eqd[0].width() != eqd[1].width()) {
        qCritical() << "selftest: equal-width split halves differ:"
                    << eqd[0].width() << "vs" << eqd[1].width();
        return 1;
    }

    QVector<const Page *> ex{&pg};
    QString err;
    if (!PdfExporter::exportPdf(outPdf, ex, 300, 85, &err)) {
        qCritical() << "selftest: export failed:" << err;
        return 1;
    }

    QFileInfo fi(outPdf);
    if (!fi.exists() || fi.size() < 1000) {
        qCritical() << "selftest: output PDF missing or too small";
        return 1;
    }
    QFile f(outPdf);
    f.open(QIODevice::ReadOnly);
    const QByteArray head = f.read(5);
    f.close();
    if (!head.startsWith("%PDF-")) {
        qCritical() << "selftest: bad PDF header";
        return 1;
    }

    qInfo().noquote() << QStringLiteral(
                             "SELFTEST PASS: %1 (%2 bytes); dewarped page %3x%4; "
                             "spine seam max jump %5 px")
                             .arg(outPdf)
                             .arg(fi.size())
                             .arg(one[0].width())
                             .arg(one[0].height())
                             .arg(seamJump, 0, 'f', 2);
    return 0;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("wonderscan");
    QApplication::setOrganizationName("wonderscan"); // for QSettings location
    QApplication::setApplicationDisplayName("wonderscan");
    QApplication::setDesktopFileName("wonderscan");

    const QStringList args = app.arguments();
    if (args.size() >= 2 && args[1] == "--selftest") {
        const QString out = args.size() >= 3 ? args[2]
                                             : QDir::tempPath() + "/wonderscan_selftest.pdf";
        return runSelfTest(out);
    }

    QIcon icon;
    icon.addFile(":/icons/wonderscan-256.png");
    icon.addFile(":/icons/wonderscan-128.png");
    icon.addFile(":/icons/wonderscan-64.png");
    icon.addFile(":/icons/wonderscan-32.png");
    QApplication::setWindowIcon(icon);

    MainWindow w;

    // Any non-flag file arguments (e.g. from the .desktop %F) are opened.
    QStringList files;
    for (int i = 1; i < args.size(); ++i) {
        if (!args[i].startsWith("--"))
            files << args[i];
    }
    if (!files.isEmpty())
        w.addPaths(files);

    w.show();
    return app.exec();
}
