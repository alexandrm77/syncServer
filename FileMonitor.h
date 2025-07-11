#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QHash>
#include <QDir>
#include "FileEntry.h"

class FileMonitor : public QObject
{
    Q_OBJECT
public:
    explicit FileMonitor(const QString &directory, QObject *parent = nullptr);

    void start();
    QList<FileEntry> currentFiles() const;

signals:
    void fileChanged(const FileEntry &entry);  // Изменён/добавлен
    void fileRemoved(const QString &relativePath); // Удалён

private slots:
    void onFileChanged(const QString &path);
    void onDirectoryChanged(const QString &path);

private:
    QString m_directory;
    QFileSystemWatcher m_watcher;
    QHash<QString, FileEntry> m_currentFiles;

    void rescan();                      // полная пересборка
    void updateWatchList();            // пересчитать файлы и каталоги
    FileEntry getFileEntry(const QString &fullPath) const;
};
