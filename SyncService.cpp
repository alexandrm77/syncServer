#include "SyncService.h"
#include "FileMonitor.h"
#include <QTcpSocket>
#include <QUdpSocket>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDirIterator>
#include <QDateTime>
#include <QUrl>

static QString makeKey(int rootIndex, const QString &relativePath) {
    return QString::number(rootIndex) + ":" + relativePath;
}

SyncService::SyncService(const QHostAddress &serverAddress,
                         quint16 serverPort,
                         QObject *parent)
    : QObject(parent), m_serverAddress(serverAddress), m_serverPort(serverPort)
{
    m_pingTimer.setInterval(30 * 1000); // 30 секунд
    connect(&m_pingTimer, &QTimer::timeout, this, &SyncService::sendPing);

    m_syncDirectories.append(QDir::homePath() + "/test/client/fold1");
    m_syncDirectories.append(QDir::homePath() + "/test/client/fold2");
    m_monitor = new FileMonitor(m_syncDirectories, this);

    connect(m_monitor, &FileMonitor::fileChanged, this, [=](const FileEntry &entry){
        QString key = makeKey(entry.rootIndex, entry.path);
        qDebug() << "Изменён/добавлен:" << key << entry.version;
        if (m_ignoreNextChange.contains(key)) {
            qDebug() << "Ignoring fileChanged for:" << key;
            m_ignoreNextChange.remove(key);
            return;
        }
        sendSyncListToServer({ entry });
    });

    connect(m_monitor, &FileMonitor::fileRemoved, this, [=](const FileEntry &entry){
        QString key = makeKey(entry.rootIndex, entry.path);
        if (m_ignoreNextChange.contains(key)) {
            qDebug() << "Ignoring fileRemoved for:" << key;
            m_ignoreNextChange.remove(key);
            return;
        }

        qDebug() << "Удалён:" << key;

        FileEntry deletedEntry = entry;
        deletedEntry.version = 0;
        deletedEntry.type = "deleted";
        deletedEntry.rootIndex = entry.rootIndex;

        sendSyncListToServer({ deletedEntry });

        // Дополнительно отправим POST /delete
        sendDeleteRequest(deletedEntry);
    });

    m_monitor->start();
}

void SyncService::start()
{
    qDebug() << "SyncService started";
    synchronizeWithServer();
    m_pingTimer.start();
    sendPing(); // первый ping сразу

    // Запуск TCP-сервера для приёма /notify
    if (!m_server.listen(QHostAddress::AnyIPv4, 9090)) {
        qCritical() << "Failed to start local server on port 9090";
    } else {
        qDebug() << "Listening for incoming connections on port 9090";
        connect(&m_server, &QTcpServer::newConnection,
                this, &SyncService::handleNewConnection);
    }
}

void SyncService::synchronizeWithServer()
{
    qDebug() << "Starting initial sync with server...";

    QList<FileEntry> localEntries = scanLocalDirectories();
    sendSyncListToServer(localEntries);
}

