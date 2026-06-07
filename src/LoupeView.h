#pragma once

#include <QImage>
#include <QPointF>
#include <QWidget>

struct Page;
class QSlider;

// A magnifier shown above the preview pane: a square, zoomed view centred on
// the corner point currently selected in the ImageCanvas. Unlike the in-canvas
// loupe it replaced, it sits above the preview so it never covers the page being
// marked, and is kept square (its height tracks its width) so the magnified view
// fills the panel width. It overlays the same boundary lines the canvas draws
// (marked quad, spine, export inset), magnified, so corners can be placed
// precisely against them. A slider along the bottom sets the magnification factor.
class LoupeView : public QWidget {
    Q_OBJECT
public:
    explicit LoupeView(QWidget *parent = nullptr);

    void setImage(const QImage &image); // the rotated source the loupe reads from
    void setPage(const Page *page);     // page whose lines to overlay (or nullptr)
    void setExportInset(int px);        // inward export offset to overlay (px)
    void setGrayscale(bool on);         // magnify the source desaturated (overlays stay coloured)
    void setZoom(double zoom);          // also moves the slider to match
    void stepZoom(int direction);       // nudge magnification: +1 in, -1 out (+/- keys)
    void showPoint(const QPointF &imagePoint); // centre on this image-space point
    void clear();                              // no selection -> placeholder

    QSize sizeHint() const override; // initial size before the square aspect kicks in

signals:
    void zoomChanged(double zoom); // the slider set a new magnification factor

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override; // re-place the zoom slider

private:
    void drawOutline(QPainter &p, const QRectF &view, double srcD) const;
    int sliderStrip() const; // height reserved at the bottom for the zoom slider
    void rebuildGray();      // refresh the cached grayscale copy of m_image
    const QImage &shownImage() const; // m_image, or its grayscale copy when on

    QImage m_image;        // same rotated image the canvas shows
    QImage m_grayImage;    // cached grayscale copy of m_image (only when on)
    bool m_grayscale = false; // magnify the source desaturated
    const Page *m_page = nullptr; // page geometry to overlay (owned elsewhere)
    int m_inset = 0;       // export inset to overlay (px), 0 = none
    QPointF m_center;      // image-space point under the crosshair
    bool m_active = false; // a point is selected -> draw the magnified view
    double m_zoom = 2.0;
    QSlider *m_zoomSlider = nullptr; // sets m_zoom; pinned to the bottom edge
};
