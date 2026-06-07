#include "LoupeView.h"

#include "Page.h"

#include <QAbstractSlider>
#include <QPainter>
#include <QPolygonF>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace {
constexpr int kDefaultSide = 220; // preferred square side (drag the splitter to resize)
constexpr int kMinSide = 90;      // keep the loupe usable when dragged small
constexpr int kMargin = 8;
constexpr double kZoomMin = 0.2;  // slider range (x); below 1.0 zooms out for context
constexpr double kZoomMax = 5.0;
constexpr int kZoomScale = 10;    // slider works in tenths of x (0.2..5.0 by 0.1)

int zoomToSlider(double z) { return int(std::lround(z * kZoomScale)); }
} // namespace

LoupeView::LoupeView(QWidget *parent) : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(45, 45, 45));
    setPalette(pal);

    // Magnification slider pinned to the bottom edge (positioned in resizeEvent).
    m_zoomSlider = new QSlider(Qt::Horizontal, this);
    m_zoomSlider->setRange(zoomToSlider(kZoomMin), zoomToSlider(kZoomMax));
    m_zoomSlider->setSingleStep(1); // 0.1x per +/- key step (see stepZoom)
    m_zoomSlider->setPageStep(kZoomScale / 2); // 0.5x
    // Don't take keyboard focus: dragging it or pressing +/- must not steal focus
    // from the canvas, so h/j/k/l and the arrow keys keep nudging the corner.
    m_zoomSlider->setFocusPolicy(Qt::NoFocus);
    m_zoomSlider->setValue(zoomToSlider(m_zoom));
    m_zoomSlider->setToolTip(tr("Loupe magnification"));
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
        const double z = double(v) / kZoomScale;
        if (qFuzzyCompare(z, m_zoom))
            return;
        m_zoom = z; // set directly; setZoom() would move the slider right back
        emit zoomChanged(m_zoom);
        update();
    });

    // A sane floor for the very first layout; thereafter resizeEvent pins the
    // height to the width so the magnified view stays square (see resizeEvent and
    // paintEvent). Width follows the column.
    setMinimumHeight(kMinSide + 2 * kMargin + sliderStrip());
}

QSize LoupeView::sizeHint() const
{
    const int h = kDefaultSide + 2 * kMargin + sliderStrip();
    return QSize(kDefaultSide + 2 * kMargin, h);
}

// Space reserved at the bottom for the slider: its natural height plus a gap.
int LoupeView::sliderStrip() const
{
    return m_zoomSlider->sizeHint().height() + kMargin;
}

void LoupeView::resizeEvent(QResizeEvent *e)
{
    // Keep the magnified view square: make the widget's height track its width
    // (plus the slider strip) so the square fills the panel width instead of
    // leaving dark bars at the sides. Guarded so it doesn't recurse on resize.
    const int square = width() + sliderStrip();
    if (maximumHeight() != square)
        setFixedHeight(square);

    const int sh = m_zoomSlider->sizeHint().height();
    m_zoomSlider->setGeometry(kMargin, height() - kMargin - sh,
                              std::max(0, width() - 2 * kMargin), sh);
    QWidget::resizeEvent(e);
}

void LoupeView::setImage(const QImage &image)
{
    m_image = image;
    rebuildGray();
    update();
}

void LoupeView::setGrayscale(bool on)
{
    if (m_grayscale == on)
        return;
    m_grayscale = on;
    rebuildGray();
    update();
}

// Cache a desaturated copy so the frequent loupe repaints during a drag don't
// reconvert the full-res source each time. Dropped when grayscale is off.
void LoupeView::rebuildGray()
{
    m_grayImage = (m_grayscale && !m_image.isNull())
                      ? m_image.convertToFormat(QImage::Format_Grayscale8)
                      : QImage();
}

const QImage &LoupeView::shownImage() const
{
    return (m_grayscale && !m_grayImage.isNull()) ? m_grayImage : m_image;
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
    m_zoom = std::clamp(zoom, kZoomMin, kZoomMax);
    if (m_zoomSlider) {
        const QSignalBlocker block(m_zoomSlider); // syncing UI, not a user change
        m_zoomSlider->setValue(zoomToSlider(m_zoom));
    }
    update();
}

// Step the slider by one single step (0.1x), which clamps to range and -- via
// the slider's valueChanged -- updates the zoom, repaints, and persists it.
void LoupeView::stepZoom(int direction)
{
    if (!m_zoomSlider)
        return;
    m_zoomSlider->triggerAction(direction >= 0
                                    ? QAbstractSlider::SliderSingleStepAdd
                                    : QAbstractSlider::SliderSingleStepSub);
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
    // Antialiasing keeps the (often diagonal) overlay lines and the crosshair an
    // even width everywhere; without it they rasterise unevenly. SmoothPixmap
    // gives the magnified image clean scaling.
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Square viewport filling the area above the slider strip (minus the
    // margin), centred in it. Grows/shrinks as the splitter resizes the loupe.
    // Integer-aligned so the black fill and white border stay pixel-crisp.
    const int availH = height() - 2 * kMargin - sliderStrip();
    const int side = std::min(width() - 2 * kMargin, availH);
    if (side <= 0)
        return;
    const int left = (width() - side) / 2;
    const int top = kMargin + (availH - side) / 2;
    const QRectF view(left, top, side, side);
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
    p.drawImage(view, shownImage(), srcRect);

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

    // Marked quad(s) (dashed magenta) + spine (dashed yellow), matching the canvas.
    p.setPen(QPen(QColor(220, 60, 220), 1.5, Qt::DashLine));
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
            p.setPen(QPen(QColor(60, 220, 220), 1.5, Qt::DashLine));
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
