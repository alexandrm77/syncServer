#include "FileMonitor.h"
#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

FileMonitor::FileMonitor(const QStringList &directories, QObject *parent)
    : QObject(parent), m_directories(directories)
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

    for (int rootIndex = 0; rootIndex < m_directories.size(); ++rootIndex) {
        const QString &rootDir = m_directories[rootIndex];
        QDirIterator it(rootDir, QDir::Files, QDirIterator::Subdirectories);

        while (it.hasNext()) {
            QString fullPath = it.next();
            FileEntry entry = getFileEntry(rootIndex, fullPath);
            QString key = makeKey(rootIndex, entry.path);
            newFiles[key] = entry;

            // Новый или обновлённый
            if (!m_firstScan && (!m_currentFiles.contains(key) || m_currentFiles[key].version != entry.version)) {
                emit fileChanged(entry);
            }
        }
    }

    // Найдём удалённые
    for (const QString &oldKey : m_currentFiles.keys()) {
        if (!newFiles.contains(oldKey)) {
            emit fileRemoved(m_currentFiles[oldKey]);
        }
    }

    m_currentFiles = std::move(newFiles);

    if (m_firstScan) {
        m_firstScan = false;
    }
}

void FileMonitor::updateWatchList()
{
    m_watcher.removePaths(m_watcher.files());
    m_watcher.removePaths(m_watcher.directories());

    // Добавляем файлы и каталоги для отслеживания
    for (const FileEntry &entry : m_currentFiles) {
        QString fullPath = m_directories[entry.rootIndex] + "/" + entry.path;
        m_watcher.addPath(fullPath);
    }

    // Добавляем все каталоги для отслеживания (родительские тоже)
    QSet<QString> allDirs;
    for (const FileEntry &entry : m_currentFiles) {
        QFileInfo info(m_directories[entry.rootIndex] + "/" + entry.path);
        QDir parent = info.dir();

        while (true) {
            QString parentPath = parent.absolutePath();
            allDirs.insert(parentPath);
            if (m_directories.contains(parentPath))
                break;
            if (!parent.cdUp())
                break;
        }
    }

    for (const QString &dirPath : allDirs) {
        m_watcher.addPath(dirPath);
    }
}

FileEntry FileMonitor::getFileEntry(int rootIndex, const QString &fullPath) const
{
    QFileInfo info(fullPath);
    QString relativePath = QDir(m_directories[rootIndex]).relativeFilePath(fullPath);
    QString type = info.suffix();
    quint64 version = (info.lastModified().toMSecsSinceEpoch() / 1000);
    return FileEntry(relativePath, type, version, rootIndex);
}

QString FileMonitor::makeKey(int rootIndex, const QString &relativePath) const
{
    return QString::number(rootIndex) + ":" + relativePath;
}

void FileMonitor::onFileChanged(const QString &path)
{
    // Определяем, из какой папки пришло событие
    for (int rootIndex = 0; rootIndex < m_directories.size(); ++rootIndex) {
        const QString &rootDir = m_directories[rootIndex];
        if (path.startsWith(rootDir)) {
            QFileInfo info(path);
            if (!info.exists()) {
                QString relative = QDir(rootDir).relativeFilePath(path);
                QString key = makeKey(rootIndex, relative);
                if (m_currentFiles.contains(key)) {
                    emit fileRemoved(m_currentFiles[key]);
                    m_currentFiles.remove(key);
                    updateWatchList();
                }
                return;
            }

            FileEntry updated = getFileEntry(rootIndex, path);
            QString key = makeKey(rootIndex, updated.path);
            m_currentFiles[key] = updated;
            emit fileChanged(updated);
            return;
        }
    }
}

void FileMonitor::onDirectoryChanged(const QString &)
{
    rescan();
    updateWatchList();
}
