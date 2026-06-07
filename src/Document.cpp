#include "Document.h"

#include "Project.h"

#include <QColor>
#include <QFileInfo>
#include <QImageReader>

namespace {

constexpr int kThumbSrcMax = 192;  // base thumbnail max side (px)
constexpr int kPlaceholderW = 96;  // fallback thumbnail for unreadable images
constexpr int kPlaceholderH = 120;

// Apply a 90-degree rotation step to a page. The marked corners were positioned
// on the old orientation, so rotating invalidates them.
void rotatePage(Page &p, int deltaDegrees)
{
    p.rotation = ((p.rotation + deltaDegrees) % 360 + 360) % 360;
    p.points.clear();
    p.marked = false;
}

} // namespace

int Document::addImages(const QStringList &paths, Page::Mode mode, bool split)
{
    if (paths.isEmpty())
        return -1;
    const int firstAdded = m_pages.size();
    for (const QString &path : paths) {
        Page p;
        p.path = path;
        p.mode = mode;
        p.splitSpread = split;
        m_pages.push_back(p);
        m_thumbs.push_back(renderThumb(path));
    }
    return firstAdded;
}

void Document::removeAt(int i)
{
    if (i < 0 || i >= m_pages.size())
        return;
    m_pages.remove(i);
    m_thumbs.remove(i);
}

int Document::move(int i, int delta)
{
    if (i < 0 || i >= m_pages.size())
        return i;
    const int target = i + delta;
    if (target < 0 || target >= m_pages.size())
        return i;
    m_pages.swapItemsAt(i, target);
    m_thumbs.swapItemsAt(i, target);
    return target;
}

void Document::rotateAt(int i, int deltaDegrees)
{
    if (i < 0 || i >= m_pages.size())
        return;
    rotatePage(m_pages[i], deltaDegrees);
}

void Document::rotateAll(int deltaDegrees)
{
    for (Page &p : m_pages)
        rotatePage(p, deltaDegrees);
}

int Document::markedCount() const
{
    int n = 0;
    for (const Page &p : m_pages)
        if (p.marked)
            ++n;
    return n;
}

bool Document::load(const QString &path, QString *error)
{
    Project project;
    if (!ProjectIO::load(path, &project, error))
        return false;

    m_pages = project.pages;
    m_dpi = project.dpi;
    m_jpegQuality = project.jpegQuality;
    m_path = path;

    m_thumbs.clear();
    m_thumbs.reserve(m_pages.size());
    for (const Page &p : m_pages)
        m_thumbs.push_back(renderThumb(p.path));

    m_dirty = false;
    return true;
}

bool Document::save(const QString &path, QString *error)
{
    Project project;
    project.pages = m_pages;
    project.dpi = m_dpi;
    project.jpegQuality = m_jpegQuality;

    if (!ProjectIO::save(path, project, error))
        return false;

    m_path = path;
    m_dirty = false;
    return true;
}

QStringList Document::missingImagePaths() const
{
    QStringList missing;
    for (const Page &p : m_pages)
        if (!QFileInfo::exists(p.path))
            missing << p.path;
    return missing;
}

QImage Document::renderThumb(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QSize sz = reader.size();
    if (sz.isValid()) {
        sz.scale(kThumbSrcMax, kThumbSrcMax, Qt::KeepAspectRatio);
        reader.setScaledSize(sz);
    }
    QImage img = reader.read();
    if (img.isNull()) {
        QImage placeholder(kPlaceholderW, kPlaceholderH, QImage::Format_RGB888);
        placeholder.fill(QColor(90, 70, 70));
        return placeholder;
    }
    return img;
}
