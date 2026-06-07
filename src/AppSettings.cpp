#include "AppSettings.h"

#include <QFileInfo>
#include <QSettings>

namespace {

constexpr int kMaxRecent = 8;

const QString kRecentKey = QStringLiteral("recentProjects");
const QString kLastDirKey = QStringLiteral("paths/lastDir");
const QString kNudgeFineKey = QStringLiteral("nudge/fine");
const QString kNudgeCoarseKey = QStringLiteral("nudge/coarse");
const QString kNudgeLargeKey = QStringLiteral("nudge/large");
const QString kLoupeKey = QStringLiteral("loupe/zoom");
const QString kEqualWidthsKey = QStringLiteral("render/equalPageWidths");

} // namespace

namespace AppSettings {

EditorPrefs loadEditorPrefs()
{
    const EditorPrefs def;
    QSettings s;
    EditorPrefs p;
    p.nudgeFine = s.value(kNudgeFineKey, def.nudgeFine).toInt();
    p.nudgeCoarse = s.value(kNudgeCoarseKey, def.nudgeCoarse).toInt();
    p.nudgeLarge = s.value(kNudgeLargeKey, def.nudgeLarge).toInt();
    p.loupeZoom = s.value(kLoupeKey, def.loupeZoom).toDouble();
    p.equalPageWidths = s.value(kEqualWidthsKey, def.equalPageWidths).toBool();
    return p;
}

void saveEditorPrefs(const EditorPrefs &prefs)
{
    QSettings s;
    s.setValue(kNudgeFineKey, prefs.nudgeFine);
    s.setValue(kNudgeCoarseKey, prefs.nudgeCoarse);
    s.setValue(kNudgeLargeKey, prefs.nudgeLarge);
    s.setValue(kLoupeKey, prefs.loupeZoom);
    s.setValue(kEqualWidthsKey, prefs.equalPageWidths);
}

QString lastDir()
{
    return QSettings().value(kLastDirKey).toString();
}

void setLastDir(const QString &dir)
{
    if (dir.isEmpty() || dir == lastDir())
        return;
    QSettings().setValue(kLastDirKey, dir);
}

QStringList recentProjects()
{
    return QSettings().value(kRecentKey).toStringList();
}

void addRecentProject(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    QSettings s;
    QStringList recent = s.value(kRecentKey).toStringList();
    recent.removeAll(abs);
    recent.prepend(abs);
    while (recent.size() > kMaxRecent)
        recent.removeLast();
    s.setValue(kRecentKey, recent);
}

bool removeRecentProject(const QString &path)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    QSettings s;
    QStringList recent = s.value(kRecentKey).toStringList();
    if (recent.removeAll(abs) == 0)
        return false;
    s.setValue(kRecentKey, recent);
    return true;
}

void clearRecentProjects()
{
    QSettings().remove(kRecentKey);
}

} // namespace AppSettings
