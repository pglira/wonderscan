#include "MainWindow.h"

#include "ImageCanvas.h"
#include "PreviewPane.h"
#include "PdfExporter.h"
#include "Project.h"
#include "Warp.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
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
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <cmath>

namespace {

const QStringList kImageFilters = {
    "*.jpg", "*.jpeg", "*.png", "*.tif", "*.tiff", "*.bmp", "*.webp"
};

constexpr int kMaxRecent = 8;
const QString kRecentKey = QStringLiteral("recentProjects");
const QString kLastDirKey = QStringLiteral("paths/lastDir");
const QString kNudgeFineKey = QStringLiteral("nudge/fine");
const QString kNudgeCoarseKey = QStringLiteral("nudge/coarse");
const QString kNudgeLargeKey = QStringLiteral("nudge/large");
const QString kLoupeKey = QStringLiteral("loupe/zoom");

bool isSupportedImage(const QString &path)
{
    const QString lower = path.toLower();
    for (const QString &f : kImageFilters)
        if (lower.endsWith(f.mid(1)))
            return true;
    return false;
}

// Apply a 90° rotation step to a page. The marked corners were positioned on
// the old orientation, so rotating invalidates them.
void rotatePageBy(Page &p, int deltaDegrees)
{
    p.rotation = ((p.rotation + deltaDegrees) % 360 + 360) % 360;
    p.points.clear();
    p.marked = false;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setupUi();
    setupActions();
    loadSettings();
    setAcceptDrops(true);
    resize(1400, 900);
    updateTitle();
    syncControlsToCurrent(); // start with no current image -> controls disabled
    updateStatus();
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
    m_filmstrip->setIconSize(QSize(96, 120));
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

    m_preview = new PreviewPane(this);

    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(m_filmstrip);
    splitter->addWidget(m_canvas);
    splitter->addWidget(m_preview);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 0);
    splitter->setSizes({130, 900, 320});
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

    m_chkSixPoint = new QCheckBox(tr("Two pages (6 pts)"), this);
    connect(m_chkSixPoint, &QCheckBox::toggled, this, &MainWindow::onModeToggled);

    m_chkSplit = new QCheckBox(tr("Split spread"), this);
    m_chkSplit->setChecked(false);
    connect(m_chkSplit, &QCheckBox::toggled, this, &MainWindow::onSplitToggled);

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
    tb->addWidget(m_chkSixPoint);
    tb->addWidget(new QLabel(QStringLiteral("  ")));
    tb->addWidget(m_chkSplit);
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

QImage MainWindow::renderThumbBase(const QString &path) const
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize sz = reader.size();
    if (sz.isValid()) {
        sz.scale(192, 192, Qt::KeepAspectRatio);
        reader.setScaledSize(sz);
    }
    QImage img = reader.read();
    if (img.isNull()) {
        QImage placeholder(96, 120, QImage::Format_RGB888);
        placeholder.fill(QColor(90, 70, 70));
        return placeholder;
    }
    return img;
}

void MainWindow::addImages(const QStringList &paths)
{
    if (paths.isEmpty())
        return;

    int firstAdded = m_pages.size();
    for (const QString &path : paths) {
        if (!isSupportedImage(path))
            continue;
        Page p;
        p.path = path;
        p.mode = m_lastMode;
        p.splitSpread = m_lastSplit;
        m_pages.push_back(p);
        m_thumbs.push_back(renderThumbBase(path));
    }

    if (m_pages.size() == firstAdded)
        return; // nothing supported was added

    rebuildFilmstrip();
    setDirty(true);
    selectIndex(m_current < 0 ? firstAdded : m_current);
}

void MainWindow::addPaths(const QStringList &paths)
{
    addImages(paths);
}

// ---- Filmstrip -------------------------------------------------------------

void MainWindow::rebuildFilmstrip()
{
    QSignalBlocker block(m_filmstrip);
    m_filmstrip->clear();
    for (int i = 0; i < m_pages.size(); ++i) {
        m_filmstrip->addItem(new QListWidgetItem);
        updateFilmstripItem(i);
    }
    if (m_current >= 0 && m_current < m_pages.size())
        m_filmstrip->setCurrentRow(m_current);
}