QList<FileEntry> SyncService::scanLocalDirectories()
{
    QList<FileEntry> entries;

    for (int rootIndex = 0; rootIndex < m_syncDirectories.size(); ++rootIndex) {
        const QString &dirPath = m_syncDirectories[rootIndex];
        QDir root(dirPath);
        QDirIterator it(dirPath, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        const int basePathLength = root.absolutePath().length() + 1;

        while (it.hasNext()) {
            it.next();
            const QFileInfo file = it.fileInfo();

            FileEntry entry;
            entry.path = file.filePath().mid(basePathLength);
            entry.rootIndex = rootIndex;
            entry.version = (file.lastModified().toMSecsSinceEpoch() / 1000);
            entry.type = "file";
            entries.append(entry);
        }
    }

    return entries;
}

void SyncService::discoverAndStart(QObject *parent)
{
    auto socket = new QUdpSocket(parent);
    socket->bind(QHostAddress::AnyIPv4, 0, QUdpSocket::ShareAddress);

    auto timer = new QTimer(parent);
    timer->setInterval(3000); // каждые 3 секунды

    QObject::connect(timer, &QTimer::timeout, [socket]() {
        QByteArray message = "DISCOVER_REQUEST";
        socket->writeDatagram(message, QHostAddress::Broadcast, 45454);
        qDebug() << "Broadcasted DISCOVER_REQUEST";
    });

    QObject::connect(socket, &QUdpSocket::readyRead, [socket, parent, timer]() {
        while (socket->hasPendingDatagrams()) {
            QByteArray buffer;
            buffer.resize(socket->pendingDatagramSize());

            QHostAddress sender;
            quint16 senderPort;
            socket->readDatagram(buffer.data(), buffer.size(), &sender, &senderPort);

            if (buffer == "DISCOVER_RESPONSE") {
                qDebug() << "Discovered SyncServer at" << sender.toString();

                timer->stop();
                timer->deleteLater();

                auto tcpSocket = new QTcpSocket(parent);
                QObject::connect(tcpSocket, &QTcpSocket::connected, [tcpSocket]() {
                    QByteArray req = "GET /register HTTP/1.1\r\nHost: server\r\nConnection: close\r\n\r\n";
                    tcpSocket->write(req);
                });

                QObject::connect(tcpSocket, &QTcpSocket::readyRead, [tcpSocket, sender, parent]() {
                    QByteArray response = tcpSocket->readAll();
                    qDebug() << "Response:\n" << response;

                    auto syncService = new SyncService(sender, 8080, parent);
                    syncService->start();
                    QObject::connect(syncService, &SyncService::connectionLost, parent, [syncService, parent]() {
                        syncService->deleteLater();
                        qWarning() << "Connection lost. Rediscovering...";
                        SyncService::discoverAndStart(parent);
                    });

                    QObject::connect(tcpSocket, &QTcpSocket::disconnected, tcpSocket, &QObject::deleteLater);
                    tcpSocket->disconnectFromHost();
                });

                tcpSocket->connectToHost(sender, 8080);

                QObject::disconnect(socket, nullptr, nullptr, nullptr);
                socket->deleteLater();
                return;
            }
        }
    });

    timer->start();
    QByteArray message = "DISCOVER_REQUEST";
    socket->writeDatagram(message, QHostAddress::Broadcast, 45454);
    qDebug() << "Broadcasted initial DISCOVER_REQUEST";
}

void SyncService::sendPing()
{
    QTcpSocket *socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, [=]() {
        socket->write("GET /ping HTTP/1.1\r\nHost: sync\r\nConnection: close\r\n\r\n");
    });
    connect(socket, &QTcpSocket::readyRead, this, [=]() {
        QByteArray response = socket->readAll(); // можно игнорировать
        qDebug() << "Response:\n" << response;
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(onPingSocketError(QAbstractSocket::SocketError)));
    socket->connectToHost(m_serverAddress, m_serverPort);
}

void SyncService::onPingSocketError(QAbstractSocket::SocketError socketError)
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (!socket)
        return;

    socket->deleteLater();

    if (socketError == QAbstractSocket::ConnectionRefusedError ||
        socketError == QAbstractSocket::HostNotFoundError) {
        qWarning() << "Ping failed with error:" << socket->errorString();
        emit connectionLost();
    }
}

void SyncService::sendSyncListToServer(const QList<FileEntry> &files)
{
    QJsonArray arr;
    for (const FileEntry &entry : files) {
        QJsonObject obj;
        obj["path"] = entry.path;
        obj["version"] = QString::number(entry.version);
        obj["type"] = entry.type;
        obj["rootIndex"] = QString::number(entry.rootIndex);
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    QByteArray body = doc.toJson();

    QByteArray request;
    request += "POST /sync-list HTTP/1.1\r\n";
    request += "Host: dummy\r\n";
    request += "Content-Type: application/json\r\n";
    request += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    request += "Connection: close\r\n\r\n";
    request += body;

    QTcpSocket *socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, [=]() {
        socket->write(request);
    });

    connect(socket, &QTcpSocket::readyRead, [=]() {
        QByteArray response = socket->readAll();
        qDebug() << "[SyncService] Response to sync-list:\n" << response;

        if (response.contains("200 OK")) {
            QVector<FileDiff> diffs = parseDiffs(response);
            if (!diffs.isEmpty()) {
                onResponse(diffs);
            } else {
                if (response.contains("Up to date")) {
                    return;
                }

                // fallback: сервер ничего не вернул, загружаем сами
                for (const FileEntry &entry : files) {
                    if (entry.type != "deleted")
                        uploadFile(entry);
                }
            }
        }
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, SIGNAL(error(QAbstractSocket::SocketError)),
            this, SLOT(handleSocketError(QAbstractSocket::SocketError)));

    socket->connectToHost(m_serverAddress, m_serverPort);
}

QVector<FileDiff> SyncService::parseDiffs(const QByteArray& response)
{
    QVector<FileDiff> diffs;

    // Отделяем тело от заголовков
    int headerEndIndex = response.indexOf("\r\n\r\n");
    if (headerEndIndex == -1)
        return diffs;

    QByteArray body = response.mid(headerEndIndex + 4);  // после \r\n\r\n
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);

    if (parseError.error != QJsonParseError::NoError || !doc.isArray())
        return diffs;

    QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();

        FileDiff diff;
        diff.path = obj["path"].toString();
        diff.version = obj["version"].toString().toLongLong();
        diff.type = obj["type"].toString();  // "upload" или "download"
        diff.rootIndex = obj["rootIndex"].toInt();
        diffs.append(diff);
    }

    return diffs;
}

