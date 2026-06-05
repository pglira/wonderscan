#include "Warp.h"

#include <QTransform>
#include <algorithm>
#include <cmath>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace {

cv::Mat qimageToMatRGB(const QImage &in)
{
    QImage img = in.convertToFormat(QImage::Format_RGB888);
    cv::Mat view(img.height(), img.width(), CV_8UC3,
                 const_cast<uchar *>(img.bits()),
                 static_cast<size_t>(img.bytesPerLine()));
    return view.clone(); // own contiguous copy
}

QImage matToQImageRGB(const cv::Mat &mat)
{
    // mat is CV_8UC3, RGB order.
    QImage img(mat.data, mat.cols, mat.rows,
               static_cast<int>(mat.step), QImage::Format_RGB888);
    return img.copy();
}

double dist(const QPointF &a, const QPointF &b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

// Dewarp a single quad given in TL, TR, BR, BL order.
cv::Mat dewarpQuad(const cv::Mat &src,
                   const QPointF &tl, const QPointF &tr,
                   const QPointF &br, const QPointF &bl)
{
    int outW = static_cast<int>(std::lround(std::max(dist(tl, tr), dist(bl, br))));
    int outH = static_cast<int>(std::lround(std::max(dist(tl, bl), dist(tr, br))));
    outW = std::max(outW, 1);
    outH = std::max(outH, 1);

    cv::Point2f srcPts[4] = {
        {float(tl.x()), float(tl.y())},
        {float(tr.x()), float(tr.y())},
        {float(br.x()), float(br.y())},
        {float(bl.x()), float(bl.y())},
    };
    cv::Point2f dstPts[4] = {
        {0.f, 0.f},
        {float(outW - 1), 0.f},
        {float(outW - 1), float(outH - 1)},
        {0.f, float(outH - 1)},
    };

    cv::Mat M = cv::getPerspectiveTransform(srcPts, dstPts);
    cv::Mat out;
    cv::warpPerspective(src, out, M, cv::Size(outW, outH),
                        cv::INTER_CUBIC, cv::BORDER_CONSTANT,
                        cv::Scalar(255, 255, 255));
    return out;
}

// Bilinear "ruled" resampling of a quad to an outW x outH image. The four
// source points map to output corners (0,0), (W,0), (W,H), (0,H). Every output
// row is a linear blend between the left edge (tl->bl) and right edge (tr->br)
// at the same row parameter t, then sampled linearly across.
//
// Key property: because the left/right edges are sampled by the SAME row
// parameter t in every call, two halves that share an edge (the book spine) and
// use the same output height stay pixel-aligned along that edge -- so the
// spread joins continuously across the seam (with at most a slope change).
cv::Mat ruledWarp(const cv::Mat &src,
                  const QPointF &tl, const QPointF &tr,
                  const QPointF &br, const QPointF &bl,
                  int outW, int outH)
{
    outW = std::max(outW, 1);
    outH = std::max(outH, 1);
    cv::Mat mapx(outH, outW, CV_32FC1);
    cv::Mat mapy(outH, outW, CV_32FC1);

    for (int y = 0; y < outH; ++y) {
        const double t = (outH > 1) ? double(y) / (outH - 1) : 0.0;
        const double lx = tl.x() + t * (bl.x() - tl.x());
        const double ly = tl.y() + t * (bl.y() - tl.y());
        const double rx = tr.x() + t * (br.x() - tr.x());
        const double ry = tr.y() + t * (br.y() - tr.y());
        float *mx = mapx.ptr<float>(y);
        float *my = mapy.ptr<float>(y);
        for (int x = 0; x < outW; ++x) {
            const double s = (outW > 1) ? double(x) / (outW - 1) : 0.0;
            mx[x] = float(lx + s * (rx - lx));
            my[x] = float(ly + s * (ry - ly));
        }
    }

    cv::Mat out;
    cv::remap(src, out, mapx, mapy, cv::INTER_CUBIC,
              cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));
    return out;
}

} // namespace

namespace Warp {

QImage applyRotation(const QImage &src, int rotation)
{
    int r = ((rotation % 360) + 360) % 360;
    if (r == 0)
        return src;
    QTransform t;
    t.rotate(r);
    return src.transformed(t, Qt::SmoothTransformation);
}

QVector<QImage> renderPages(const QImage &rotated,
                            const QVector<QPointF> &points,
                            Page::Mode mode,
                            bool splitSpread)
{
    const int expected = (mode == Page::Four) ? 4 : 6;
    if (points.size() != expected || rotated.isNull())
        return {};

    cv::Mat src = qimageToMatRGB(rotated);

    if (mode == Page::Four) {
        cv::Mat page = dewarpQuad(src, points[0], points[1], points[2], points[3]);
        return { matToQImageRGB(page) };
    }

    // Six-point spread. Points: [TL, TopSpine, TR, BR, BottomSpine, BL].
    const QPointF tl = points[0], topS = points[1], tr = points[2];
    const QPointF br = points[3], botS = points[4], bl = points[5];

    // A single shared output height makes the two halves' rows line up, so the
    // spine column is sampled identically on both sides (continuous seam).
    const int H = std::max(1, int(std::lround(
        std::max({dist(tl, bl), dist(topS, botS), dist(tr, br)}))));
    const int WL = std::max(1, int(std::lround(
        std::max(dist(tl, topS), dist(bl, botS)))));
    const int WR = std::max(1, int(std::lround(
        std::max(dist(topS, tr), dist(botS, br)))));

    // Left half: the spine is its right edge. Right half: the spine is its left
    // edge. Both share topS/botS and the same H, so the seam matches exactly.
    cv::Mat left  = ruledWarp(src, tl, topS, botS, bl, WL, H);
    cv::Mat right = ruledWarp(src, topS, tr, br, botS, WR, H);

    if (splitSpread)
        return { matToQImageRGB(left), matToQImageRGB(right) };

    cv::Mat spread;
    cv::hconcat(left, right, spread); // equal height -> continuous across seam
    return { matToQImageRGB(spread) };
}

} // namespace Warp
