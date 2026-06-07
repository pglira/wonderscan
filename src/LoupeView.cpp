#include "LoupeView.h"

#include <QPainter>
#include <algorithm>

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
