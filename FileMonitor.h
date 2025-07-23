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
    void fileRemoved(const FileEntry &entry);      // Удалён

private slots:
    void onFileChanged(const QString &path);
    void onDirectoryChanged(const QString &path);

private:
    QStringList m_directories;
    QFileSystemWatcher m_watcher;
    QHash<QString, FileEntry> m_currentFiles;
    QTimer m_rescanTimer;
    bool m_firstScan = true;

    void rescan();
    void updateWatchList();
    FileEntry getFileEntry(int rootIndex, const QString &fullPath) const;
    QString makeKey(int rootIndex, const QString &relativePath) const;
};
