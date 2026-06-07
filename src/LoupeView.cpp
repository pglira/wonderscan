#include "LoupeView.h"

#include "Page.h"

#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <initializer_list>

namespace {
constexpr int kSize = 220;  // square side of the magnified viewport
constexpr int kMargin = 8;
} // namespace

LoupeView::LoupeView(QWidget *parent) : QWidget(parent)
{
    // Fixed height so the loupe keeps a constant spot; the preview takes the
    // rest of the column. Width follows the column (the square is centred in it).
    setFixedHeight(kSize + 2 * kMargin);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(45, 45, 45));
    setPalette(pal);
}

void LoupeView::setImage(const QImage &image)
{
    m_image = image;
    update();
}

void LoupeView::setPage(const Page *page)
{
    m_page = page;
    update();
}

void LoupeView::setExportInset(int px)
{
    m_inset = std::max(0, px);
    update();
}

void LoupeView::setZoom(double zoom)
{
    m_zoom = std::clamp(zoom, 1.5, 12.0);
    update();
}

void LoupeView::showPoint(const QPointF &imagePoint)
{
    m_center = imagePoint;
    m_active = true;
    update();
}

void LoupeView::clear()
{
    m_active = false;
    update();
}

void LoupeView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Square viewport, centred in the (possibly wider) column.
    const int side = std::min(kSize, std::min(width(), height()) - 2 * kMargin);
    if (side <= 0)
        return;
    const QRectF view((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    p.fillRect(view, QColor(0, 0, 0));

    if (!m_active || m_image.isNull()) {
        p.setPen(QColor(150, 150, 150));
        p.drawText(view, Qt::AlignCenter, tr("Loupe"));
        p.setPen(QPen(QColor(90, 90, 90), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(view);
        return;
    }

    // Source window: `side / zoom` image px around the point, so the displayed
    // magnification (view / source) is exactly `zoom`. Near an image edge the
    // window runs off the bitmap; the uncovered part stays the black fill above.
    const double srcD = side / m_zoom;
    QRectF srcRect(m_center.x() - srcD / 2.0, m_center.y() - srcD / 2.0, srcD, srcD);
    p.drawImage(view, m_image, srcRect);

    // The same boundary lines the canvas draws, magnified to match.
    drawOutline(p, view, srcD);

    // Crosshair at centre + border, matching the old in-canvas loupe styling.
    p.setPen(QPen(QColor(255, 80, 80), 1.2));
    const QPointF c = view.center();
    p.drawLine(QPointF(c.x() - 10, c.y()), QPointF(c.x() + 10, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - 10), QPointF(c.x(), c.y() + 10));
    p.setPen(QPen(QColor(240, 240, 240), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(view);

    p.setPen(QColor(230, 230, 230));
    p.drawText(view.adjusted(0, 0, -4, -2), Qt::AlignRight | Qt::AlignBottom,
               QString::number(m_zoom, 'g', 2) + QStringLiteral("x"));
}

// Overlay the marked quad(s), spine, and export-inset boundary -- the same lines
// ImageCanvas draws -- mapped from image space into the magnified viewport and
// clipped to it. `srcD` is the side of the source window shown in `view`.
void LoupeView::drawOutline(QPainter &p, const QRectF &view, double srcD) const
{
    if (!m_page || !m_page->hasPoints() || srcD <= 0)
        return;

    const QPointF srcTL(m_center.x() - srcD / 2.0, m_center.y() - srcD / 2.0);
    const double k = view.width() / srcD; // == m_zoom
    auto map = [&](const QPointF &ip) { return view.topLeft() + (ip - srcTL) * k; };
    auto polyOf = [&](const QVector<QPointF> &pts, std::initializer_list<int> idx) {
        QPolygonF poly;
        for (int i : idx)
            poly << map(pts[i]);
        return poly;
    };

    p.save();
    p.setClipRect(view);
    p.setBrush(Qt::NoBrush);

    // Marked quad(s) (dashed green) + spine (dashed yellow), matching the canvas.
    p.setPen(QPen(QColor(60, 200, 120), 1.5, Qt::DashLine));
    if (m_page->mode == Page::Four) {
        p.drawPolygon(polyOf(m_page->points, {0, 1, 2, 3}));
    } else {
        p.drawPolygon(polyOf(m_page->points, {0, 1, 4, 5}));
        p.drawPolygon(polyOf(m_page->points, {1, 2, 3, 4}));
        p.setPen(QPen(QColor(255, 200, 60), 1.5, Qt::DashLine));
        p.drawLine(map(m_page->points[1]), map(m_page->points[4]));
    }

    // Export-inset boundary (dashed cyan).
    if (m_inset > 0) {
        const QVector<QPointF> in =
            insetPoints(m_page->points, m_page->mode, m_inset);
        if (in.size() == m_page->points.size()) {
            p.setPen(QPen(QColor(70, 200, 255), 1.5, Qt::DashLine));
            if (m_page->mode == Page::Four) {
                p.drawPolygon(polyOf(in, {0, 1, 2, 3}));
            } else {
                p.drawPolygon(polyOf(in, {0, 1, 4, 5}));
                p.drawPolygon(polyOf(in, {1, 2, 3, 4}));
            }
        }
    }

    p.restore();
}
