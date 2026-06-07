#include "MainWindow.h"

#include "AppSettings.h"
#include "Document.h"
#include "ImageCanvas.h"
#include "ImageConv.h"
#include "LoupeView.h"
#include "PreviewPane.h"
#include "PdfExporter.h"
#include "Warp.h"
#include "PageDetector.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QProgressDialog>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <cmath>

namespace {

const QStringList kImageFilters = {
    "*.jpg", "*.jpeg", "*.png", "*.tif", "*.tiff", "*.bmp", "*.webp"
};

// Filmstrip icon size shown in the list. Base thumbnails (kept in Document) are
// rotated and scaled down to this size per item, so a rotation never re-reads
// the source file.
constexpr int kThumbW = 96;
constexpr int kThumbH = 120;

bool isSupportedImage(const QString &path)
{
    const QString lower = path.toLower();
    for (const QString &f : kImageFilters)
        if (lower.endsWith(f.mid(1)))
            return true;
    return false;
}

// A wonderscan project file, per the Open Project dialog filter (*.wsp *.json).
bool isProjectFile(const QString &path)
{
    return path.endsWith(".wsp", Qt::CaseInsensitive)
           || path.endsWith(".json", Qt::CaseInsensitive);
}

// Map detected page quads onto a Page per the count rule. Returns the number of
// pages applied (1 -> 4-point quad, 2 -> 6-point spread), or 0 if detection
// failed (0 or >2 pages) -- in which case the page is left untouched.
int applyQuadsToPage(Page &p, const QVector<PageDetector::Quad> &quads)
{
    const int n = quads.size();
    if (n == 1) {
        const PageDetector::Quad &q = quads[0];
        p.mode = Page::Four;
        p.points = { q.tl, q.tr, q.br, q.bl };
        p.marked = true;
        return 1;
    }
    if (n == 2) {
        const PageDetector::Quad &l = quads[0]; // left  (sorted by centroid x)
        const PageDetector::Quad &r = quads[1]; // right
        const QPointF topSpine = (l.tr + r.tl) / 2.0;
        const QPointF botSpine = (l.br + r.bl) / 2.0;
        p.mode = Page::Six;
        p.points = { l.tl, topSpine, r.tr, r.br, botSpine, l.bl };
        p.marked = true;
        return 2;
    }
    return 0;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setupUi();
    setupActions();
    loadSettings();
    setAcceptDrops(true);
    resize(1400, 900); // restore size; the window opens maximized (see main.cpp)
    updateTitle();
    syncControlsToCurrent(); // start with no current image -> controls disabled
    syncInsetControl();
    updateStatus();
}

MainWindow::~MainWindow()
{
    delete m_detector;
}

void MainWindow::setupUi()
{
    m_filmstrip = new QListWidget(this);
    // IconMode (single, non-wrapping column) so the selection highlight hugs the
    // thumbnail rather than spanning a full-width row — there is no longer a
    // filename beside the thumbnail to fill that width.
    m_filmstrip->setViewMode(QListView::IconMode);
    m_filmstrip->setFlow(QListView::TopToBottom);
    m_filmstrip->setWrapping(false);
    m_filmstrip->setIconSize(QSize(kThumbW, kThumbH));
    m_filmstrip->setMovement(QListView::Static);
    m_filmstrip->setResizeMode(QListView::Adjust);
    m_filmstrip->setUniformItemSizes(true);
    m_filmstrip->setSpacing(4);
    m_filmstrip->setMinimumWidth(124);
    m_filmstrip->setMaximumWidth(160);
    // Mark the selected thumbnail with a crisp frame tight to the image, rather
    // than relying on the theme's (here barely visible) IconMode tint.
    m_filmstrip->setStyleSheet(
        "QListWidget::item { border: 2px solid transparent; }"
        "QListWidget::item:selected { border-color: #4a90d9;"
        " background: rgba(74,144,217,45); }");
    m_filmstrip->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_filmstrip, &QListWidget::currentRowChanged,
            this, &MainWindow::onFilmstripRow);
    connect(m_filmstrip, &QListWidget::customContextMenuRequested,
            this, &MainWindow::onFilmstripContextMenu);

    m_canvas = new ImageCanvas(this);
    connect(m_canvas, &ImageCanvas::pointsChanged,
            this, &MainWindow::onPointsChanged);

    m_loupe = new LoupeView(this);
    connect(m_canvas, &ImageCanvas::loupeTargetChanged, this,
            [this](const QPointF &pt, bool active) {
                if (active)
                    m_loupe->showPoint(pt);
                else
                    m_loupe->clear();
            });
    // The loupe's own slider sets the magnification; remember the chosen value.
    connect(m_loupe, &LoupeView::zoomChanged, this, [this](double z) {
        m_prefs.loupeZoom = z;
        AppSettings::saveEditorPrefs(m_prefs);
    });
    // +/- adjust the loupe magnification from anywhere in the window. Bind both
    // '+' and '=' (the same physical key, with/without Shift) for zooming in.
    for (const auto key : {Qt::Key_Plus, Qt::Key_Equal}) {
        connect(new QShortcut(QKeySequence(key), this), &QShortcut::activated, this,
                [this] { m_loupe->stepZoom(+1); });
    }
    connect(new QShortcut(QKeySequence(Qt::Key_Minus), this), &QShortcut::activated,
            this, [this] { m_loupe->stepZoom(-1); });

    m_preview = new PreviewPane(this);

    // Right column: a square loupe on top (its height tracks its width, see
    // LoupeView) with the live preview filling the rest of the column.
    auto *rightCol = new QWidget(this);
    auto *rightLayout = new QVBoxLayout(rightCol);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(4);
    rightLayout->addWidget(m_loupe);
    rightLayout->addWidget(m_preview, 1);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_filmstrip);
    splitter->addWidget(m_canvas);
    splitter->addWidget(rightCol);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({130, 760, 480}); // wider preview/loupe panel on the right
    setCentralWidget(splitter);

    m_statusLabel = new QLabel(this);
    statusBar()->addWidget(m_statusLabel);
}

