#include "PageDetector.h"

#include <QCoreApplication>
#include <QFileInfo>

#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp> // blobFromImage + NMSBoxes (work fine in OpenCV 4.6)

#include <algorithm>
#include <array>
#include <cmath>

namespace {

constexpr int   kInput    = 1024;  // model input side (square, letterboxed)
constexpr float kConf     = 0.45f; // class-score threshold
constexpr float kIoU      = 0.50f; // NMS IoU
constexpr float kMaskThr  = 0.5f;  // mask binarisation
constexpr float kContain  = 0.70f; // drop a box this-fraction nested in another

// Letterbox into kInput x kInput, returning the scale gain and pad offsets so
// detections can be mapped back to the original image.
cv::Mat letterbox(const cv::Mat &src, double &gain, int &padX, int &padY)
{
    gain = std::min(double(kInput) / src.rows, double(kInput) / src.cols);
    int nw = int(std::round(src.cols * gain));
    int nh = int(std::round(src.rows * gain));
    cv::Mat resized;
    cv::resize(src, resized, cv::Size(nw, nh), 0, 0, cv::INTER_LINEAR);
    cv::Mat canvas(kInput, kInput, src.type(), cv::Scalar(114, 114, 114));
    padX = (kInput - nw) / 2;
    padY = (kInput - nh) / 2;
    resized.copyTo(canvas(cv::Rect(padX, padY, nw, nh)));
    return canvas;
}

} // namespace

PageDetector::PageDetector()
    : m_env(ORT_LOGGING_LEVEL_WARNING, "wonderscan")
{
    m_modelPath = locateModel();
    if (m_modelPath.isEmpty())
        return;
    try {
        Ort::SessionOptions opts;
        opts.SetIntraOpNumThreads(4);
        opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        m_session = std::make_unique<Ort::Session>(
            m_env, m_modelPath.toStdString().c_str(), opts);

        Ort::AllocatorWithDefaultOptions alloc;
        m_inputName = m_session->GetInputNameAllocated(0, alloc).get();
        for (size_t i = 0; i < m_session->GetOutputCount(); ++i)
            m_outputNames.push_back(m_session->GetOutputNameAllocated(i, alloc).get());
        m_ok = (m_session->GetOutputCount() == 2);
    } catch (const Ort::Exception &) {
        m_ok = false;
    }
}

QString PageDetector::locateModel()
{
    QStringList candidates;
#ifdef WONDERSCAN_MODEL_INSTALL_PATH
    candidates << QStringLiteral(WONDERSCAN_MODEL_INSTALL_PATH);
#endif
    const QString exeDir = QCoreApplication::applicationDirPath();
    candidates << exeDir + QStringLiteral("/page-seg.onnx");
    candidates << exeDir + QStringLiteral("/../share/wonderscan/page-seg.onnx");
#ifdef WONDERSCAN_MODEL_DEV_PATH
    candidates << QStringLiteral(WONDERSCAN_MODEL_DEV_PATH); // running from build tree
#endif
    for (const QString &c : candidates)
        if (QFileInfo::exists(c))
            return c;
    return QString();
}

