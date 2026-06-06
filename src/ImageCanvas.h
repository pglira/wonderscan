#pragma once

#include <QWidget>
#include <QImage>

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

signals:
    void pointsChanged(); // a handle was moved (page->marked is set true)

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    QRectF displayRect() const;   // where the image is drawn within the widget
    double scale() const;         // image->widget scale factor
    QPointF imageToWidget(const QPointF &p) const;
    QPointF widgetToImage(const QPointF &p) const;
    int handleAt(const QPointF &widgetPos) const;
    QPointF clampToImage(const QPointF &imagePt) const;
    void moveActivePoint(const QPointF &imagePt); // clamp, store, mark, emit, repaint
    void drawQuad(QPainter &p, const QVector<int> &idx) const;
    void drawLoupe(QPainter &p) const;

    QImage m_image;
    Page *m_page = nullptr;
    int m_activeIndex = -1;  // selected/highlighted point (mouse or keyboard), or -1
    bool m_dragging = false; // left button is currently dragging m_activeIndex
    double m_loupeZoom = 2.0;
};