void MainWindow::updateFilmstripItem(int i)
{
    if (i < 0 || i >= m_pages.size() || i >= m_filmstrip->count())
        return;
    const QImage base = m_thumbs.value(i);
    QImage rot = Warp::applyRotation(base, m_pages[i].rotation);
    QPixmap pm = QPixmap::fromImage(
        rot.scaled(96, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    if (m_pages[i].marked) {
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
    moveDown->setEnabled(row < m_pages.size() - 1);
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
    if (m_current < 0 || m_current >= m_pages.size())
        return;

    // Guard marked images (no undo); unmarked ones go without a prompt.
    if (m_pages[m_current].marked) {
        const auto ret = QMessageBox::question(
            this, tr("Remove Image"),
            tr("Remove \"%1\" from the project?\nIts corner markings will be lost.")
                .arg(QFileInfo(m_pages[m_current].path).fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }

    const int removed = m_current;
    m_pages.remove(removed);
    m_thumbs.remove(removed);
    setDirty(true);

    int next = removed;
    if (next >= m_pages.size())
        next = m_pages.size() - 1; // -1 when the list is now empty

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
    const int target = m_current + delta;
    if (target < 0 || target >= m_pages.size())
        return;

    m_pages.swapItemsAt(m_current, target);
    m_thumbs.swapItemsAt(m_current, target);
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
    const Page &prev = m_pages[m_current - 1];
    if (!prev.marked || !prev.hasPoints()) {
        statusBar()->showMessage(
            tr("The previous image has no corners to copy."), 2500);
        return;
    }

    Page &cur = m_pages[m_current];
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

// ---- Selection / current image ---------------------------------------------

void MainWindow::selectIndex(int i)
{
    if (i < 0 || i >= m_pages.size()) {
        m_current = -1;
        m_canvas->clear();
        m_preview->clear();
        syncControlsToCurrent();
        updateStatus();
        return;
    }

    m_current = i;

    QImageReader reader(m_pages[i].path);
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
    Page &pg = m_pages[m_current];

    if (m_currentOriginal.isNull()) {
        m_canvas->clear();
        m_preview->clear();
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

    m_canvas->setImage(m_currentRotated, &pg);
    updatePreview();
}

void MainWindow::updatePreview()
{
    if (m_current < 0) {
        m_preview->clear();
        return;
    }
    const Page &pg = m_pages[m_current];
    if (!pg.hasPoints() || m_currentProxy.isNull()) {
        m_preview->clear();
        return;
    }
    QVector<QPointF> scaled;
    scaled.reserve(pg.points.size());
    for (const QPointF &p : pg.points)
        scaled.push_back(p * m_proxyScale);

    m_preview->setPages(
        Warp::renderPages(m_currentProxy, scaled, pg.mode, pg.splitSpread));
}

void MainWindow::syncControlsToCurrent()
{
    const bool has = (m_current >= 0);
    m_actRotL->setEnabled(has);
    m_actRotR->setEnabled(has);
    m_actRemove->setEnabled(has);
    m_actMoveUp->setEnabled(has && m_current > 0);
    m_actMoveDown->setEnabled(has && m_current < m_pages.size() - 1);
    m_actTakePrev->setEnabled(has && m_current > 0 && m_pages[m_current - 1].marked);
    m_chkSixPoint->setEnabled(has);

    if (!has) {
        m_chkSplit->setEnabled(false);
        return;
    }
    const Page &pg = m_pages[m_current];
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
    rotatePageBy(m_pages[m_current], deltaDegrees);
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
    if (m_pages.isEmpty())
        return;

    // Rotating clears corners (like the single-image rotate), so warn before
    // discarding any markings the user already made.
    bool anyMarked = false;
    for (const Page &p : m_pages)
        if (p.marked) {
            anyMarked = true;
            break;
        }
    if (anyMarked) {
        const auto ret = QMessageBox::question(
            this, tr("Rotate All Images"),
            tr("Rotating every image will clear the corner markings on all "
               "already-marked pages.\n\nRotate all %n image(s) anyway?", "",
               m_pages.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }

    for (Page &p : m_pages)
        rotatePageBy(p, deltaDegrees);

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
    Page &pg = m_pages[m_current];
    pg.mode = six ? Page::Six : Page::Four;
    m_lastMode = pg.mode;
    pg.points.clear();
    pg.marked = false;
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
    Page &pg = m_pages[m_current];
    pg.splitSpread = split;
    m_lastSplit = split;
    setDirty(true);
    updatePreview();
}

void MainWindow::goPrev()
{
    if (m_current > 0)
        selectIndex(m_current - 1);
}

void MainWindow::goNext()
{
    if (m_current >= 0 && m_current < m_pages.size() - 1)
        selectIndex(m_current + 1);
}

// ---- Project I/O -----------------------------------------------------------

void MainWindow::openProject()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Project"), m_lastDir,
        tr("wonderscan project (*.wsp *.json)"));
    if (path.isEmpty())
        return;
    loadProjectFile(path);
}

bool MainWindow::loadProjectFile(const QString &path)
{
    Project project;
    QString err;
    if (!ProjectIO::load(path, &project, &err)) {
        QMessageBox::warning(this, tr("Open failed"), err);
        removeFromRecent(path); // drop a stale entry if this came from the list
        return false;
    }

    m_pages = project.pages;
    m_dpi = project.dpi;
    m_jpegQuality = project.jpegQuality;
    m_projectPath = path;

    m_thumbs.clear();
    for (const Page &p : m_pages)
        m_thumbs.push_back(renderThumbBase(p.path));

    m_current = -1;
    rebuildFilmstrip();
    setDirty(false);
    selectIndex(m_pages.isEmpty() ? -1 : 0);
    updateTitle();
    addToRecent(path);
    setLastDir(QFileInfo(path).absolutePath());
    return true;
}

bool MainWindow::saveProject()
{
    if (m_projectPath.isEmpty())
        return saveProjectAs();

    Project project;
    project.pages = m_pages;
    project.dpi = m_dpi;
    project.jpegQuality = m_jpegQuality;

    QString err;
    if (!ProjectIO::save(m_projectPath, project, &err)) {
        QMessageBox::warning(this, tr("Save failed"), err);
        return false;
    }
    setDirty(false);
    addToRecent(m_projectPath);
    setLastDir(QFileInfo(m_projectPath).absolutePath());
    return true;
}

bool MainWindow::saveProjectAs()
{
    const QString suggested = m_lastDir.isEmpty()
                                  ? QStringLiteral("project.wsp")
                                  : QDir(m_lastDir).filePath(QStringLiteral("project.wsp"));
    QString path = QFileDialog::getSaveFileName(
        this, tr("Save Project As"), suggested,
        tr("wonderscan project (*.wsp)"));
    if (path.isEmpty())
        return false;
    if (!path.endsWith(".wsp", Qt::CaseInsensitive)
        && !path.endsWith(".json", Qt::CaseInsensitive))
        path += ".wsp";
    m_projectPath = path;
    updateTitle();
    return saveProject();
}

// ---- Settings / recent projects --------------------------------------------

// Push the current nudge/loupe preferences into the canvas.
void MainWindow::applyEditorSettings()
{
    m_canvas->setNudgeSteps(m_nudgeFine, m_nudgeCoarse, m_nudgeLarge);
    m_canvas->setLoupeZoom(m_loupeZoom);
}

void MainWindow::loadSettings()
{
    QSettings s;
    m_nudgeFine = s.value(kNudgeFineKey, m_nudgeFine).toInt();
    m_nudgeCoarse = s.value(kNudgeCoarseKey, m_nudgeCoarse).toInt();
    m_nudgeLarge = s.value(kNudgeLargeKey, m_nudgeLarge).toInt();
    m_loupeZoom = s.value(kLoupeKey, m_loupeZoom).toDouble();
    m_lastDir = s.value(kLastDirKey).toString();

    applyEditorSettings();
    updateRecentMenu();
}

void MainWindow::setLastDir(const QString &dir)
{
    if (dir.isEmpty() || dir == m_lastDir)
        return;
    m_lastDir = dir;
    QSettings().setValue(kLastDirKey, m_lastDir);
}

void MainWindow::addToRecent(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    QSettings s;
    QStringList recent = s.value(kRecentKey).toStringList();
    recent.removeAll(abs);
    recent.prepend(abs);
    while (recent.size() > kMaxRecent)
        recent.removeLast();
    s.setValue(kRecentKey, recent);
    updateRecentMenu();
}

void MainWindow::removeFromRecent(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    QSettings s;
    QStringList recent = s.value(kRecentKey).toStringList();
    if (recent.removeAll(abs) > 0) {
        s.setValue(kRecentKey, recent);
        updateRecentMenu();
    }
}

void MainWindow::updateRecentMenu()
{
    if (!m_recentMenu)
        return;
    m_recentMenu->clear();

    const QStringList recent = QSettings().value(kRecentKey).toStringList();
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
                QSettings().remove(kRecentKey);
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
    auto *fine = makeStep(m_nudgeFine);
    auto *coarse = makeStep(m_nudgeCoarse);
    auto *large = makeStep(m_nudgeLarge);
    form->addRow(tr("Nudge (arrow):"), fine);
    form->addRow(tr("Nudge (Shift):"), coarse);
    form->addRow(tr("Nudge (Ctrl+Shift):"), large);

    auto *loupe = new QDoubleSpinBox(&dlg);
    loupe->setRange(1.5, 12.0);
    loupe->setSingleStep(0.5);
    loupe->setSuffix(QStringLiteral("x"));
    loupe->setValue(m_loupeZoom);
    form->addRow(tr("Loupe zoom:"), loupe);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;

    m_nudgeFine = fine->value();
    m_nudgeCoarse = coarse->value();
    m_nudgeLarge = large->value();
    m_loupeZoom = loupe->value();

    QSettings s;
    s.setValue(kNudgeFineKey, m_nudgeFine);
    s.setValue(kNudgeCoarseKey, m_nudgeCoarse);
    s.setValue(kNudgeLargeKey, m_nudgeLarge);
    s.setValue(kLoupeKey, m_loupeZoom);

    applyEditorSettings();
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
    dpiSpin->setValue(m_dpi);
    dpiSpin->setSuffix(tr(" dpi"));
    form->addRow(tr("Resolution:"), dpiSpin);

    auto *qSlider = new QSlider(Qt::Horizontal, &dlg);
    qSlider->setRange(1, 100);
    qSlider->setValue(m_jpegQuality);
    auto *qLabel = new QLabel(QString::number(m_jpegQuality), &dlg);
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

    m_dpi = dpiSpin->value();
    m_jpegQuality = qSlider->value();
    setDirty(true);
    return true;
}

void MainWindow::exportPdf()
{
    if (m_pages.isEmpty()) {
        QMessageBox::information(this, tr("Export"), tr("No images loaded."));
        return;
    }

    QVector<const Page *> ready;
    int unmarked = 0;
    for (const Page &p : m_pages) {
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
                .arg(unmarked).arg(m_pages.size()).arg(ready.size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (ret != QMessageBox::Yes)
            return;
    }

    if (!promptExportSettings())
        return;

    QString defaultDir = m_lastDir;
    if (defaultDir.isEmpty()) {
        if (!m_projectPath.isEmpty())
            defaultDir = QFileInfo(m_projectPath).absolutePath();
        else if (!m_pages.isEmpty())
            defaultDir = QFileInfo(m_pages.first().path).absolutePath();
    }
    const QString defaultPath = QDir(defaultDir).filePath("wonderscan.pdf");

    const QString out = QFileDialog::getSaveFileName(
        this, tr("Export PDF"), defaultPath, tr("PDF (*.pdf)"));
    if (out.isEmpty())
        return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = PdfExporter::exportPdf(out, ready, m_dpi, m_jpegQuality, &err);
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
    m_dirty = dirty;
    setWindowModified(dirty);
}

bool MainWindow::maybeSave()
{
    if (!m_dirty)
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
    QString name = m_projectPath.isEmpty()
                       ? tr("Untitled")
                       : QFileInfo(m_projectPath).fileName();
    setWindowTitle(tr("wonderscan — %1[*]").arg(name));
}

void MainWindow::updateStatus()
{
    // Rotate-all acts on the whole batch, so it only needs images present.
    const bool anyImages = !m_pages.isEmpty();
    if (m_actRotAllL)
        m_actRotAllL->setEnabled(anyImages);
    if (m_actRotAllR)
        m_actRotAllR->setEnabled(anyImages);

    if (m_current < 0) {
        m_statusLabel->setText(tr("%n image(s) loaded.", "", m_pages.size()));
        return;
    }
    const Page &pg = m_pages[m_current];
    m_statusLabel->setText(
        tr("%1 / %2  —  %3  —  %4  —  %5")
            .arg(m_current + 1)
            .arg(m_pages.size())
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
