#include "FileMonitor.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

FileMonitor::FileMonitor(const QStringList &directories, QObject *parent)
    : QObject(parent),
    m_directories(directories)
{
    for (QString &dir : m_directories)
        dir = QDir(dir).absolutePath();

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
    rescan();
    updateWatchList();
}

void FileMonitor::rescan()
{
    QHash<QString, FileEntry> newFiles;

    for (const QString &rootDir : m_directories) {
        QDir dir(rootDir);

        QDirIterator it(dir.absolutePath(), QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString fullPath = it.next();
            QString relative = makeRelativePath(rootDir, fullPath);
            FileEntry entry = getFileEntry(rootDir, fullPath);
            newFiles[relative] = entry;

            if (!m_currentFiles.contains(relative) || m_currentFiles[relative].version != entry.version) {
                emit fileChanged(entry);
            }
        }
    }

    // Найдём удалённые
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

    QSet<QString> allDirs;
    for (const QString &relative : m_currentFiles.keys()) {
        const FileEntry &entry = m_currentFiles[relative];
        for (const QString &root : m_directories) {
            QString fullPath = QDir(root).absoluteFilePath(relative);
            if (QFileInfo::exists(fullPath)) {
                m_watcher.addPath(fullPath);

                // Добавим директории вверх по дереву
                QFileInfo info(fullPath);
                QDir parent = info.dir();
                while (parent.path().startsWith(root)) {
                    allDirs.insert(parent.path());
                    if (parent.path() == root)
                        break;
                    parent.cdUp();
                }
                break;
            }
        }
    }

    for (const QString &dirPath : allDirs)
        m_watcher.addPath(dirPath);
}

FileEntry FileMonitor::getFileEntry(const QString &rootDir, const QString &fullPath) const
{
    QFileInfo info(fullPath);
    QString relativePath = makeRelativePath(rootDir, fullPath);
    QString type = info.suffix();
    int version = static_cast<int>(info.lastModified().toMSecsSinceEpoch() / 1000);
    return FileEntry(relativePath, type, version);
}

QString FileMonitor::makeRelativePath(const QString &rootDir, const QString &fullPath) const
{
    return QDir(rootDir).relativeFilePath(fullPath);
}

void FileMonitor::onFileChanged(const QString &path)
{
    QFileInfo info(path);
    if (!info.exists()) {
        for (const QString &root : m_directories) {
            QString relative = makeRelativePath(root, path);
            if (m_currentFiles.contains(relative)) {
                emit fileRemoved(relative);
                m_currentFiles.remove(relative);
                break;
            }
        }
        updateWatchList();
        return;
    }

    for (const QString &root : m_directories) {
        if (path.startsWith(root)) {
            FileEntry updated = getFileEntry(root, path);
            QString relative = updated.path;
            m_currentFiles[relative] = updated;
            emit fileChanged(updated);
            break;
        }
    }
}

void FileMonitor::onDirectoryChanged(const QString &)
{
    rescan();
    updateWatchList();
}
