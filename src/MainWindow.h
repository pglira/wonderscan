#pragma once

#include <QMainWindow>
#include <QImage>
#include <QVector>

#include "AppSettings.h"
#include "Document.h"
#include "Page.h"

class ImageCanvas;
class PreviewPane;
class LoupeView;
class QListWidget;
class QAction;
class QCheckBox;
class QLabel;
class QMenu;
class PageDetector;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Add image files passed on the command line (e.g. via the .desktop %F).
    void addPaths(const QStringList &paths);

protected:
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private slots:
    void addImagesDialog();
    void addFolderDialog();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    void exportPdf();
    void onFilmstripRow(int row);
    void onFilmstripContextMenu(const QPoint &pos);
    void removeCurrentImage();
    void moveCurrentImageUp();
    void moveCurrentImageDown();
    void takeOverPreviousPoints();
    void autoDetectCorners();
    void autoDetectCornersAll();
    void onPointsChanged();
    void rotateLeft();
    void rotateRight();
    void rotateAllLeft();
    void rotateAllRight();
    void onModeToggled(bool six);
    void onSplitToggled(bool split);
    void goPrev();
    void goNext();
    void openSettingsDialog();

private:
    void setupUi();
    void setupActions();
    void addImages(const QStringList &paths);
    void moveCurrentImage(int delta); // reorder current image within the filmstrip
    void rotateCurrent(int deltaDegrees);
    void rotateAll(int deltaDegrees);
    void selectIndex(int i);
    void refreshCurrent();
    void updatePreview();
    void updateFilmstripItem(int i);
    void rebuildFilmstrip();
    void syncControlsToCurrent();
    bool promptExportSettings();
    bool loadProjectFile(const QString &path);
    void loadSettings();
    void applyEditorSettings();
    void updateRecentMenu();
    void addToRecent(const QString &path);
    void removeFromRecent(const QString &path);
    void setLastDir(const QString &dir);
    void setDirty(bool dirty);
    bool maybeSave();
    void updateTitle();
    void updateStatus();

    // Model (page list, thumbnails, export settings, dirty/project-path state).
    Document m_doc;
    int m_current = -1;                  // selected page index (view state), or -1
    Page::Mode m_lastMode = Page::Four;  // sticky defaults applied to new pages
    bool m_lastSplit = false;

    // Persisted preferences (see AppSettings); cached here because some are read
    // on the preview hot path. lastDir and the recent list live in AppSettings
    // and are read on demand.
    EditorPrefs m_prefs;

    // Current-image working state
    QImage m_currentOriginal; // EXIF-corrected, unrotated
    QImage m_currentRotated;  // full-res rotated (canvas + loupe)
    QImage m_currentProxy;    // downscaled rotated (preview)
    double m_proxyScale = 1.0;

    // Widgets
    QListWidget *m_filmstrip = nullptr;
    ImageCanvas *m_canvas = nullptr;
    PreviewPane *m_preview = nullptr;
    LoupeView *m_loupe = nullptr;
    QMenu *m_recentMenu = nullptr;
    QLabel *m_statusLabel = nullptr;

    // Controls / actions
    QCheckBox *m_chkSixPoint = nullptr;
    QCheckBox *m_chkSplit = nullptr;
    QAction *m_actPrev = nullptr;
    QAction *m_actNext = nullptr;
    QAction *m_actRotL = nullptr;
    QAction *m_actRotR = nullptr;
    QAction *m_actRotAllL = nullptr;
    QAction *m_actRotAllR = nullptr;
    QAction *m_actRemove = nullptr;
    QAction *m_actMoveUp = nullptr;
    QAction *m_actMoveDown = nullptr;
    QAction *m_actTakePrev = nullptr;
    QAction *m_actAutoDetect = nullptr;
    QAction *m_actAutoDetectAll = nullptr;

    PageDetector *m_detector = nullptr; // lazily created on first auto-detect
};
