#pragma once

#include <QImage>

#include <opencv2/core.hpp>

// The single QImage <-> cv::Mat bridge. Both functions deal exclusively in
// RGB channel order (OpenCV's usual default is BGR, so callers must keep that in
// mind): a cv::Mat produced here is CV_8UC3 with R,G,B byte order, matching
// QImage::Format_RGB888. Each conversion returns an independent deep copy, so
// the result never aliases the argument's pixel buffer.
namespace ImageConv {

// QImage -> owning CV_8UC3 (RGB) Mat.
cv::Mat toMatRgb(const QImage &img);

// CV_8UC3 (RGB) Mat -> QImage (Format_RGB888).
QImage toQImageRgb(const cv::Mat &mat);

} // namespace ImageConv