void MainWindow::setupActions()
{
    auto *fileMenu = menuBar()->addMenu(tr("&File"));
    auto *editMenu = menuBar()->addMenu(tr("&Edit"));
    auto *tb = addToolBar(tr("Main"));
    tb->setMovable(false);

    auto *actOpen = new QAction(tr("&Open Project..."), this);
    actOpen->setShortcut(QKeySequence::Open);
    connect(actOpen, &QAction::triggered, this, &MainWindow::openProject);

    auto *actSave = new QAction(tr("&Save Project"), this);
    actSave->setShortcut(QKeySequence::Save);
    connect(actSave, &QAction::triggered, this, [this] { saveProject(); });

    auto *actSaveAs = new QAction(tr("Save Project &As..."), this);
    actSaveAs->setShortcut(QKeySequence::SaveAs);
    connect(actSaveAs, &QAction::triggered, this, [this] { saveProjectAs(); });

    auto *actAddImages = new QAction(tr("Add &Images..."), this);
    connect(actAddImages, &QAction::triggered, this, &MainWindow::addImagesDialog);

    auto *actAddFolder = new QAction(tr("Add &Folder..."), this);
    connect(actAddFolder, &QAction::triggered, this, &MainWindow::addFolderDialog);

    auto *actExport = new QAction(tr("&Export PDF..."), this);
    actExport->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    connect(actExport, &QAction::triggered, this, &MainWindow::exportPdf);

    auto *actQuit = new QAction(tr("&Quit"), this);
    actQuit->setShortcut(QKeySequence::Quit);
    connect(actQuit, &QAction::triggered, this, &QWidget::close);

    fileMenu->addAction(actOpen);
    m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
    fileMenu->addAction(actSave);
    fileMenu->addAction(actSaveAs);
    fileMenu->addSeparator();
    fileMenu->addAction(actAddImages);
    fileMenu->addAction(actAddFolder);
    fileMenu->addSeparator();
    fileMenu->addAction(actExport);
    fileMenu->addSeparator();
    fileMenu->addAction(actQuit);

    m_actRotL = new QAction(tr("Rotate &Left"), this);
    m_actRotL->setShortcut(QKeySequence(Qt::Key_BracketLeft));
    connect(m_actRotL, &QAction::triggered, this, &MainWindow::rotateLeft);

    m_actRotR = new QAction(tr("Rotate &Right"), this);
    m_actRotR->setShortcut(QKeySequence(Qt::Key_BracketRight));
    connect(m_actRotR, &QAction::triggered, this, &MainWindow::rotateRight);

    m_actRotAllL = new QAction(tr("Rotate &All Left"), this);
    m_actRotAllL->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft));
    connect(m_actRotAllL, &QAction::triggered, this, &MainWindow::rotateAllLeft);

    m_actRotAllR = new QAction(tr("Rotate All Ri&ght"), this);
    m_actRotAllR->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_BracketRight));
    connect(m_actRotAllR, &QAction::triggered, this, &MainWindow::rotateAllRight);

    m_actRemove = new QAction(tr("&Remove Image"), this);
    m_actRemove->setShortcut(QKeySequence::Delete);
    connect(m_actRemove, &QAction::triggered, this, &MainWindow::removeCurrentImage);

    m_actMoveUp = new QAction(tr("Move Image &Up"), this);
    m_actMoveUp->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Up));
    connect(m_actMoveUp, &QAction::triggered, this, &MainWindow::moveCurrentImageUp);

    m_actMoveDown = new QAction(tr("Move Image &Down"), this);
    m_actMoveDown->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Down));
    connect(m_actMoveDown, &QAction::triggered, this, &MainWindow::moveCurrentImageDown);

    m_actTakePrev = new QAction(tr("Copy Corners from &Previous"), this);
    m_actTakePrev->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    m_actTakePrev->setToolTip(
        tr("Reuse the previous image's corner positions on this one"));
    connect(m_actTakePrev, &QAction::triggered,
            this, &MainWindow::takeOverPreviousPoints);

    m_actAutoDetect = new QAction(tr("&Auto-detect Corners"), this);
    m_actAutoDetect->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    m_actAutoDetect->setToolTip(
        tr("Detect the page corners automatically (1 page → 4 points, "
           "2 pages → 6-point spread)"));
    connect(m_actAutoDetect, &QAction::triggered,
            this, &MainWindow::autoDetectCorners);

    m_actAutoDetectAll = new QAction(tr("Auto-detect Corners on A&ll Images"), this);
    m_actAutoDetectAll->setToolTip(
        tr("Run auto-detect on every image in the project"));
    connect(m_actAutoDetectAll, &QAction::triggered,
            this, &MainWindow::autoDetectCornersAll);

    m_chkSixPoint = new QCheckBox(tr("Two pages (6 pts)"), this);
    connect(m_chkSixPoint, &QCheckBox::toggled, this, &MainWindow::onModeToggled);

    m_chkSplit = new QCheckBox(tr("Split spread"), this);
    m_chkSplit->setChecked(false);
    connect(m_chkSplit, &QCheckBox::toggled, this, &MainWindow::onSplitToggled);

    m_chkGrayscale = new QCheckBox(tr("Grayscale"), this);
    m_chkGrayscale->setToolTip(
        tr("Show the source image and thumbnails in grayscale (shortcut: G); "
           "the live preview stays in colour"));
    connect(m_chkGrayscale, &QCheckBox::toggled, this, &MainWindow::onGrayscaleToggled);
    // 'G' toggles it from anywhere (the checkbox is the state's single home).
    connect(new QShortcut(QKeySequence(Qt::Key_G), this), &QShortcut::activated,
            this, [this] { m_chkGrayscale->toggle(); });

    m_insetSpin = new QSpinBox(this);
    m_insetSpin->setRange(0, 2000);
    m_insetSpin->setSuffix(tr(" px"));
    m_insetSpin->setToolTip(
        tr("Shrink the marked area inward by this many pixels before export, so "
           "the page edges are cropped off (0 = export the marked area as-is)"));
    connect(m_insetSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::onInsetChanged);

    m_actPrev = new QAction(tr("&Previous"), this);
    m_actPrev->setShortcut(QKeySequence(Qt::Key_P));
    connect(m_actPrev, &QAction::triggered, this, &MainWindow::goPrev);

    m_actNext = new QAction(tr("&Next"), this);
    m_actNext->setShortcut(QKeySequence(Qt::Key_N));
    connect(m_actNext, &QAction::triggered, this, &MainWindow::goNext);

    editMenu->addAction(m_actRotL);
    editMenu->addAction(m_actRotR);
    editMenu->addAction(m_actRotAllL);
    editMenu->addAction(m_actRotAllR);
    editMenu->addSeparator();
    editMenu->addAction(m_actRemove);
    editMenu->addAction(m_actMoveUp);
    editMenu->addAction(m_actMoveDown);
    editMenu->addSeparator();
    editMenu->addAction(m_actTakePrev);
    editMenu->addAction(m_actAutoDetect);
    editMenu->addAction(m_actAutoDetectAll);
    editMenu->addSeparator();
    editMenu->addAction(m_actPrev);
    editMenu->addAction(m_actNext);

    auto *actSettings = new QAction(tr("&Settings..."), this);
    actSettings->setShortcut(QKeySequence::Preferences);
    connect(actSettings, &QAction::triggered, this, &MainWindow::openSettingsDialog);
    editMenu->addSeparator();
    editMenu->addAction(actSettings);

    tb->addAction(m_actRotL);
    tb->addAction(m_actRotR);
    tb->addAction(m_actRemove);
    tb->addSeparator();
    tb->addAction(m_actAutoDetect);
    tb->addSeparator();
    tb->addWidget(m_chkSixPoint);
    tb->addWidget(new QLabel(QStringLiteral("  ")));
    tb->addWidget(m_chkSplit);
    tb->addSeparator();
    tb->addWidget(new QLabel(tr("Inset:")));
    tb->addWidget(m_insetSpin);
    tb->addSeparator();
    tb->addWidget(m_chkGrayscale);
    tb->addSeparator();
    tb->addAction(m_actPrev);
    tb->addAction(m_actNext);
}

