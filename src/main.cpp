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

// Headless verification of the warp + PDF pipeline (no display needed).
static int runSelfTest(const QString &outPdf)
{
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
