#pragma once

#include <QString>
#include <QVector>
#include <QPointF>

#include <opencv2/core.hpp>

#include <onnxruntime_cxx_api.h>

#include <memory>
#include <string>
#include <vector>

// Auto-detects book-page outlines in an (already upright) image using the
// fine-tuned YOLO11-seg model. Inference runs on ONNX Runtime (the system
// OpenCV 4.6 dnn is too old to parse a YOLO11 graph); OpenCV is still used for
// image ops and mask post-processing.
//
// The model only ever saw upright pages, so the input must already be rotated
// upright (i.e. MainWindow's m_currentRotated). Returned corners are therefore
// in that same rotated-image pixel space -- which is exactly Page::points'
// coordinate space, so no further transform is needed by the caller.
class PageDetector {
public:
    PageDetector();

    bool ok() const { return m_ok; }
    QString modelPath() const { return m_modelPath; }

    // One detected page: corners ordered TL, TR, BR, BL (input-image pixels).
    struct Quad { QPointF tl, tr, br, bl; };

    // Run detection on an RGB image (CV_8UC3, RGB channel order). Returns one
    // quad per detected page, sorted left -> right by centroid x.
    QVector<Quad> detect(const cv::Mat &rgb) const;

private:
    static QString locateModel();

    QString m_modelPath;
    bool m_ok = false;

    Ort::Env m_env;                          // must outlive the session
    std::unique_ptr<Ort::Session> m_session;
    std::string m_inputName;                 // owned (kept alive for Run())
    std::vector<std::string> m_outputNames;
};