// ---- Importing -------------------------------------------------------------

void MainWindow::addImagesDialog()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, tr("Add Images"), QString(),
        tr("Images (%1)").arg(kImageFilters.join(' ')));
    addImages(paths);
}

void MainWindow::addFolderDialog()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Add Folder"));
    if (dir.isEmpty())
        return;
    QStringList paths;
    QDirIterator it(dir, kImageFilters, QDir::Files, QDirIterator::NoIteratorFlags);
    while (it.hasNext())
        paths << it.next();
    paths.sort();
    addImages(paths);
}

void MainWindow::addImages(const QStringList &paths)
{
    QStringList supported;
    for (const QString &path : paths)
        if (isSupportedImage(path))
            supported << path;

    const int firstAdded = m_doc.addImages(supported, m_lastMode, m_lastSplit);
    if (firstAdded < 0)
        return; // nothing supported was added

    rebuildFilmstrip();
    setDirty(true);
    selectIndex(m_current < 0 ? firstAdded : m_current);
}

// External entry point (command-line args, .desktop %F). A project file opens
// as a project; anything else is added as images.
void MainWindow::addPaths(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (isProjectFile(path)) {
            if (maybeSave())
                loadProjectFile(path);
            return;
        }
    }
    addImages(paths);
}

// ---- Filmstrip -------------------------------------------------------------

void MainWindow::rebuildFilmstrip()
{
    QSignalBlocker block(m_filmstrip);
    m_filmstrip->clear();
    for (int i = 0; i < m_doc.pageCount(); ++i) {
        m_filmstrip->addItem(new QListWidgetItem);
        updateFilmstripItem(i);
    }
    if (m_current >= 0 && m_current < m_doc.pageCount())
        m_filmstrip->setCurrentRow(m_current);
}

void MainWindow::updateFilmstripItem(int i)
{
    if (i < 0 || i >= m_doc.pageCount() || i >= m_filmstrip->count())
        return;
    const QImage base = m_doc.thumb(i);
    QImage rot = Warp::applyRotation(base, m_doc.page(i).rotation);
    QImage scaled =
        rot.scaled(kThumbW, kThumbH, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    // Desaturate the page image when the grayscale view is on; the marked badge
    // below is painted afterwards so it stays a coloured status indicator.
    if (m_grayscale)
        scaled = scaled.convertToFormat(QImage::Format_Grayscale8);
    QPixmap pm = QPixmap::fromImage(scaled);

    if (m_doc.page(i).marked) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const double d = 20;
        QRectF badge(pm.width() - d - 3, 3, d, d);
        p.setBrush(QColor(50, 180, 90));
        p.setPen(QPen(Qt::white, 2));
        p.drawEllipse(badge);
        const QPointF c = badge.center();
        p.setPen(QPen(Qt::white, 2.5));
        p.drawPolyline(QPolygonF()
                       << QPointF(c.x() - 5, c.y())
                       << QPointF(c.x() - 1, c.y() + 4)
                       << QPointF(c.x() + 6, c.y() - 5));
    }
    m_filmstrip->item(i)->setIcon(QIcon(pm));
}

void MainWindow::onFilmstripRow(int row)
{
    if (row >= 0 && row != m_current)
        selectIndex(row);
}

