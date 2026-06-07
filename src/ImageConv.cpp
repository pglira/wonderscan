#include "ImageConv.h"

namespace ImageConv {

cv::Mat toMatRgb(const QImage &img)
{
    const QImage rgb = img.convertToFormat(QImage::Format_RGB888);
    cv::Mat view(rgb.height(), rgb.width(), CV_8UC3,
                 const_cast<uchar *>(rgb.bits()),
                 static_cast<size_t>(rgb.bytesPerLine()));
    return view.clone(); // own a contiguous copy (rgb goes out of scope)
}

QImage toQImageRgb(const cv::Mat &mat)
{
    // mat is CV_8UC3 in RGB order.
    QImage img(mat.data, mat.cols, mat.rows,
               static_cast<int>(mat.step), QImage::Format_RGB888);
    return img.copy(); // detach from mat's buffer
}

} // namespace ImageConv