QString SyncService::resolveFullPath(int rootIndex, const QString &relativePath) const
{
    if (rootIndex < 0 || rootIndex >= m_syncDirectories.size())
        return QString();

    QString dir = m_syncDirectories[rootIndex];
    QString fullPath = QDir(dir).filePath(relativePath);
    return fullPath;
}

void SyncService::onResponse(const QVector<FileDiff> &diffs)
{
    for (const FileDiff &diff : diffs) {
        if (diff.type == "download") {
            getFile(diff.rootIndex, diff.path);
        } else if (diff.type == "upload") {
            FileEntry entry;
            entry.path = diff.path;
            entry.version = diff.version;
            entry.type = "file";
            entry.rootIndex = diff.rootIndex;
            uploadFile(entry);
        } else if (diff.type == "delete") {
            qDebug() << "Deleting file per server instruction:" << diff.path;
            QString fullPath = resolveFullPath(diff.rootIndex, diff.path);
            if (!fullPath.isEmpty()) {
                QFile::remove(fullPath);

                QString key;
                // Сформируем ключ для игнорирования
                for (int i = 0; i < m_syncDirectories.size(); ++i) {
                    QDir dir(m_syncDirectories[i]);
                    QString rootDirName = dir.dirName();
                    if (diff.path.startsWith(rootDirName + "/")) {
                        key = makeKey(i, diff.path);
                        break;
                    }
                }

                if (!key.isEmpty())
                    m_ignoreNextChange.insert(key);
            }
        } else {
            qWarning() << "Unknown diff type:" << diff.type;
        }
    }
}

void SyncService::handleSocketError(QAbstractSocket::SocketError err)
{
    QTcpSocket *socket = qobject_cast<QTcpSocket *>(sender());
    if (socket) {
        qWarning() << "SyncService: socket error:" << err << socket->errorString();
    }
}