void MainWindow::onFilmstripContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_filmstrip->itemAt(pos);
    if (!item)
        return;
    const int row = m_filmstrip->row(item);

    QMenu menu(this);
    QAction *moveUp = menu.addAction(tr("Move Up"));
    moveUp->setEnabled(row > 0);
    QAction *moveDown = menu.addAction(tr("Move Down"));
    moveDown->setEnabled(row < m_doc.pageCount() - 1);
    menu.addSeparator();
    QAction *remove = menu.addAction(tr("Remove Image"));

    QAction *chosen = menu.exec(m_filmstrip->mapToGlobal(pos));
    if (!chosen)
        return;
    selectIndex(row);
    if (chosen == moveUp)
        moveCurrentImage(-1);
    else if (chosen == moveDown)
        moveCurrentImage(1);
    else if (chosen == remove)
        removeCurrentImage();
}

void MainWindow::removeCurrentImage()
{
    if (m_current < 0 || m_current >= m_doc.pageCount())
        return;

    // Guard marked images (no undo); unmarked ones go without a prompt.
    if (m_doc.page(m_current).marked) {
        const auto ret = QMessageBox::question(
            this, tr("Remove Image"),
            tr("Remove \"%1\" from the project?\nIts corner markings will be lost.")
                .arg(QFileInfo(m_doc.page(m_current).path).fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }

    const int removed = m_current;
    m_doc.removeAt(removed);
    setDirty(true);

    int next = removed;
    if (next >= m_doc.pageCount())
        next = m_doc.pageCount() - 1; // -1 when the list is now empty

    m_current = -1; // force a fresh load in selectIndex
    rebuildFilmstrip();
    selectIndex(next);
}

void MainWindow::moveCurrentImageUp()
{
    moveCurrentImage(-1);
}

void MainWindow::moveCurrentImageDown()
{
    moveCurrentImage(1);
}

// Swap the current image with its neighbour `delta` rows away and keep it
// selected, so the user can reorder pages within the filmstrip.
void MainWindow::moveCurrentImage(int delta)
{
    if (m_current < 0)
        return;
    const int target = m_doc.move(m_current, delta);
    if (target == m_current)
        return; // out of range -> nothing moved

    setDirty(true);
    m_current = -1; // force selectIndex to reload and rebind the canvas
    rebuildFilmstrip();
    selectIndex(target);
}

// Copy the previous image's corner marking onto the current one — useful when
// consecutive photos are shot from the same position. The points live in
// rotated-image pixel coordinates, so we also adopt the previous page's
// mode/split (otherwise the copied points wouldn't fit) and clamp them to this
// image's bounds in case its dimensions differ; the user can then fine-tune.
void MainWindow::takeOverPreviousPoints()
{
    if (m_current <= 0)
        return;
    const Page &prev = m_doc.page(m_current - 1);
    if (!prev.marked || !prev.hasPoints()) {
        statusBar()->showMessage(
            tr("The previous image has no corners to copy."), 2500);
        return;
    }

    Page &cur = m_doc.page(m_current);
    cur.mode = prev.mode;
    cur.splitSpread = prev.splitSpread;
    cur.points = prev.points;
    cur.marked = true;
    m_lastMode = cur.mode;
    m_lastSplit = cur.splitSpread;

    if (!m_currentOriginal.isNull()) {
        const bool swap = (cur.rotation % 180) != 0; // 90/270 swaps W/H
        const double w = swap ? m_currentOriginal.height() : m_currentOriginal.width();
        const double h = swap ? m_currentOriginal.width() : m_currentOriginal.height();
        for (QPointF &p : cur.points) {
            p.setX(qBound(0.0, p.x(), w));
            p.setY(qBound(0.0, p.y(), h));
        }
    }

    setDirty(true);
    refreshCurrent();
    syncControlsToCurrent();
    updateFilmstripItem(m_current);
    updateStatus();
}

// Auto-detect the page corners on the current image with the YOLO11-seg model.
// The model expects an upright page, and operates on m_currentRotated -- whose
// pixel space is exactly Page::points' space, so detections drop straight in.
//   1 page  -> 4-point quad (Four)
//   2 pages -> 6-point spread (Six); spine = midpoints of the shared edge
//   0 or >2 -> reported as a failed detection, corners left untouched
void MainWindow::autoDetectCorners()
{
    if (m_current < 0 || m_currentRotated.isNull())
        return;

    if (!m_detector)
        m_detector = new PageDetector(); // lazily loads the ONNX model
    if (!m_detector->ok()) {
        statusBar()->showMessage(
            tr("Auto-detect: page-detection model could not be loaded."), 4000);
        return;
    }

    const cv::Mat mat = ImageConv::toMatRgb(m_currentRotated);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QVector<PageDetector::Quad> quads = m_detector->detect(mat);
    QApplication::restoreOverrideCursor();

    const int applied = applyQuadsToPage(m_doc.page(m_current), quads);
    if (applied == 0) {
        const int n = quads.size();
        statusBar()->showMessage(
            n == 0 ? tr("Auto-detect: no page found.")
                   : tr("Auto-detect: %1 regions found (expected 1 or 2) — "
                        "please mark this image manually.").arg(n), 5000);
        return;
    }
    m_lastMode = m_doc.page(m_current).mode;

    setDirty(true);
    refreshCurrent();
    syncControlsToCurrent();
    updateFilmstripItem(m_current);
    updateStatus();
    statusBar()->showMessage(
        applied == 1 ? tr("Auto-detect: 1 page → 4 points.")
                     : tr("Auto-detect: 2 pages → 6-point spread."), 4000);
}

// Auto-detect corners on EVERY image in the project. Loads each image, rotates
// it upright, runs the detector, and applies the same 1->4 / 2->6 / fail rule.
// Already-marked images are overwritten only where a page is found (subject to a
// confirmation), so a failed detection never wipes existing corners.
void MainWindow::autoDetectCornersAll()
{
    if (m_doc.isEmpty())
        return;

    if (!m_detector)
        m_detector = new PageDetector(); // lazily loads the ONNX model
    if (!m_detector->ok()) {
        statusBar()->showMessage(
            tr("Auto-detect: page-detection model could not be loaded."), 4000);
        return;
    }

    const int markedCount = m_doc.markedCount();
    if (markedCount > 0) {
        const auto choice = QMessageBox::warning(
            this, tr("Auto-detect Corners on All Images"),
            tr("This replaces the corners on %1 already-marked image(s) where a "
               "page is detected. Continue?").arg(markedCount),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice != QMessageBox::Yes)
            return;
    }

    QProgressDialog progress(tr("Auto-detecting page corners…"), tr("Cancel"),
                             0, m_doc.pageCount(), this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);

    int four = 0, six = 0, failed = 0;
    for (int i = 0; i < m_doc.pageCount(); ++i) {
        progress.setValue(i);
        if (progress.wasCanceled())
            break;

        Page &pg = m_doc.page(i);
        QImageReader reader(pg.path);
        reader.setAutoTransform(true); // honour EXIF, like elsewhere
        const QImage original = reader.read();
        if (original.isNull()) {
            ++failed;
            continue;
        }
        const QImage rotated = Warp::applyRotation(original, pg.rotation);
        const cv::Mat mat = ImageConv::toMatRgb(rotated);

        const int applied = applyQuadsToPage(pg, m_detector->detect(mat));
        if (applied == 1) ++four;
        else if (applied == 2) ++six;
        else ++failed;
        if (applied > 0)
            updateFilmstripItem(i);
    }
    progress.setValue(m_doc.pageCount());

    if (four + six > 0) {
        setDirty(true);
        refreshCurrent(); // re-render the currently shown image with its new corners
        syncControlsToCurrent();
        updateStatus();
    }
    statusBar()->showMessage(
        tr("Auto-detect: %1 marked (%2 single, %3 spread), %4 left to mark manually.")
            .arg(four + six).arg(four).arg(six).arg(failed), 8000);
}

// ---- Selection / current image ---------------------------------------------

void MainWindow::selectIndex(int i)
{
    if (i < 0 || i >= m_doc.pageCount()) {
        m_current = -1;
        m_canvas->clear();
        m_preview->clear();
        m_loupe->clear();
        m_loupe->setPage(nullptr);
        syncControlsToCurrent();
        updateStatus();
        return;
    }

    m_current = i;

    QImageReader reader(m_doc.page(i).path);
    reader.setAutoTransform(true);
    m_currentOriginal = reader.read();

    {
        QSignalBlocker block(m_filmstrip);
        m_filmstrip->setCurrentRow(i);
    }

    refreshCurrent();
    syncControlsToCurrent();
    updateStatus();
    m_canvas->setFocus(); // so digit/arrow keys drive the corner points
}

void MainWindow::refreshCurrent()
{
    if (m_current < 0)
        return;
    Page &pg = m_doc.page(m_current);

    if (m_currentOriginal.isNull()) {
        m_canvas->clear();
        m_preview->clear();
        m_loupe->clear();
        m_loupe->setPage(nullptr);
        return;
    }

    m_currentRotated = Warp::applyRotation(m_currentOriginal, pg.rotation);

    if (pg.points.size() != pg.expectedPointCount())
        pg.points = defaultInsetPoints(m_currentRotated.size(), pg.mode);

    // Downscaled proxy for the live preview (keeps dewarp interactive).
    const int maxDim = std::max(m_currentRotated.width(), m_currentRotated.height());
    m_proxyScale = std::min(1.0, 1400.0 / std::max(1, maxDim));
    if (m_proxyScale < 1.0) {
        m_currentProxy = m_currentRotated.scaled(
            std::lround(m_currentRotated.width() * m_proxyScale),
            std::lround(m_currentRotated.height() * m_proxyScale),
            Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    } else {
        m_currentProxy = m_currentRotated;
    }

    // Give the loupe the image + page first: the next line emits
    // loupeTargetChanged, which makes the loupe paint using them.
    m_loupe->setImage(m_currentRotated);
    m_loupe->setPage(&pg);
    m_canvas->setImage(m_currentRotated, &pg);
    updatePreview();
}

void MainWindow::updatePreview()
{
    if (m_current < 0) {
        m_preview->clear();
        return;
    }
    const Page &pg = m_doc.page(m_current);
    if (!pg.hasPoints() || m_currentProxy.isNull()) {
        m_preview->clear();
        return;
    }
    // Preview the exact exported geometry: dewarp the inset (interior) points,
    // the same ones PdfExporter renders. With inset 0 this is the marked quad.
    const QVector<QPointF> exported =
        insetPoints(pg.points, pg.mode, m_doc.inset());
    QVector<QPointF> scaled;
    scaled.reserve(exported.size());
    for (const QPointF &p : exported)
        scaled.push_back(p * m_proxyScale);

    m_preview->setPages(
        Warp::renderPages(m_currentProxy, scaled, pg.mode, pg.splitSpread,
                          m_prefs.equalPageWidths));
}

void MainWindow::syncControlsToCurrent()
{
    // Per-image actions need a current selection; batch actions only need at
    // least one image present. This is the single place action enablement lives.
    const bool has = (m_current >= 0);
    const bool anyImages = !m_doc.isEmpty();
    m_actRotL->setEnabled(has);
    m_actRotR->setEnabled(has);
    m_actRemove->setEnabled(has);
    m_actMoveUp->setEnabled(has && m_current > 0);
    m_actMoveDown->setEnabled(has && m_current < m_doc.pageCount() - 1);
    m_actTakePrev->setEnabled(has && m_current > 0 && m_doc.page(m_current - 1).marked);
    m_actAutoDetect->setEnabled(has);
    m_actAutoDetectAll->setEnabled(anyImages);
    m_actRotAllL->setEnabled(anyImages);
    m_actRotAllR->setEnabled(anyImages);
    m_insetSpin->setEnabled(anyImages); // project-level: needs only some images
    m_chkSixPoint->setEnabled(has);

    if (!has) {
        m_chkSplit->setEnabled(false);
        return;
    }
    const Page &pg = m_doc.page(m_current);
    {
        QSignalBlocker b1(m_chkSixPoint);
        m_chkSixPoint->setChecked(pg.mode == Page::Six);
    }
    {
        QSignalBlocker b2(m_chkSplit);
        m_chkSplit->setChecked(pg.splitSpread);
    }
    m_chkSplit->setEnabled(pg.mode == Page::Six);
}

void MainWindow::onPointsChanged()
{
    if (m_current < 0)
        return;
    setDirty(true);
    updateFilmstripItem(m_current);
    updatePreview();
    updateStatus();
}

// ---- Editing actions -------------------------------------------------------

void MainWindow::rotateLeft()
{
    rotateCurrent(-90);
}

void MainWindow::rotateRight()
{
    rotateCurrent(90);
}

void MainWindow::rotateCurrent(int deltaDegrees)
{
    if (m_current < 0)
        return;
    m_doc.rotateAt(m_current, deltaDegrees);
    setDirty(true);
    refreshCurrent();
    updateFilmstripItem(m_current);
}

void MainWindow::rotateAllLeft()
{
    rotateAll(-90);
}

void MainWindow::rotateAllRight()
{
    rotateAll(90);
}

void MainWindow::rotateAll(int deltaDegrees)
{
    if (m_doc.isEmpty())
        return;

    // Rotating clears corners (like the single-image rotate), so warn before
    // discarding any markings the user already made.
    if (m_doc.anyMarked()) {
        const auto ret = QMessageBox::question(
            this, tr("Rotate All Images"),
            tr("Rotating every image will clear the corner markings on all "
               "already-marked pages.\n\nRotate all %n image(s) anyway?", "",
               m_doc.pageCount()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }

    m_doc.rotateAll(deltaDegrees);

    setDirty(true);
    rebuildFilmstrip(); // refresh every thumbnail at the new rotation
    if (m_current >= 0)
        refreshCurrent();
    updateStatus();
}

void MainWindow::onModeToggled(bool six)
{
    if (m_current < 0)
        return;
    Page &pg = m_doc.page(m_current);
    const Page::Mode newMode = six ? Page::Six : Page::Four;

    // Preserve digitized corners across the switch: a marked 4-point quad gains
    // spine points at the top/bottom edge midpoints, a marked 6-point spread
    // drops its spine points back to the four outer corners. Anything not yet
    // digitized just resets to the new mode's default inset (via refreshCurrent).
    if (pg.marked && pg.hasPoints())
        pg.points = convertPoints(pg.points, pg.mode, newMode);
    else {
        pg.points.clear();
        pg.marked = false;
    }

    pg.mode = newMode;
    m_lastMode = pg.mode;
    m_chkSplit->setEnabled(six);
    setDirty(true);
    refreshCurrent();
    updateFilmstripItem(m_current);
    updateStatus();
}

void MainWindow::onSplitToggled(bool split)
{
    if (m_current < 0)
        return;
    Page &pg = m_doc.page(m_current);
    pg.splitSpread = split;
    m_lastSplit = split;
    setDirty(true);
    updatePreview();
}

// Toggle the grayscale view aid (shortcut G). The canvas + loupe gray their
// source on the spot — corner selection and any in-progress drag are kept — and
// every thumbnail is repainted. The live preview is intentionally left in colour,
// and nothing here touches the document (this is a view state, not a save).
void MainWindow::onGrayscaleToggled(bool on)
{
    m_grayscale = on;
    m_canvas->setGrayscale(on);
    m_loupe->setGrayscale(on);
    for (int i = 0; i < m_doc.pageCount(); ++i)
        updateFilmstripItem(i);
    statusBar()->showMessage(
        on ? tr("Grayscale view on (preview stays in colour).")
           : tr("Grayscale view off."), 2500);
}

// Project-level export inset changed from the toolbar: store it, mirror it onto
// the canvas + preview overlays live, and mark the project dirty.
void MainWindow::onInsetChanged(int px)
{
    m_doc.setInset(px);
    setDirty(true);
    m_canvas->setExportInset(px);
    m_loupe->setExportInset(px);
    updatePreview();
}

void MainWindow::syncInsetControl()
{
    QSignalBlocker block(m_insetSpin); // setValue must not mark the project dirty
    m_insetSpin->setValue(m_doc.inset());
    m_canvas->setExportInset(m_doc.inset());
    m_loupe->setExportInset(m_doc.inset());
}

void MainWindow::goPrev()
{
    if (m_current > 0)
        selectIndex(m_current - 1);
}

void MainWindow::goNext()
{
    if (m_current >= 0 && m_current < m_doc.pageCount() - 1)
        selectIndex(m_current + 1);
}

// ---- Project I/O -----------------------------------------------------------

void MainWindow::openProject()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), AppSettings::lastDir(),
        tr("wonderscan project (*.wsp *.json)"));
    if (path.isEmpty())
        return;
    loadProjectFile(path);
}

bool MainWindow::loadProjectFile(const QString &path)
{
    QString err;
    if (!m_doc.load(path, &err)) {
        QMessageBox::warning(this, tr("Open failed"), err);
        removeFromRecent(path); // drop a stale entry if this came from the list
        return false;
    }

    m_current = -1;
    rebuildFilmstrip();
    setDirty(false);
    selectIndex(m_doc.isEmpty() ? -1 : 0);
    syncInsetControl(); // reflect the project's saved inset on the spin box/canvas
    updateTitle();
    addToRecent(path);
    setLastDir(QFileInfo(path).absolutePath());

    // Projects store absolute image paths; warn if any no longer resolve (e.g.
    // the images were moved/renamed) instead of silently showing blank pages.
    const QStringList missing = m_doc.missingImagePaths();
    if (!missing.isEmpty()) {
        const QStringList shown = missing.mid(0, 10);
        QString body = tr("%1 of %2 image(s) referenced by this project could not "
                          "be found at their saved (absolute) paths:\n\n%3")
                           .arg(missing.size())
                           .arg(m_doc.pageCount())
                           .arg(shown.join(QStringLiteral("\n")));
        if (missing.size() > shown.size())
            body += tr("\n… and %1 more.").arg(missing.size() - shown.size());
        body += tr("\n\nThose pages will appear blank. Re-add the images from "
                   "their current location to fix them.");
        QMessageBox::warning(this, tr("Missing images"), body);
    }
    return true;
}

bool MainWindow::saveProject()
{
    if (m_doc.isUntitled())
        return saveProjectAs();

    QString err;
    if (!m_doc.save(m_doc.path(), &err)) {
        QMessageBox::warning(this, tr("Save failed"), err);
        return false;
    }
    setDirty(false);
    addToRecent(m_doc.path());
    setLastDir(QFileInfo(m_doc.path()).absolutePath());
    return true;
}

bool MainWindow::saveProjectAs()
{
    const QString lastDir = AppSettings::lastDir();
    const QString suggested = lastDir.isEmpty()
                                  ? QStringLiteral("project.wsp")
                                  : QDir(lastDir).filePath(QStringLiteral("project.wsp"));
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project As"), suggested,
        tr("wonderscan project (*.wsp)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".wsp", Qt::CaseInsensitive)
        && !path.endsWith(".json", Qt::CaseInsensitive))
        path += ".wsp";
    m_doc.setPath(path);
    updateTitle();
    return saveProject();
}

// ---- Settings / recent projects --------------------------------------------

// Push the current nudge/loupe preferences into the canvas and loupe.
void MainWindow::applyEditorSettings()
{
    m_canvas->setNudgeSteps(m_prefs.nudgeFine, m_prefs.nudgeCoarse, m_prefs.nudgeLarge);
    m_loupe->setZoom(m_prefs.loupeZoom);
}

void MainWindow::loadSettings()
{
    m_prefs = AppSettings::loadEditorPrefs();
    applyEditorSettings();
    updateRecentMenu();
}

void MainWindow::setLastDir(const QString &dir)
{
    AppSettings::setLastDir(dir);
}

void MainWindow::addToRecent(const QString &path)
{
    AppSettings::addRecentProject(path);
    updateRecentMenu();
}

void MainWindow::removeFromRecent(const QString &path)
{
    if (AppSettings::removeRecentProject(path))
        updateRecentMenu();
}

void MainWindow::updateRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();

    const QStringList recent = AppSettings::recentProjects();
    if (recent.isEmpty()) {
        QAction *empty = m_recentMenu->addAction(tr("(No recent projects)"));
        empty->setEnabled(false);
        return;
    }

    for (const QString &path : recent) {
        const QFileInfo fi(path);
        // Disambiguate the many identically-named "project.wsp" files by their
        // parent folder, e.g. "Das Geburtstagspaket/project.wsp".
        const QString parent = fi.dir().dirName();
        const QString label =
            parent.isEmpty() ? fi.fileName() : parent + '/' + fi.fileName();
        QAction *a = m_recentMenu->addAction(label);
        a->setStatusTip(path);
        connect(a, &QAction::triggered, this, [this, path] {
            if (maybeSave())
                loadProjectFile(path);
        });
    }
    m_recentMenu->addSeparator();
    connect(m_recentMenu->addAction(tr("Clear List")), &QAction::triggered, this,
            [this] {
                AppSettings::clearRecentProjects();
                updateRecentMenu();
            });
}

void MainWindow::openSettingsDialog()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Settings"));
    auto *form = new QFormLayout(&dlg);

    auto makeStep = [&dlg](int value) {
        auto *sb = new QSpinBox(&dlg);
        sb->setRange(1, 500);
        sb->setSuffix(tr(" px"));
        sb->setValue(value);
        return sb;
    };
    auto *fine = makeStep(m_prefs.nudgeFine);
    auto *coarse = makeStep(m_prefs.nudgeCoarse);
    auto *large = makeStep(m_prefs.nudgeLarge);
    form->addRow(tr("Nudge (arrow):"), fine);
    form->addRow(tr("Nudge (Shift):"), coarse);
    form->addRow(tr("Nudge (Ctrl+Shift):"), large);

    // Loupe magnification is set by the slider on the loupe itself, not here.

    auto *equalWidths =
        new QCheckBox(tr("Equal page widths in two-page spreads"), &dlg);
    equalWidths->setChecked(m_prefs.equalPageWidths);
    form->addRow(equalWidths);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;

    m_prefs.nudgeFine = fine->value();
    m_prefs.nudgeCoarse = coarse->value();
    m_prefs.nudgeLarge = large->value();
    m_prefs.equalPageWidths = equalWidths->isChecked();
    AppSettings::saveEditorPrefs(m_prefs);

    applyEditorSettings();
    updatePreview(); // reflect the spread-width change live (no-op if no image)
}