QVector<PageDetector::Quad> PageDetector::detect(const cv::Mat &rgb) const
{
    QVector<Quad> result;
    if (!m_ok || rgb.empty())
        return result;

    const int W = rgb.cols, H = rgb.rows;
    double gain; int padX, padY;
    cv::Mat lb = letterbox(rgb, gain, padX, padY);

    // `rgb` is already in RGB channel order (built by ImageConv::toMatRgb), so
    // tell blobFromImage not to swap channels.
    cv::Mat blob = cv::dnn::blobFromImage(lb, 1.0 / 255.0, cv::Size(kInput, kInput),
                                          cv::Scalar(), /*swapRB=*/false, /*crop=*/false);

    // ONNX Runtime inference (the system OpenCV 4.6 dnn can't parse a YOLO11
    // graph). OpenCV is still used for the blob and all post-processing.
    const std::array<int64_t, 4> inShape{1, 3, kInput, kInput};
    Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value input = Ort::Value::CreateTensor<float>(
        mem, blob.ptr<float>(), blob.total(), inShape.data(), inShape.size());

    const char *inNames[] = {m_inputName.c_str()};
    std::vector<const char *> outNames;
    outNames.reserve(m_outputNames.size());
    for (const std::string &n : m_outputNames)
        outNames.push_back(n.c_str());

    std::vector<Ort::Value> outs;
    try {
        outs = m_session->Run(Ort::RunOptions{nullptr}, inNames, &input, 1,
                              outNames.data(), outNames.size());
    } catch (const Ort::Exception &) {
        return result;
    }

    // Identify outputs by shape: o0 [1,37,N] (4 box + 1 cls + 32 coeff),
    // o1 [1,32,Ph,Pw].
    float *p0 = nullptr, *p1 = nullptr;
    int nAnchors = 0, protoH = 0, protoW = 0;
    for (Ort::Value &o : outs) {
        const std::vector<int64_t> s = o.GetTensorTypeAndShapeInfo().GetShape();
        if (s.size() == 3 && s[1] == 37) {
            p0 = o.GetTensorMutableData<float>();
            nAnchors = int(s[2]);
        } else if (s.size() == 4 && s[1] == 32) {
            p1 = o.GetTensorMutableData<float>();
            protoH = int(s[2]);
            protoW = int(s[3]);
        }
    }
    if (!p0 || !p1)
        return result;

    // [1,37,N] viewed as 37 x N, transposed to N x 37 for per-anchor rows.
    cv::Mat det(37, nAnchors, CV_32F, p0);
    cv::Mat detT;
    cv::transpose(det, detT); // N x 37
    cv::Mat protos(32, protoH * protoW, CV_32F, p1);

    std::vector<cv::Rect> boxes;       // letterbox px
    std::vector<float> scores;
    std::vector<int> rows;             // anchor index into detT
    boxes.reserve(256); scores.reserve(256); rows.reserve(256);
    for (int a = 0; a < nAnchors; ++a) {
        const float *r = detT.ptr<float>(a);
        const float score = r[4];
        if (score < kConf)
            continue;
        const float cx = r[0], cy = r[1], w = r[2], h = r[3];
        boxes.emplace_back(int(cx - w / 2), int(cy - h / 2), int(w), int(h));
        scores.push_back(score);
        rows.push_back(a);
    }
    if (boxes.empty())
        return result;

    std::vector<int> nms;
    cv::dnn::NMSBoxes(boxes, scores, kConf, kIoU, nms);
    if (nms.empty())
        return result;

    // Containment suppression: highest score first; drop boxes mostly nested in
    // an already-kept one (removes 'whole spread + its two halves' triples).
    std::sort(nms.begin(), nms.end(),
              [&](int a, int b) { return scores[a] > scores[b]; });
    std::vector<int> kept;
    for (int i : nms) {
        const cv::Rect &b = boxes[i];
        bool nested = false;
        for (int k : kept) {
            const cv::Rect inter = b & boxes[k];
            if (b.area() > 0 && double(inter.area()) / b.area() > kContain) {
                nested = true;
                break;
            }
        }
        if (!nested)
            kept.push_back(i);
    }

    struct Cand { Quad quad; double cx; };
    std::vector<Cand> cands;
    const double protoScale = double(protoW) / kInput; // 256/1024
    for (int i : kept) {
        // mask = sigmoid(coeffs * protos)
        cv::Mat coeffs = detT.row(rows[i]).colRange(5, 37); // 1x32
        cv::Mat mflat = coeffs * protos;                    // 1 x (Ph*Pw)
        cv::Mat negexp;
        cv::exp(-mflat, negexp);
        cv::Mat m = 1.0 / (1.0 + negexp);
        cv::Mat mask = m.reshape(1, protoH);                // Ph x Pw, 0..1

        // restrict to the detection box (in proto coords)
        const cv::Rect &b = boxes[i];
        int x0 = std::max(0, int(b.x * protoScale));
        int y0 = std::max(0, int(b.y * protoScale));
        int x1 = std::min(protoW, int((b.x + b.width) * protoScale));
        int y1 = std::min(protoH, int((b.y + b.height) * protoScale));
        if (x1 <= x0 || y1 <= y0)
            continue;
        cv::Mat bin = cv::Mat::zeros(protoH, protoW, CV_8U);
        cv::Mat roi = mask(cv::Rect(x0, y0, x1 - x0, y1 - y0)) > kMaskThr;
        roi.copyTo(bin(cv::Rect(x0, y0, x1 - x0, y1 - y0)));

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(bin, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        if (contours.empty())
            continue;
        const auto &c = *std::max_element(
            contours.begin(), contours.end(),
            [](const auto &a, const auto &b) { return cv::contourArea(a) < cv::contourArea(b); });
        if (c.size() < 4)
            continue;

        // 4 extreme corners (pages are ~upright): TL=min(x+y) TR=max(x-y)
        // BR=max(x+y) BL=min(x-y), in proto coords.
        cv::Point tl = c[0], tr = c[0], br = c[0], bl = c[0];
        for (const cv::Point &p : c) {
            if (p.x + p.y < tl.x + tl.y) tl = p;
            if (p.x + p.y > br.x + br.y) br = p;
            if (p.x - p.y > tr.x - tr.y) tr = p;
            if (p.x - p.y < bl.x - bl.y) bl = p;
        }
        auto toOrig = [&](const cv::Point &p) {
            double X = (p.x * (double(kInput) / protoW) - padX) / gain;
            double Y = (p.y * (double(kInput) / protoH) - padY) / gain;
            X = std::clamp(X, 0.0, double(W - 1));
            Y = std::clamp(Y, 0.0, double(H - 1));
            return QPointF(X, Y);
        };
        Quad q{toOrig(tl), toOrig(tr), toOrig(br), toOrig(bl)};
        const double cx = (q.tl.x() + q.tr.x() + q.br.x() + q.bl.x()) / 4.0;
        cands.push_back({q, cx});
    }

    std::sort(cands.begin(), cands.end(),
              [](const Cand &a, const Cand &b) { return a.cx < b.cx; });
    for (const Cand &c : cands)
        result.push_back(c.quad);
    return result;
}
