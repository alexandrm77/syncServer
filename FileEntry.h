#pragma once

#include <QString>
#include <QJsonObject>

struct FileDiff {
    QString path;
    int version;
    QString type; // "upload", "download", "delete"
};

struct FileEntry
{
    QString path;
    QString type; // "file", "directory", "deleted"
    int version;

    FileEntry() = default;
    FileEntry(const QString &p, const QString &t, int v)
        : path(p), type(t), version(v) {}

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["path"] = path;
        obj["type"] = type;
        obj["version"] = version;
        return obj;
    }

    static FileEntry fromJson(const QJsonObject &obj) {
        return FileEntry(
            obj["path"].toString(),
            obj["type"].toString(),
            obj["version"].toInt()
            );
    }
};