// ---- Export ----------------------------------------------------------------

// Prompt for DPI + JPEG quality. On accept, stores them and marks the project
// dirty, then returns true; returns false if the user cancels.
bool MainWindow::promptExportSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Export settings"));
    auto *form = new QFormLayout(&dlg);

    auto *dpiSpin = new QSpinBox(&dlg);
    dpiSpin->setRange(72, 1200);
    dpiSpin->setValue(m_doc.dpi());
    dpiSpin->setSuffix(tr(" dpi"));
    form->addRow(tr("Resolution:"), dpiSpin);

    auto *qSlider = new QSlider(Qt::Horizontal, &dlg);
    qSlider->setRange(1, 100);
    qSlider->setValue(m_doc.jpegQuality());
    auto *qLabel = new QLabel(QString::number(m_doc.jpegQuality()), &dlg);
    connect(qSlider, &QSlider::valueChanged, qLabel,
            [qLabel](int v) { qLabel->setNum(v); });
    auto *qRow = new QWidget(&dlg);
    auto *qLayout = new QFormLayout(qRow);
    qLayout->setContentsMargins(0, 0, 0, 0);
    qLayout->addRow(qSlider, qLabel);
    form->addRow(tr("JPEG quality:"), qRow);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    m_doc.setDpi(dpiSpin->value());
    m_doc.setJpegQuality(qSlider->value());
    setDirty(true);
    return true;
}

