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

    void setLoupeZoom(double zoom);
    double loupeZoom() const { return m_loupeZoom; }

signals:
    void pointsChanged(); // a handle was moved (page->marked is set true)

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    QRectF displayRect() const;   // where the image is drawn within the widget
    double scale() const;         // image->widget scale factor
    QPointF imageToWidget(const QPointF &p) const;
    QPointF widgetToImage(const QPointF &p) const;
    int handleAt(const QPointF &widgetPos) const;
    void drawQuad(QPainter &p, const QVector<int> &idx) const;
    void drawLoupe(QPainter &p) const;

    QImage m_image;
    Page *m_page = nullptr;
    int m_dragIndex = -1;
    double m_loupeZoom = 2.0;
};
