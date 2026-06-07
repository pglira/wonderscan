#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

struct Page;

// A fixed magnifier shown above the preview pane: a square, zoomed view centred
// on the corner point currently selected in the ImageCanvas. Unlike the
// in-canvas loupe it replaced, it sits in a fixed spot so it never covers the
// page being marked. It overlays the same boundary lines the canvas draws
// (marked quad, spine, export inset), magnified, so corners can be placed
// precisely against them.
class LoupeView : public QWidget {
    Q_OBJECT
public:
    explicit LoupeView(QWidget *parent = nullptr);

    void setImage(const QImage &image); // the rotated source the loupe reads from
    void setPage(const Page *page);     // page whose lines to overlay (or nullptr)
    void setExportInset(int px);        // inward export offset to overlay (px)
    void setZoom(double zoom);
    void showPoint(const QPointF &imagePoint); // centre on this image-space point
    void clear();                              // no selection -> placeholder

protected:
    void paintEvent(QPaintEvent *) override;

private:
    void drawOutline(QPainter &p, const QRectF &view, double srcD) const;

    QImage m_image;        // same rotated image the canvas shows
    const Page *m_page = nullptr; // page geometry to overlay (owned elsewhere)
    int m_inset = 0;       // export inset to overlay (px), 0 = none
    QPointF m_center;      // image-space point under the crosshair
    bool m_active = false; // a point is selected -> draw the magnified view
    double m_zoom = 2.0;
};
