#include "FileMonitor.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>

FileMonitor::FileMonitor(const QString &directory, QObject *parent)
    : QObject(parent), m_directory(QDir(directory).absolutePath())
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &FileMonitor::onFileChanged);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &FileMonitor::onDirectoryChanged);

    m_rescanTimer.setInterval(5000);
    connect(&m_rescanTimer, &QTimer::timeout, this, &FileMonitor::rescan);
    m_rescanTimer.start();
}

QList<FileEntry> FileMonitor::currentFiles() const
{
    return m_currentFiles.values();
}

void FileMonitor::start()
{
    rescan();           // построить список файлов и директорий
    updateWatchList();  // обновить watcher
}

void FileMonitor::rescan()
{
    QDir dir(m_directory);
    QHash<QString, FileEntry> newFiles;

    QDirIterator it(dir.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString fullPath = it.next();
        QFileInfo info(fullPath);
        QString relative = dir.relativeFilePath(fullPath);
        FileEntry entry = getFileEntry(fullPath);
        newFiles[relative] = entry;

        // новый или обновлённый
        if (!m_currentFiles.contains(relative) || m_currentFiles[relative].version != entry.version) {
            emit fileChanged(entry);
        }
    }

    // найдём удалённые
    for (const QString &oldPath : m_currentFiles.keys()) {
        if (!newFiles.contains(oldPath)) {
            emit fileRemoved(oldPath);
        }
    }

    m_currentFiles = newFiles;
}

void FileMonitor::updateWatchList()
{
    m_watcher.removePaths(m_watcher.files());
    m_watcher.removePaths(m_watcher.directories());

    QDir dir(m_directory);

    // Следим за файлами
    for (const QString &relative : m_currentFiles.keys()) {
        QString fullPath = dir.absoluteFilePath(relative);
        m_watcher.addPath(fullPath);
    }

    // Следим за всеми каталогами (включая родительские)
    QSet<QString> allDirs;
    for (const QString &relative : m_currentFiles.keys()) {
        QFileInfo info(QDir(m_directory).absoluteFilePath(relative));
        QDir parent = info.dir();
        while (parent.path().startsWith(m_directory)) {
            allDirs.insert(parent.path());
            if (parent.path() == m_directory)
                break;
            parent.cdUp();
        }
    }

    for (const QString &dirPath : allDirs)
        m_watcher.addPath(dirPath);
}

FileEntry FileMonitor::getFileEntry(const QString &fullPath) const
{
    QFileInfo info(fullPath);
    QString relativePath = QDir(m_directory).relativeFilePath(fullPath);
    QString type = info.suffix();
    int version = static_cast<int>(info.lastModified().toSecsSinceEpoch());
    return FileEntry(relativePath, type, version);
}

void FileMonitor::onFileChanged(const QString &path)
{
    QFileInfo info(path);
    if (!info.exists()) {
        QString relative = QDir(m_directory).relativeFilePath(path);
        emit fileRemoved(relative);
        m_currentFiles.remove(relative);
        updateWatchList();
        return;
    }

    FileEntry updated = getFileEntry(path);
    QString relative = updated.path;
    m_currentFiles[relative] = updated;
    emit fileChanged(updated);
}

void FileMonitor::onDirectoryChanged(const QString &)
{
    rescan();           // возможно добавлены/удалены файлы
    updateWatchList();  // пересчитать paths
}
