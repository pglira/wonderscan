#include "PreviewPane.h"

#include <QPainter>

PreviewPane::PreviewPane(QWidget *parent) : QWidget(parent)
{
    setMinimumWidth(220);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(45, 45, 45));
    setPalette(pal);
}

void PreviewPane::setPages(const QVector<QImage> &pages)
{
    m_pages = pages;
    update();
}

void PreviewPane::clear()
{
    m_pages.clear();
    update();
}

void PreviewPane::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_pages.isEmpty()) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(rect(), Qt::AlignCenter, tr("Preview"));
        return;
    }

    const int gap = 8;
    const int n = m_pages.size();
    const double cellW = (width() - gap * (n + 1)) / double(n);
    const double cellH = height() - 2 * gap;

    double x = gap;
    for (const QImage &img : m_pages) {
        if (img.isNull()) {
            x += cellW + gap;
            continue;
        }
        const double s = std::min(cellW / img.width(), cellH / img.height());
        const double w = img.width() * s;
        const double h = img.height() * s;
        QRectF target(x + (cellW - w) / 2.0, gap + (cellH - h) / 2.0, w, h);
        p.drawImage(target, img);
        x += cellW + gap;
    }
}
