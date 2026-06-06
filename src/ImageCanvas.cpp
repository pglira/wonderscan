#include "ImageCanvas.h"
#include "Page.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kHandleRadius = 7.0;
constexpr double kHitRadius = 16.0;
constexpr int kLoupeDiameter = 160;
constexpr int kLoupeMargin = 12;
} // namespace

ImageCanvas::ImageCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(320, 240);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(30, 30, 30));
    setAutoFillBackground(true);
    setPalette(pal);
}

void ImageCanvas::setImage(const QImage &rotated, Page *page)
{
    m_image = rotated;
    m_page = page;
    m_dragIndex = -1;
    update();
}

void ImageCanvas::clear()
{
    m_image = QImage();
    m_page = nullptr;
    m_dragIndex = -1;
    update();
}

void ImageCanvas::setLoupeZoom(double zoom)
{
    m_loupeZoom = std::clamp(zoom, 1.5, 12.0);
    update();
}

QRectF ImageCanvas::displayRect() const
{
    if (m_image.isNull())
        return QRectF();
    const double s = scale();
    const double w = m_image.width() * s;
    const double h = m_image.height() * s;
    return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
}

double ImageCanvas::scale() const
{
    if (m_image.isNull())
        return 1.0;
    return std::min(double(width()) / m_image.width(),
                    double(height()) / m_image.height());
}

QPointF ImageCanvas::imageToWidget(const QPointF &p) const
{
    const QRectF r = displayRect();
    const double s = scale();
    return QPointF(r.left() + p.x() * s, r.top() + p.y() * s);
}

QPointF ImageCanvas::widgetToImage(const QPointF &p) const
{
    const QRectF r = displayRect();
    const double s = scale();
    return QPointF((p.x() - r.left()) / s, (p.y() - r.top()) / s);
}

int ImageCanvas::handleAt(const QPointF &widgetPos) const
{
    if (!m_page || !m_page->hasPoints())
        return -1;
    int best = -1;
    double bestDist = kHitRadius;
    for (int i = 0; i < m_page->points.size(); ++i) {
        const QPointF w = imageToWidget(m_page->points[i]);
        const double d = std::hypot(w.x() - widgetPos.x(), w.y() - widgetPos.y());
        if (d <= bestDist) {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void ImageCanvas::drawQuad(QPainter &p, const QVector<int> &idx) const
{
    QPolygonF poly;
    for (int i : idx)
        poly << imageToWidget(m_page->points[i]);
    p.drawPolygon(poly);
}

void ImageCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_image.isNull()) {
        p.setPen(QColor(160, 160, 160));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("Open or add images to begin.\n\n"
                      "Drag the corner handles to the page edges."));
        return;
    }

    const QRectF dr = displayRect();
    p.drawImage(dr, m_image);

    if (m_page && m_page->hasPoints()) {
        // Quad outline(s).
        QPen quadPen(QColor(60, 200, 120), 2);
        p.setPen(quadPen);
        p.setBrush(Qt::NoBrush);

        if (m_page->mode == Page::Four) {
            drawQuad(p, {0, 1, 2, 3});
        } else {
            drawQuad(p, {0, 1, 4, 5}); // left
            drawQuad(p, {1, 2, 3, 4}); // right
            // Spine line emphasis.
            QPen spinePen(QColor(255, 200, 60), 2, Qt::DashLine);
            p.setPen(spinePen);
            p.drawLine(imageToWidget(m_page->points[1]),
                       imageToWidget(m_page->points[4]));
        }

        // Handles.
        for (int i = 0; i < m_page->points.size(); ++i) {
            const QPointF w = imageToWidget(m_page->points[i]);
            const bool spine = (m_page->mode == Page::Six && (i == 1 || i == 4));
            QColor fill = (i == m_dragIndex) ? QColor(255, 255, 255)
                          : spine            ? QColor(255, 200, 60)
                                             : QColor(60, 200, 120);
            p.setPen(QPen(QColor(20, 20, 20), 1.5));
            p.setBrush(fill);
            p.drawEllipse(w, kHandleRadius, kHandleRadius);
        }

        if (m_dragIndex >= 0)
            drawLoupe(p);
    }
}

void ImageCanvas::drawLoupe(QPainter &p) const
{
    if (m_dragIndex < 0 || !m_page)
        return;

    const QPointF ip = m_page->points[m_dragIndex]; // image coords
    const double srcD = kLoupeDiameter / m_loupeZoom;
    QRectF srcRect(ip.x() - srcD / 2.0, ip.y() - srcD / 2.0, srcD, srcD);

    // Keep the loupe clear of the handle being dragged: place it on the same
    // horizontal side as the handle but the opposite vertical side (e.g. drag
    // the top-left corner -> loupe shows in the bottom-left).
    const QPointF handleW = imageToWidget(ip);
    const bool handleLeft = handleW.x() < width() / 2.0;
    const bool handleTop = handleW.y() < height() / 2.0;
    const double lx = handleLeft ? kLoupeMargin
                                 : width() - kLoupeDiameter - kLoupeMargin;
    const double ly = handleTop ? height() - kLoupeDiameter - kLoupeMargin
                                : kLoupeMargin;
    QRectF loupeRect(lx, ly, kLoupeDiameter, kLoupeDiameter);

    QPainterPath clip;
    clip.addEllipse(loupeRect);
    p.save();
    p.setClipPath(clip);
    p.fillRect(loupeRect, QColor(0, 0, 0));
    p.drawImage(loupeRect, m_image, srcRect);
    p.restore();

    // Crosshair at the loupe centre + border.
    p.setPen(QPen(QColor(255, 80, 80), 1.2));
    const QPointF c = loupeRect.center();
    p.drawLine(QPointF(c.x() - 10, c.y()), QPointF(c.x() + 10, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - 10), QPointF(c.x(), c.y() + 10));
    p.setPen(QPen(QColor(240, 240, 240), 2));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(loupeRect);

    p.setPen(QColor(230, 230, 230));
    const bool labelBelow = loupeRect.bottom() + 20 <= height();
    const QRectF labelRect =
        labelBelow ? QRectF(loupeRect.left(), loupeRect.bottom() + 2,
                            loupeRect.width(), 16)
                   : QRectF(loupeRect.left(), loupeRect.top() - 18,
                            loupeRect.width(), 16);
    p.drawText(labelRect, Qt::AlignHCenter | Qt::AlignVCenter,
               QString::number(m_loupeZoom, 'g', 2) + QStringLiteral("x"));
}

void ImageCanvas::mousePressEvent(QMouseEvent *e)
{
    m_cursor = e->position();
    if (e->button() == Qt::LeftButton && m_page) {
        m_dragIndex = handleAt(e->position());
        update();
    }
}

void ImageCanvas::mouseMoveEvent(QMouseEvent *e)
{
    m_cursor = e->position();
    if (m_dragIndex >= 0 && m_page) {
        QPointF ip = widgetToImage(e->position());
        ip.setX(std::clamp(ip.x(), 0.0, double(m_image.width())));
        ip.setY(std::clamp(ip.y(), 0.0, double(m_image.height())));
        m_page->points[m_dragIndex] = ip;
        m_page->marked = true;
        emit pointsChanged();
    }
    update();
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_dragIndex >= 0) {
        m_dragIndex = -1;
        update();
    }
}
