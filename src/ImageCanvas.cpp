#include "ImageCanvas.h"
#include "Page.h"

#include <QFont>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace {
constexpr double kHandleRadius = 7.0;
constexpr double kHitRadius = 16.0;
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
    m_dragging = false;
    m_activeIndex = (page && page->hasPoints()) ? 0 : -1; // auto-select corner 1
    emitLoupeTarget();
    update();
}

void ImageCanvas::clear()
{
    m_image = QImage();
    m_page = nullptr;
    m_activeIndex = -1;
    m_dragging = false;
    // No loupe signal here: tearing down the view is the owner's job (MainWindow
    // clears the loupe alongside the canvas). The signal only reports the active
    // corner changing while an image is loaded.
    update();
}

bool ImageCanvas::selectPoint(int index)
{
    if (!m_page || index < 0 || index >= m_page->points.size())
        return false;
    m_activeIndex = index;
    emitLoupeTarget();
    update();
    return true;
}

// Tell the LoupeView which corner (if any) is active so the magnifier tracks it.
void ImageCanvas::emitLoupeTarget()
{
    if (m_page && m_activeIndex >= 0 && m_activeIndex < m_page->points.size())
        emit loupeTargetChanged(m_page->points[m_activeIndex], true);
    else
        emit loupeTargetChanged(QPointF(), false);
}

void ImageCanvas::setNudgeSteps(double fine, double coarse, double large)
{
    m_nudgeFine = fine;
    m_nudgeCoarse = coarse;
    m_nudgeLarge = large;
}

void ImageCanvas::setExportInset(int px)
{
    m_exportInset = std::max(0, px);
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

void ImageCanvas::drawInsetOverlay(QPainter &p) const
{
    const QVector<QPointF> in =
        insetPoints(m_page->points, m_page->mode, m_exportInset);
    if (in.size() != m_page->points.size())
        return;

    auto mapPoly = [this, &in](std::initializer_list<int> idx) {
        QPolygonF poly;
        for (int i : idx)
            poly << imageToWidget(in[i]);
        return poly;
    };

    p.setPen(QPen(QColor(70, 200, 255), 1.5, Qt::DashLine)); // cyan
    p.setBrush(Qt::NoBrush);
    if (m_page->mode == Page::Four) {
        p.drawPolygon(mapPoly({0, 1, 2, 3}));
    } else {
        p.drawPolygon(mapPoly({0, 1, 4, 5})); // left
        p.drawPolygon(mapPoly({1, 2, 3, 4})); // right
    }
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
        QPen quadPen(QColor(60, 200, 120), 2, Qt::DashLine);
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

        // The inset boundary that will actually be exported (dashed, on top of
        // the marked quad).
        if (m_exportInset > 0)
            drawInsetOverlay(p);

        // Handles, each labelled with its 1-based number (for keyboard select).
        p.save();
        QFont labelFont = p.font();
        labelFont.setPixelSize(10);
        labelFont.setBold(true);
        p.setFont(labelFont);
        for (int i = 0; i < m_page->points.size(); ++i) {
            const QPointF w = imageToWidget(m_page->points[i]);
            const bool spine = (m_page->mode == Page::Six && (i == 1 || i == 4));
            QColor fill = (i == m_activeIndex) ? QColor(255, 255, 255)
                          : spine              ? QColor(255, 200, 60)
                                               : QColor(60, 200, 120);
            p.setPen(QPen(QColor(20, 20, 20), 1.5));
            p.setBrush(fill);
            p.drawEllipse(w, kHandleRadius, kHandleRadius);

            p.setPen(QColor(20, 20, 20));
            p.drawText(QRectF(w.x() - kHandleRadius, w.y() - kHandleRadius,
                              2 * kHandleRadius, 2 * kHandleRadius),
                       Qt::AlignCenter, QString::number(i + 1));
        }
        p.restore();
    }
}

QPointF ImageCanvas::clampToImage(const QPointF &imagePt) const
{
    return QPointF(std::clamp(imagePt.x(), 0.0, double(m_image.width())),
                   std::clamp(imagePt.y(), 0.0, double(m_image.height())));
}