void SyncService::uploadFile(const FileEntry &entry)
{
    QString fullPath = resolveFullPath(entry.rootIndex, entry.path);
    qDebug()<<Q_FUNC_INFO<<"fullPath "<<fullPath;
    if (fullPath.isEmpty()) {
        qWarning() << "uploadFile: cannot resolve full path for" << entry.path;
        return;
    }

    QFile *file = new QFile(fullPath, this);
    if (!file->open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file for upload:" << entry.path;
        file->deleteLater();
        return;
    }

    QTcpSocket *socket = new QTcpSocket(this);
    const qint64 chunkSize = 64 * 1024; // 64 Кб

    // Сохраняем состояние в захвате
    connect(socket, &QTcpSocket::connected, this, [=]() mutable {
        // Отправляем HTTP заголовки
        QByteArray headers;
        headers += "POST /upload HTTP/1.1\r\n";
        headers += "Host: syncserver\r\n";
        headers += "Content-Length: " + QByteArray::number(file->size()) + "\r\n";
        headers += "X-File-Path: " + entry.path.toUtf8() + "\r\n";
        headers += "X-File-Version: " + QByteArray::number(entry.version) + "\r\n";
        headers += "X-File-Type: " + entry.type.toUtf8() + "\r\n";
        headers += "X-File-Root-Index: " + QByteArray::number(entry.rootIndex) + "\r\n";
        headers += "Content-Type: application/octet-stream\r\n";
        headers += "Connection: close\r\n\r\n";

        socket->write(headers);
        socket->flush();
    });

    // После записи — отправляем чанки файла
    // connect(socket, &QTcpSocket::bytesWritten, this, [=]() mutable {
    //     if (!file->atEnd()) {
    //         QByteArray chunk = file->read(chunkSize);
    //         socket->write(chunk);
    //     }
    // });

    auto done = QSharedPointer<bool>::create(false);

    auto sendChunk = [socket, file, done]() {
        qDebug()<<Q_FUNC_INFO<<"In sendChunk";
        if (*done) return;

        if (!file->atEnd()) {
            qDebug()<<Q_FUNC_INFO<<"In sendChunk, write";
            QByteArray chunk = file->read(chunkSize);
            /*int bytes = */socket->write(chunk);
            //qDebug()<<"written bytes = "<<bytes;
        } else {
            qDebug()<<Q_FUNC_INFO<<"In sendChunk, close";
            file->close();
            file->deleteLater();
            *done = true;
            socket->disconnectFromHost();
        }
    };

    // Отправляем первый чанк сразу
    sendChunk();

    // Подключаемся к сигналу, чтобы продолжать отправку, когда сокет готов
    QObject::connect(socket, &QTcpSocket::bytesWritten, socket, [sendChunk]() {
        sendChunk();
    });


    connect(socket, &QTcpSocket::readyRead, this, [=]() {
        QByteArray response = socket->readAll();
        qDebug() << "Upload response:" << response;
    });

    connect(socket, &QTcpSocket::disconnected, this, [=]() {
        socket->deleteLater();
        qDebug() << "Upload finished:" << entry.path;
    });

    socket->connectToHost(m_serverAddress, m_serverPort);
}

void SyncService::getFile(int rootIndex, const QString &relativePath)
{
    qDebug()<<Q_FUNC_INFO<<"rootIndex "<<rootIndex<<" relativePath "<<relativePath;
    QString fullPath = resolveFullPath(rootIndex, relativePath);
    if (fullPath.isEmpty()) {
        qWarning() << "getFile: cannot resolve full path for" << relativePath;
        return;
    }

    QTcpSocket *socket = new QTcpSocket(this);
    QByteArray *buffer = new QByteArray;
    QFile *file = new QFile(fullPath);
    auto headerParsed = QSharedPointer<bool>::create(false);

    connect(socket, &QTcpSocket::connected, [=]() {
        QByteArray request;
        request += "GET /download?path=" + QUrl::toPercentEncoding(relativePath)
                   + "&rootIndex=" + QByteArray::number(rootIndex)
                   + " HTTP/1.1\r\n";
        request += "Host: syncserver\r\n";
        request += "Connection: close\r\n\r\n";
        socket->write(request);
    });

    connect(socket, &QTcpSocket::readyRead, [=]() {
        qDebug()<<Q_FUNC_INFO<<"In readyRead";
        buffer->append(socket->readAll());

        if (!*headerParsed) {
            int headerEndIndex = buffer->indexOf("\r\n\r\n");
            if (headerEndIndex == -1)
                return;

            *headerParsed = true;
            QByteArray body = buffer->mid(headerEndIndex + 4);
            buffer->clear();  // освобождаем память
            buffer->append(body);

            QDir().mkpath(QFileInfo(fullPath).absolutePath());
            if (!file->open(QIODevice::WriteOnly)) {
                qWarning() << "Failed to save downloaded file:" << fullPath;
                delete buffer;
                file->deleteLater();
                socket->disconnectFromHost();
                return;
            }
qDebug()<<Q_FUNC_INFO<<"In readyRead, !*headerParsed, write";
            file->write(*buffer);
            buffer->clear();
        }
        else if (file->isOpen()) {
            qDebug()<<Q_FUNC_INFO<<"In readyRead, headerParsed already, just write";
            file->write(*buffer);
            buffer->clear();
        }
    });

    connect(socket, &QTcpSocket::disconnected, [=]() {
        qDebug()<<Q_FUNC_INFO<<"socket disconnected";
        if (file->isOpen()) {
            file->close();
            qDebug() << "Downloaded file:" << file->fileName();
        }

        // Игнорируем следующий change
        QString key;
        for (int i = 0; i < m_syncDirectories.size(); ++i) {
            QDir dir(m_syncDirectories[i]);
            QString rootDirName = dir.dirName();
            if (relativePath.startsWith(rootDirName + "/")) {
                key = makeKey(i, relativePath);
                break;
            }
        }
        if (!key.isEmpty())
            m_ignoreNextChange.insert(key);

        delete buffer;
        file->deleteLater();
        socket->deleteLater();
    });

    socket->connectToHost(m_serverAddress, m_serverPort);
}

