#pragma once

#include <QMainWindow>
#include <QImage>
#include <QVector>

#include "Page.h"

class ImageCanvas;
class PreviewPane;
class QListWidget;
class QAction;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

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
    void onPointsChanged();
    void rotateLeft();
    void rotateRight();
    void rotateAllLeft();
    void rotateAllRight();
    void onModeToggled(bool six);
    void onSplitToggled(bool split);
    void goPrev();
    void goNext();

private:
    void setupUi();
    void setupActions();
    void addImages(const QStringList &paths);
    void rotateCurrent(int deltaDegrees);
    void rotateAll(int deltaDegrees);
    void selectIndex(int i);
    void refreshCurrent();
    void updatePreview();
    void updateFilmstripItem(int i);
    QImage renderThumbBase(const QString &path) const;
    void rebuildFilmstrip();
    void syncControlsToCurrent();
    bool promptExportSettings();
    void setDirty(bool dirty);
    bool maybeSave();
    void updateTitle();
    void updateStatus();

    // Model
    QVector<Page> m_pages;
    QVector<QImage> m_thumbs; // base thumbnails (EXIF-corrected, unrotated)
    int m_current = -1;
    QString m_projectPath;
    bool m_dirty = false;
    int m_dpi = 300;
    int m_jpegQuality = 85;
    Page::Mode m_lastMode = Page::Four;
    bool m_lastSplit = false;

    // Current-image working state
    QImage m_currentOriginal; // EXIF-corrected, unrotated
    QImage m_currentRotated;  // full-res rotated (canvas + loupe)
    QImage m_currentProxy;    // downscaled rotated (preview)
    double m_proxyScale = 1.0;

    // Widgets
    QListWidget *m_filmstrip = nullptr;
    ImageCanvas *m_canvas = nullptr;
    PreviewPane *m_preview = nullptr;
    QDoubleSpinBox *m_loupeSpin = nullptr;
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
};
