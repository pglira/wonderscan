#include "Project.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace ProjectIO {

bool save(const QString &path, const Project &project, QString *error)
{
    QJsonArray imagesArr;
    for (const Page &p : project.pages) {
        QJsonObject o;
        o["path"] = p.path;
        o["rotation"] = p.rotation;
        o["mode"] = (p.mode == Page::Four) ? "four" : "six";
        o["splitSpread"] = p.splitSpread;
        o["marked"] = p.marked;

        QJsonArray pts;
        for (const QPointF &pt : p.points) {
            QJsonArray xy;
            xy.append(pt.x());
            xy.append(pt.y());
            pts.append(xy);
        }
        o["points"] = pts;
        imagesArr.append(o);
    }

    QJsonObject exportObj;
    exportObj["dpi"] = project.dpi;
    exportObj["jpegQuality"] = project.jpegQuality;
    exportObj["inset"] = project.inset;

    QJsonObject root;
    root["version"] = 1;
    root["images"] = imagesArr;
    root["export"] = exportObj;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("Cannot write project: %1").arg(path);
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool load(const QString &path, Project *project, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("Cannot read project: %1").arg(path);
        return false;
    }

    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseErr);
    file.close();
    if (doc.isNull() || !doc.isObject()) {
        if (error)
            *error = QStringLiteral("Invalid project file: %1").arg(parseErr.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    Project result;

    const QJsonObject exportObj = root["export"].toObject();
    result.dpi = exportObj.value("dpi").toInt(300);
    result.jpegQuality = exportObj.value("jpegQuality").toInt(85);
    result.inset = exportObj.value("inset").toInt(0);

    for (const QJsonValue &v : root["images"].toArray()) {
        const QJsonObject o = v.toObject();
        Page p;
        p.path = o["path"].toString();
        p.rotation = o["rotation"].toInt(0);
        p.mode = (o["mode"].toString() == "six") ? Page::Six : Page::Four;
        p.splitSpread = o["splitSpread"].toBool(false);
        p.marked = o["marked"].toBool(false);

        for (const QJsonValue &pv : o["points"].toArray()) {
            const QJsonArray xy = pv.toArray();
            if (xy.size() == 2)
                p.points.append(QPointF(xy[0].toDouble(), xy[1].toDouble()));
        }
        result.pages.append(p);
    }

    *project = result;
    return true;
}

} // namespace ProjectIO
