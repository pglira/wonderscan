#pragma once

#include <QString>
#include <QVector>

#include "Page.h"

// On-disk project: the list of pages plus export settings.
struct Project {
    QVector<Page> pages;
    int dpi = 300;
    int jpegQuality = 85;
};

namespace ProjectIO {

bool save(const QString &path, const Project &project, QString *error);
bool load(const QString &path, Project *project, QString *error);

} // namespace ProjectIO
