#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

// A fixed magnifier shown above the preview pane: a square, zoomed view centred
// on the corner point currently selected in the ImageCanvas. Unlike the
// in-canvas loupe it replaced, it sits in a fixed spot so it never covers the
// page being marked.
class LoupeView : public QWidget {
    Q_OBJECT
public:
    explicit LoupeView(QWidget *parent = nullptr);

    void setImage(const QImage &image); // the rotated source the loupe reads from
    void setZoom(double zoom);
    void showPoint(const QPointF &imagePoint); // centre on this image-space point
    void clear();                              // no selection -> placeholder

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QImage m_image;       // same rotated image the canvas shows
    QPointF m_center;     // image-space point under the crosshair
    bool m_active = false; // a point is selected -> draw the magnified view
    double m_zoom = 2.0;
};
