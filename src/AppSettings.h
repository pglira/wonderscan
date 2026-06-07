#pragma once

#include <QString>
#include <QStringList>

// Persistent application preferences, backed by QSettings. This is the single
// place that knows the QSettings key names and storage rules; the rest of the
// app deals in plain values. Three groups of state:
//   - editor preferences (corner-nudge steps, loupe zoom, spread widths),
//   - the last-used directory for file dialogs,
//   - the most-recently-opened project list.

// Editor preferences, loaded/saved as a unit. Defaults match a fresh install.
struct EditorPrefs {
    int nudgeFine = 1;          // arrow-key nudge (image px)
    int nudgeCoarse = 10;       // ... with Shift
    int nudgeLarge = 25;        // ... with Ctrl+Shift
    double loupeZoom = 2.0;     // loupe magnification
    bool equalPageWidths = true; // force equal widths for 6-point spread halves
};

namespace AppSettings {

EditorPrefs loadEditorPrefs();
void saveEditorPrefs(const EditorPrefs &prefs);

// Last folder used in open/save/export dialogs ("" if never set).
QString lastDir();
void setLastDir(const QString &dir);

// Most-recently-opened projects, newest first (stored as absolute paths).
QStringList recentProjects();
void addRecentProject(const QString &path);    // dedups, prepends, caps the list
bool removeRecentProject(const QString &path); // true if an entry was removed
void clearRecentProjects();

} // namespace AppSettings