// Move the selected point to `imagePt` (clamped to the image) and notify.
// Requires a valid m_activeIndex on the current page.
void ImageCanvas::moveActivePoint(const QPointF &imagePt)
{
    m_page->points[m_activeIndex] = clampToImage(imagePt);
    m_page->marked = true;
    emit pointsChanged();
    emitLoupeTarget(); // loupe follows the corner as it moves
    update();
}

void ImageCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_page) {
        const int hit = handleAt(e->position());
        if (hit >= 0) {
            m_activeIndex = hit; // also becomes the keyboard selection
            m_dragging = true;
            emitLoupeTarget();
        }
        update();
    }
}

void ImageCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging && m_page)
        moveActivePoint(widgetToImage(e->position()));
}

void ImageCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false; // keep m_activeIndex selected so the loupe stays up
        update();
    }
}

void ImageCanvas::keyPressEvent(QKeyEvent *e)
{
    if (m_page && m_page->hasPoints()) {
        // Digit 1..N selects the matching corner point.
        if (e->key() >= Qt::Key_1 && e->key() <= Qt::Key_9
            && selectPoint(e->key() - Qt::Key_1)) {
            e->accept();
            return;
        }

        // Esc clears the selection (and hides the loupe).
        if (e->key() == Qt::Key_Escape && m_activeIndex >= 0) {
            m_activeIndex = -1;
            emitLoupeTarget();
            update();
            e->accept();
            return;
        }

        // Arrow keys nudge the selected point. Holding two at once (e.g. Up+Left)
        // moves diagonally; Shift / Ctrl+Shift give larger steps.
        if (m_activeIndex >= 0 && m_activeIndex < m_page->points.size()) {
            switch (e->key()) {
            case Qt::Key_Left:
            case Qt::Key_Right:
            case Qt::Key_Up:
            case Qt::Key_Down:
                m_heldArrows.insert(e->key());
                nudgeByHeldArrows(e->modifiers());
                e->accept();
                return;
            default:
                break;
            }
        }
    }
    QWidget::keyPressEvent(e);
}

// Move the active point by the combined direction of all currently-held arrow
// keys, so pressing e.g. Up+Left together nudges diagonally.
void ImageCanvas::nudgeByHeldArrows(Qt::KeyboardModifiers mods)
{
    if (!m_page || m_activeIndex < 0 || m_activeIndex >= m_page->points.size())
        return;
    double dx = 0.0, dy = 0.0;
    if (m_heldArrows.contains(Qt::Key_Left))  dx -= 1.0;
    if (m_heldArrows.contains(Qt::Key_Right)) dx += 1.0;
    if (m_heldArrows.contains(Qt::Key_Up))    dy -= 1.0;
    if (m_heldArrows.contains(Qt::Key_Down))  dy += 1.0;
    if (dx == 0.0 && dy == 0.0)
        return;
    const double step =
        (mods & Qt::ShiftModifier) && (mods & Qt::ControlModifier) ? m_nudgeLarge
        : (mods & Qt::ShiftModifier)                               ? m_nudgeCoarse
                                                                   : m_nudgeFine;
    moveActivePoint(m_page->points[m_activeIndex] + QPointF(dx * step, dy * step));
}

void ImageCanvas::keyReleaseEvent(QKeyEvent *e)
{
    // Ignore the synthetic release that precedes an auto-repeat (the key is
    // still held); only a genuine release stops tracking that arrow.
    if (!e->isAutoRepeat()) {
        switch (e->key()) {
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
            m_heldArrows.remove(e->key());
            break;
        default:
            break;
        }
    }
    QWidget::keyReleaseEvent(e);
}

void ImageCanvas::focusOutEvent(QFocusEvent *e)
{
    m_heldArrows.clear(); // don't let a key stay "held" if released off-widget
    QWidget::focusOutEvent(e);
}

// Repurpose Tab / Shift+Tab to step the selection through the corner points
// instead of moving focus away from the canvas.
bool ImageCanvas::focusNextPrevChild(bool next)
{
    if (m_page && m_page->hasPoints()) {
        const int n = m_page->points.size();
        const int idx = m_activeIndex < 0 ? (next ? 0 : n - 1)
                                          : (m_activeIndex + (next ? 1 : -1) + n) % n;
        selectPoint(idx);
        return true; // consume so focus stays on the canvas
    }
    return QWidget::focusNextPrevChild(next);
}
