#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QDir>
#include <QTimer>
#include "FileEntry.h"

class FileMonitor : public QObject
{
    Q_OBJECT
public:
    explicit FileMonitor(const QStringList &directories, QObject *parent = nullptr);

    void start();
    QList<FileEntry> currentFiles() const;

signals:
    void fileChanged(const FileEntry &entry);           // Изменён/добавлен
    void fileRemoved(const QString &relativePath);      // Удалён

private slots:
    void onFileChanged(const QString &path);
    void onDirectoryChanged(const QString &path);

private:
    QStringList m_directories;
    QFileSystemWatcher m_watcher;
    QHash<QString, FileEntry> m_currentFiles;
    QTimer m_rescanTimer;

    void rescan();
    void updateWatchList();
    FileEntry getFileEntry(const QString &rootDir, const QString &fullPath) const;
    QString makeRelativePath(const QString &rootDir, const QString &fullPath) const;
};
