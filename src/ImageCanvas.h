#pragma once

#include <QWidget>
#include <QImage>
#include <QSet>

struct Page;

// Displays the rotated source fit-to-window, draws the page quad(s) with
// draggable corner handles, and shows an adjustable-zoom loupe while dragging.
class ImageCanvas : public QWidget {
    Q_OBJECT
public:
    explicit ImageCanvas(QWidget *parent = nullptr);

    // `page` is owned by the caller and must outlive the assignment / next call.
    void setImage(const QImage &rotated, Page *page);
    void clear();

    // Select the corner point at `index` (0-based) for keyboard editing, which
    // highlights it and shows the loupe. Returns false if no such point exists.
    bool selectPoint(int index);

    void setLoupeZoom(double zoom);
    double loupeZoom() const { return m_loupeZoom; }

    // Arrow-key nudge distances (image px): plain / +Shift / +Ctrl+Shift.
    void setNudgeSteps(double fine, double coarse, double large);

signals:
    void pointsChanged(); // a handle was moved (page->marked is set true)

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void focusOutEvent(QFocusEvent *) override;
    bool focusNextPrevChild(bool next) override; // Tab / Shift+Tab cycle corners

private:
    QRectF displayRect() const;   // where the image is drawn within the widget
    double scale() const;         // image->widget scale factor
    QPointF imageToWidget(const QPointF &p) const;
    QPointF widgetToImage(const QPointF &p) const;
    int handleAt(const QPointF &widgetPos) const;
    QPointF clampToImage(const QPointF &imagePt) const;
    void moveActivePoint(const QPointF &imagePt); // clamp, store, mark, emit, repaint
    void nudgeByHeldArrows(Qt::KeyboardModifiers mods); // combined-direction nudge
    void drawQuad(QPainter &p, const QVector<int> &idx) const;
    void drawLoupe(QPainter &p) const;

    QImage m_image;
    Page *m_page = nullptr;
    int m_activeIndex = -1;  // selected/highlighted point (mouse or keyboard), or -1
    bool m_dragging = false; // left button is currently dragging m_activeIndex
    double m_loupeZoom = 2.0;
    double m_nudgeFine = 1.0;    // arrow-key step (image px)
    double m_nudgeCoarse = 10.0; // ... with Shift
    double m_nudgeLarge = 25.0;  // ... with Ctrl+Shift
    QSet<int> m_heldArrows;      // arrow keys currently held (for diagonal nudging)
};
