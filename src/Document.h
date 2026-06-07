#pragma once

#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

#include "Page.h"

// The editable document: the ordered page list, the matching base thumbnails,
// the export settings, and the association with an on-disk project file.
//
// This is pure model state with no UI dependencies. Collection-level operations
// (add / remove / reorder / rotate / count / load / save) live here; per-page
// corner editing stays with the controller, which reaches a page through the
// mutable page() accessor.
//
// The dirty flag tracks unsaved changes. Collection mutators here do NOT set it
// (the controller marks the document dirty so it can also update window chrome);
// load() and save() clear it, since both establish a clean on-disk state.
class Document {
public:
    // ---- page collection ----------------------------------------------------
    bool isEmpty() const { return m_pages.isEmpty(); }
    int pageCount() const { return m_pages.size(); }
    const QVector<Page> &pages() const { return m_pages; }
    Page &page(int i) { return m_pages[i]; }
    const Page &page(int i) const { return m_pages[i]; }
    QImage thumb(int i) const { return m_thumbs.value(i); } // null if out of range

    // Append the (already format-validated) image paths, each with the given
    // default mode/split and a freshly rendered base thumbnail. Returns the
    // index of the first appended page, or -1 if `paths` was empty.
    int addImages(const QStringList &paths, Page::Mode mode, bool split);

    void removeAt(int i);
    // Move page i by `delta` rows (swapping with the neighbour). Returns the new
    // index of that page (== i if the move was a no-op / out of range).
    int move(int i, int delta);
    void rotateAt(int i, int deltaDegrees); // clears that page's corners
    void rotateAll(int deltaDegrees);       // clears every page's corners

    int markedCount() const;
    bool anyMarked() const { return markedCount() > 0; }

    // ---- export settings (persisted in the project) -------------------------
    int dpi() const { return m_dpi; }
    void setDpi(int dpi) { m_dpi = dpi; }
    int jpegQuality() const { return m_jpegQuality; }
    void setJpegQuality(int q) { m_jpegQuality = q; }
    // Inward offset (px, rotated-image space): export shrinks each marked quad by
    // this much, so the marked corners can sit on the page edge but the messy
    // boundary is cropped out. 0 = export the marked quad as-is.
    int inset() const { return m_inset; }
    void setInset(int px) { m_inset = px; }

    // ---- identity / dirty ---------------------------------------------------
    QString path() const { return m_path; }
    void setPath(const QString &path) { m_path = path; }
    bool isUntitled() const { return m_path.isEmpty(); }
    bool dirty() const { return m_dirty; }
    void setDirty(bool dirty) { m_dirty = dirty; }

    // ---- persistence (wraps ProjectIO) --------------------------------------
    // Replace all contents from `path`, regenerate thumbnails, set path, clear
    // dirty. Returns false (and sets *error) on failure, leaving the document
    // unchanged.
    bool load(const QString &path, QString *error);
    // Write to `path`, set it as the document's path, clear dirty.
    bool save(const QString &path, QString *error);

    // Absolute/stored paths of pages whose image file no longer exists on disk
    // (used to warn after loading a project with moved/renamed images).
    QStringList missingImagePaths() const;

    // Base (EXIF-corrected, unrotated) thumbnail for an image path, scaled to
    // fit the thumbnail box; a flat placeholder if the file can't be read.
    static QImage renderThumb(const QString &path);

private:
    QVector<Page> m_pages;
    QVector<QImage> m_thumbs; // base thumbnails (EXIF-corrected, unrotated), 1:1 with pages
    int m_dpi = 300;
    int m_jpegQuality = 85;
    int m_inset = 20;         // inward export offset in px (0 = off); new-doc default
    QString m_path;           // project file path ("" until first save)
    bool m_dirty = false;
};
