#include "PdfExporter.h"

#include "Page.h"
#include "Warp.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QImageReader>

namespace {

struct PdfImage {
    QByteArray jpeg;
    int wpx = 0;
    int hpx = 0;
};

QByteArray encodeJpeg(const QImage &img, int quality)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    // RGB888 keeps the embedded JPEG a 3-component DeviceRGB stream.
    img.convertToFormat(QImage::Format_RGB888).save(&buf, "JPEG", quality);
    return bytes;
}

QByteArray fmt(double v)
{
    return QByteArray::number(v, 'f', 4); // locale-independent
}

// Minimal PDF writer: one image-only page per PdfImage, embedded as DCTDecode.
QByteArray buildPdf(const QVector<PdfImage> &images, int dpi)
{
    const int n = images.size();
    // Object numbering: 1=Catalog, 2=Pages, then per page: page, content, image.
    const int firstPageObj = 3;
    const int objsPerPage = 3;
    const int totalObjs = 2 + n * objsPerPage;

    QByteArray out;
    QVector<qint64> offset(totalObjs + 1, 0); // 1-based

    out += "%PDF-1.7\n";
    out += "%\xE2\xE3\xCF\xD3\n"; // binary marker

    auto beginObj = [&](int num) {
        offset[num] = out.size();
        out += QByteArray::number(num) + " 0 obj\n";
    };
    auto endObj = [&]() { out += "endobj\n"; };

    // 1: Catalog
    beginObj(1);
    out += "<< /Type /Catalog /Pages 2 0 R >>\n";
    endObj();

    // 2: Pages
    beginObj(2);
    out += "<< /Type /Pages /Count " + QByteArray::number(n) + " /Kids [";
    for (int i = 0; i < n; ++i)
        out += QByteArray::number(firstPageObj + i * objsPerPage) + " 0 R ";
    out += "] >>\n";
    endObj();

    for (int i = 0; i < n; ++i) {
        const PdfImage &im = images[i];
        const int pageObj = firstPageObj + i * objsPerPage;
        const int contentObj = pageObj + 1;
        const int imageObj = pageObj + 2;

        const double wpts = double(im.wpx) / dpi * 72.0;
        const double hpts = double(im.hpx) / dpi * 72.0;

        // Page
        beginObj(pageObj);
        out += "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "
               + fmt(wpts) + " " + fmt(hpts) + "] "
               + "/Resources << /XObject << /Im0 " + QByteArray::number(imageObj)
               + " 0 R >> /ProcSet [/PDF /ImageC] >> "
               + "/Contents " + QByteArray::number(contentObj) + " 0 R >>\n";
        endObj();

        // Content stream: fill the page with the image.
        QByteArray content = "q\n" + fmt(wpts) + " 0 0 " + fmt(hpts)
                             + " 0 0 cm\n/Im0 Do\nQ\n";
        beginObj(contentObj);
        out += "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n";
        out += content;
        out += "endstream\n";
        endObj();

        // Image XObject (DCTDecode)
        beginObj(imageObj);
        out += "<< /Type /XObject /Subtype /Image /Width "
               + QByteArray::number(im.wpx) + " /Height " + QByteArray::number(im.hpx)
               + " /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length "
               + QByteArray::number(im.jpeg.size()) + " >>\nstream\n";
        out += im.jpeg;
        out += "\nendstream\n";
        endObj();
    }

    // xref
    const qint64 xrefOffset = out.size();
    out += "xref\n";
    out += "0 " + QByteArray::number(totalObjs + 1) + "\n";
    out += "0000000000 65535 f \n";
    for (int num = 1; num <= totalObjs; ++num) {
        QByteArray o = QByteArray::number(offset[num]);
        while (o.size() < 10)
            o.prepend('0');
        out += o + " 00000 n \n";
    }

    // trailer
    out += "trailer\n<< /Size " + QByteArray::number(totalObjs + 1)
           + " /Root 1 0 R >>\n";
    out += "startxref\n" + QByteArray::number(xrefOffset) + "\n";
    out += "%%EOF\n";

    return out;
}

} // namespace

namespace PdfExporter {

bool exportPdf(const QString &outPath,
               const QVector<const Page *> &pages,
               int dpi,
               int jpegQuality,
               QString *error,
               bool equalPageWidths)
{
    if (dpi <= 0)
        dpi = 300;
    jpegQuality = qBound(1, jpegQuality, 100);

    QVector<PdfImage> images;

    for (const Page *p : pages) {
        QImageReader reader(p->path);
        reader.setAutoTransform(true); // honor EXIF orientation
        QImage original = reader.read();
        if (original.isNull()) {
            if (error)
                *error = QStringLiteral("Failed to load image: %1").arg(p->path);
            return false;
        }

        QImage rotated = Warp::applyRotation(original, p->rotation);
        QVector<QImage> rendered =
            Warp::renderPages(rotated, p->points, p->mode, p->splitSpread,
                              equalPageWidths);
        if (rendered.isEmpty()) {
            if (error)
                *error = QStringLiteral("Could not dewarp: %1").arg(p->path);
            return false;
        }

        for (const QImage &page : rendered) {
            PdfImage pi;
            pi.wpx = page.width();
            pi.hpx = page.height();
            pi.jpeg = encodeJpeg(page, jpegQuality);
            if (pi.jpeg.isEmpty()) {
                if (error)
                    *error = QStringLiteral("JPEG encoding failed for: %1").arg(p->path);
                return false;
            }
            images.push_back(std::move(pi));
        }
    }

    if (images.isEmpty()) {
        if (error)
            *error = QStringLiteral("No pages to export.");
        return false;
    }

    QByteArray pdf = buildPdf(images, dpi);

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot write file: %1").arg(outPath);
        return false;
    }
    file.write(pdf);
    file.close();
    return true;
}

} // namespace PdfExporter
