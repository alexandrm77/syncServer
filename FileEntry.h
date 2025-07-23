#pragma once

#include <QString>
#include <QJsonObject>

struct FileDiff {
    QString path;
    QString type; // "upload", "download", "delete"
    quint64 version;
    int rootIndex = -1;
};

struct FileEntry
{
    QString path;
    QString type; // "file", "directory", "deleted"
    quint64 version;
    int rootIndex;    // индекс в m_syncDirectories

    FileEntry() = default;
    FileEntry(const QString &p, const QString &t, quint64 v, int i)
        : path(p), type(t), version(v), rootIndex(i) {}

    QJsonObject toJson() const {
        QJsonObject obj;
        obj["path"] = path;
        obj["type"] = type;
        obj["version"] = QString::number(version);
        obj["rootIndex"] = rootIndex;
        return obj;
    }

    static FileEntry fromJson(const QJsonObject &obj) {
        return FileEntry(
            obj["path"].toString(),
            obj["type"].toString(),
            obj["version"].toString().toLongLong(),
            obj["rootIndex"].toInt()
            );
    }
};