void SyncService::handleNotify(QTcpSocket *socket, const QByteArray &body)
{
    Q_UNUSED(socket)
    QJsonDocument doc = QJsonDocument::fromJson(body);
    if (!doc.isObject()) {
        qWarning() << "Invalid JSON in /notify";
        return;
    }

    QJsonObject obj = doc.object();
    QString path = obj.value("path").toString();
    bool deleted = obj.value("deleted").toBool();
    int rootIndex = obj.value("rootIndex").toInt();

    if (path.isEmpty())
        return;

    if (deleted) {
        qDebug() << "Received deletion notification for" << path;
        QString fullPath = resolveFullPath(rootIndex, path);
        if (!fullPath.isEmpty()) {
            QFile::remove(fullPath);

            QString key;
            for (int i = 0; i < m_syncDirectories.size(); ++i) {
                QDir dir(m_syncDirectories[i]);
                QString rootDirName = dir.dirName();
                if (path.startsWith(rootDirName + "/")) {
                    key = makeKey(i, path);
                    break;
                }
            }

            if (!key.isEmpty())
                m_ignoreNextChange.insert(key);
        }
    } else {
        qDebug() << "Received update notification for" << path;
        QString key;
        for (int i = 0; i < m_syncDirectories.size(); ++i) {
            QDir dir(m_syncDirectories[i]);
            QString rootDirName = dir.dirName();
            if (path.startsWith(rootDirName + "/")) {
                key = makeKey(i, path);
                break;
            }
        }
        if (!key.isEmpty())
            m_ignoreNextChange.insert(key);

        getFile(rootIndex, path);
    }
}

void SyncService::handleNewConnection()
{
    while (m_server.hasPendingConnections())
    {
        QTcpSocket *clientSocket = m_server.nextPendingConnection();
        connect(clientSocket, &QTcpSocket::readyRead, this, [=]() {
            QByteArray data = clientSocket->readAll();

            // Минимальный разбор HTTP-запроса
            int headerEnd = data.indexOf("\r\n\r\n");
            if (headerEnd == -1)
                return;

            QByteArray headers = data.left(headerEnd);
            QByteArray body = data.mid(headerEnd + 4);

            QString requestLine = QString::fromUtf8(headers).split("\r\n").value(0);
            QString method = requestLine.section(' ', 0, 0);
            QString path   = requestLine.section(' ', 1, 1);

            if (method == "POST" && path == "/notify") {
                handleNotify(clientSocket, body);
            } else {
                // Ответ по умолчанию
                clientSocket->write("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
                clientSocket->disconnectFromHost();
            }
        });

        connect(clientSocket, &QTcpSocket::disconnected, clientSocket, &QObject::deleteLater);
    }
}

void SyncService::sendDeleteRequest(const FileEntry &entry)
{
    QString fullPath = resolveFullPath(entry.rootIndex, entry.path);
    if (fullPath.isEmpty()) {
        qWarning() << "sendDeleteRequest: cannot resolve full path for" << entry.path;
        return;
    }

    QTcpSocket *socket = new QTcpSocket(this);

    connect(socket, &QTcpSocket::connected, [=]() {
        QByteArray request;
        request += "POST /delete HTTP/1.1\r\n";
        request += "Host: syncserver\r\n";
        request += "X-File-Path: " + entry.path.toUtf8() + "\r\n";
        request += "X-File-Root-Index: " + QByteArray::number(entry.rootIndex) + "\r\n";
        request += "Connection: close\r\n\r\n";
        socket->write(request);
    });

    connect(socket, &QTcpSocket::readyRead, [=]() {
        QByteArray response = socket->readAll();
        qDebug() << "Delete response:" << response;
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

    socket->connectToHost(m_serverAddress, m_serverPort);
}