void MainWindow::exportPdf()
{
    if (m_doc.isEmpty()) {
        QMessageBox::information(this, tr("Export"), tr("No images loaded."));
        return;
    }

    QVector<const Page *> ready;
    int unmarked = 0;
    for (const Page &p : m_doc.pages()) {
        if (p.readyForExport())
            ready.push_back(&p);
        else
            ++unmarked;
    }

    if (ready.isEmpty()) {
        QMessageBox::information(
            this, tr("Export"),
            tr("No images have corners set yet. Mark some pages first."));
        return;
    }

    if (unmarked > 0) {
        const auto ret = QMessageBox::question(
            this, tr("Export"),
            tr("%1 of %2 images have no corners set and will be skipped.\n\n"
               "Export the %3 marked page(s)?")
                .arg(unmarked).arg(m_doc.pageCount()).arg(ready.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ret != QMessageBox::Yes)
            return;
    }

    if (!promptExportSettings())
        return;

    QString defaultDir = AppSettings::lastDir();
    if (defaultDir.isEmpty()) {
        if (!m_doc.isUntitled())
            defaultDir = QFileInfo(m_doc.path()).absolutePath();
        else if (!m_doc.isEmpty())
            defaultDir = QFileInfo(m_doc.page(0).path).absolutePath();
    }
    const QString defaultPath = QDir(defaultDir).filePath("wonderscan.pdf");

    const QString out = QFileDialog::getSaveFileName(
        this, tr("Export PDF"), defaultPath, tr("PDF (*.pdf)"));
    if (out.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = PdfExporter::exportPdf(out, ready, m_doc.dpi(),
                                           m_doc.jpegQuality(), &err,
                                           m_prefs.equalPageWidths, m_doc.inset());
    QApplication::restoreOverrideCursor();

    if (ok) {
        setLastDir(QFileInfo(out).absolutePath());
        QMessageBox::information(
            this, tr("Export complete"),
            tr("Exported %1 page(s) to:\n%2").arg(ready.size()).arg(out));
    } else {
        QMessageBox::warning(this, tr("Export failed"), err);
    }
}

// ---- Dirty / title / status ------------------------------------------------

void MainWindow::setDirty(bool dirty)
{
    m_doc.setDirty(dirty);
    setWindowModified(dirty);
}

bool MainWindow::maybeSave()
{
    if (!m_doc.dirty())
        return true;
    const auto ret = QMessageBox::warning(
        this, tr("wonderscan"),
        tr("The project has unsaved changes. Save before continuing?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (ret == QMessageBox::Save)
        return saveProject();
    return ret == QMessageBox::Discard;
}

void MainWindow::updateTitle()
{
    QString name = m_doc.isUntitled()
                       ? tr("Untitled")
                       : QFileInfo(m_doc.path()).fileName();
    setWindowTitle(tr("wonderscan — %1[*]").arg(name));
}

void MainWindow::updateStatus()
{
    if (m_current < 0) {
        m_statusLabel->setText(tr("%n image(s) loaded.", "", m_doc.pageCount()));
        return;
    }
    const Page &pg = m_doc.page(m_current);
    m_statusLabel->setText(
        tr("%1 / %2  —  %3  —  %4  —  %5")
            .arg(m_current + 1)
            .arg(m_doc.pageCount())
            .arg(QFileInfo(pg.path).fileName())
            .arg(pg.mode == Page::Six
                     ? (pg.splitSpread ? tr("2 pages (split)") : tr("2 pages (stitched)"))
                     : tr("1 page"))
            .arg(pg.marked ? tr("marked") : tr("unmarked")));
}

// ---- Events ----------------------------------------------------------------

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (maybeSave())
        e->accept();
    else
        e->ignore();
}

void MainWindow::keyPressEvent(QKeyEvent *e)
{
    // Digit 1..6 selects a corner point for keyboard editing even when focus
    // sits outside the canvas; arrow-key nudging is handled by the canvas once
    // it has focus. (If the canvas already has focus it consumes these first.)
    if (m_current >= 0 && e->key() >= Qt::Key_1 && e->key() <= Qt::Key_6) {
        m_canvas->setFocus();
        if (m_canvas->selectPoint(e->key() - Qt::Key_1)) {
            e->accept();
            return;
        }
    }
    QMainWindow::keyPressEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls())
        e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    QStringList paths;
    for (const QUrl &url : e->mimeData()->urls()) {
        const QString local = url.toLocalFile();
        if (local.isEmpty())
            continue;
        QFileInfo fi(local);
        if (fi.isDir()) {
            QDirIterator it(local, kImageFilters, QDir::Files);
            while (it.hasNext())
                paths << it.next();
        } else if (isSupportedImage(local)) {
            paths << local;
        }
    }
    paths.sort();
    addImages(paths);
}
