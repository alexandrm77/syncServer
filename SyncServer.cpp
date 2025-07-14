#include "SyncServer.h"
#include "FileMonitor.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QTcpSocket>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

SyncServer::SyncServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &SyncServer::handleNewConnection);

    m_cleanupTimer.setInterval(60 * 1000); // раз в минуту
    connect(&m_cleanupTimer, &QTimer::timeout, this, &SyncServer::cleanupInactiveClients);
    m_cleanupTimer.start();

    // Инициализация мониторинга файлов
    m_syncDirectory = QDir::homePath()+ "/test/serv";  // Папка сервера
    m_monitor = new FileMonitor(m_syncDirectory, this);

    connect(m_monitor, &FileMonitor::fileChanged, this, [=](const FileEntry &entry){
        qDebug() << "[SERVER] Изменён/добавлен:" << entry.path << entry.version;

        // Обнови внутреннюю структуру, если есть
        m_fileEntries[entry.path] = entry;

        // Уведомить клиентов
        notifyUpdate(entry.path);
    });

    connect(m_monitor, &FileMonitor::fileRemoved, this, [=](const QString &path){
        qDebug() << "[SERVER] Удалён:" << path;

        m_fileEntries.remove(path);

        // Уведомить клиентов
        notifyUpdate(path);
    });

    m_monitor->start();

    // Заполняем m_fileEntries актуальными файлами из папки
    const auto initialFiles = m_monitor->currentFiles();
    for (const FileEntry &entry : initialFiles) {
        m_fileEntries[entry.path] = entry;
    }
}

bool SyncServer::listen(const QHostAddress &address, quint16 port)
{
    bool ok = m_server.listen(address, port);
    if (ok) {
        qDebug() << "Sync server listening on" << m_server.serverAddress().toString() << ":" << m_server.serverPort();
        emit serverStarted();
    } else {
        qWarning() << "Failed to listen:" << m_server.errorString();
    }
    return ok;
}

void SyncServer::stop()
{
    m_server.close();
    emit serverStopped();
}

void SyncServer::handleNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *clientSocket = m_server.nextPendingConnection();
        qDebug() << "New client connected from" << clientSocket->peerAddress().toString();
        handleClient(clientSocket);
    }
}

void SyncServer::handleClient(QTcpSocket *clientSocket)
{
    connect(clientSocket, &QTcpSocket::readyRead, this, &SyncServer::handleClientReadyRead);
    connect(clientSocket, &QTcpSocket::disconnected, this, &SyncServer::handleClientDisconnected);

    m_clientBuffers[clientSocket] = QByteArray();
}

void SyncServer::handleClientReadyRead()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    QByteArray &buffer = m_clientBuffers[socket];
    buffer += socket->readAll();

    int headerEndIndex = buffer.indexOf("\r\n\r\n");
    if (headerEndIndex == -1) {
        // Ждем ещё данных
        return;
    }

    QByteArray headerPart = buffer.left(headerEndIndex + 4);
    QList<QByteArray> lines = headerPart.split('\n');
    if (lines.isEmpty())
        return;

    QByteArray requestLine = lines.first().trimmed();
    QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() < 2)
        return;

    QByteArray method = parts[0];
    QByteArray path = parts[1];

    // Собираем заголовки
    QMap<QString, QString> headers;
    int contentLength = 0;

    for (int i = 1; i < lines.size(); ++i) {
        QByteArray line = lines[i].trimmed();
        int colonIndex = line.indexOf(':');
        if (colonIndex > 0) {
            QString key = QString::fromUtf8(line.left(colonIndex)).toLower();
            QString value = QString::fromUtf8(line.mid(colonIndex + 1)).trimmed();
            headers[key] = value;

            if (key == "content-length")
                contentLength = value.toInt();
        }
    }

    // Выделяем body
    QByteArray body = buffer.mid(headerEndIndex + 4);
    handleClientRequest(socket, buffer, headers, body, path);

    m_clientBuffers.remove(socket);
}

void SyncServer::handleClientDisconnected()
{
    QTcpSocket *socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket)
        return;

    qDebug() << "Client disconnected:" << socket->peerAddress().toString();

    m_clientBuffers.remove(socket);
    socket->deleteLater();
}

void SyncServer::handleClientRequest(QTcpSocket *socket, const QByteArray &data,
                                     const QMap<QString, QString>& headers,
                                     const QByteArray &body,
                                     const QByteArray& path)
{
    qDebug() << "Request:" << data;

    if (data.startsWith("GET /register")) {
        handleRegisterRequest(socket->peerAddress());
        sendHttpResponse(socket, 200, "OK", QString("Registered"));
        socket->disconnectFromHost();
        return;
    }

    if (data.startsWith("GET /ping")) {
        QString clientIp = socket->peerAddress().toString();
        m_registeredClients[clientIp] = QDateTime::currentDateTime();
        qDebug() << "Ping from" << clientIp;
        sendHttpResponse(socket, 200, "OK", QString("Pong"));
        socket->disconnectFromHost();
        return;
    }

    if (data.startsWith("POST /sync-list")) {
        handleSyncList(socket, body);
        return;
    }

    if (data.startsWith("POST /upload")) {
        handleUpload(socket, headers, body);
        return;
    }

    if (data.startsWith("GET /download")) {
        QUrl url = QUrl::fromEncoded(path);
        QString relativePath = QUrlQuery(url).queryItemValue("path");
        handleDownload(socket, relativePath);
        return;
    }

    if (data.startsWith("POST /delete")) {
        handleDelete(socket, headers);
        return;
    }

    sendHttpResponse(socket, 404, "Not Found", QString("Unknown command"));
    socket->disconnectFromHost();
}

void SyncServer::handleRegisterRequest(const QHostAddress &addr)
{
    QString ip = addr.toString();
    m_registeredClients[ip] = QDateTime::currentDateTime();
    qDebug() << "Registered client:" << ip;
}

void SyncServer::handleSyncList(QTcpSocket *socket, const QByteArray &body)
{
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "Invalid sync-list JSON:" << parseError.errorString();
        sendHttpResponse(socket, 400, "Bad Request", QString("Invalid JSON"));
        socket->disconnectFromHost();
        return;
    }

    QJsonArray arr = doc.array();
    bool allAccepted = true;

    for (const QJsonValue &val : arr) {
        if (!val.isObject())
            continue;

        QJsonObject obj = val.toObject();
        QString path = obj["path"].toString();
        quint64 version = obj["version"].toString().toULongLong();

        const bool exists = m_fileEntries.contains(path);
        const quint64 currentVer = exists ? m_fileEntries[path].version : 0;

        if (!exists || version > currentVer) {
            qDebug() << "[SyncServer] Accept newer file:" << path << "ver:" << version;
            // Примем: возможно, позже клиент загрузит тело
            // Не обновляем m_fileEntries пока не получим файл
        } else {
            qDebug() << "[SyncServer] Reject outdated file:" << path;
            allAccepted = false;
        }
    }

    if (allAccepted) {
        sendHttpResponse(socket, 200, "OK", QString("All files accepted"));
    } else {
        sendHttpResponse(socket, 409, "Conflict", QString("Some files are outdated"));
    }

    socket->disconnectFromHost();
}

void SyncServer::handleDownloadRequest(QTcpSocket *socket, const QString &fileName)
{
    if (fileName.isEmpty()) {
        sendHttpResponse(socket, 400, "Bad Request", QString("No file specified"));
        socket->disconnectFromHost();
        return;
    }

    QFile file(fileName);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            sendHttpResponse(socket, 500, "Internal Server Error", QString("Cannot open file"));
            socket->disconnectFromHost();
            return;
        }

        QByteArray content = file.readAll();
        file.close();

        QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n";
        response.append(content);
        socket->write(response);
        socket->disconnectFromHost();
    } else {
        // Попробовать получить с внешнего источника
        fetchFromRemote("/" + fileName, [=](QByteArray data) {
            if (!data.isEmpty()) {
                QFile f(fileName);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(data);
                    f.close();
                }
                QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n\r\n" + data;
                socket->write(response);
            } else {
                sendHttpResponse(socket, 404, "Not Found", QString("File not found and not fetched"));
            }
            socket->disconnectFromHost();
        });
    }
}

void SyncServer::handleDownload(QTcpSocket *socket, const QString &relativePath)
{
    QString fullPath = m_syncDirectory + "/" + relativePath;
    QFile file(fullPath);

    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        sendHttpResponse(socket, 404, "Not Found", QString("File not found"));
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    sendHttpResponse(socket, 200, "OK", data, "application/octet-stream");
}


void SyncServer::handleUpload(QTcpSocket *socket,
                              const QMap<QString, QString> &headers,
                              const QByteArray &body)
{
    QString relativePath = headers.value("x-file-path");
    int version = headers.value("x-file-version").toInt();
    QString type = headers.value("x-file-type").toLower();
    if (type.isEmpty()) {
        type = "modified"; // По умолчанию
    }

    if (relativePath.isEmpty() || version <= 0 || body.isEmpty()) {
        sendHttpResponse(socket, 400, "Bad Request", QString("Missing headers or body"));
        return;
    }

    // Сравнение версий
    FileEntry current = m_fileEntries.value(relativePath, FileEntry{relativePath, "unknown", 0});
    if (version <= current.version) {
        qDebug() << "Upload rejected: incoming version" << version << "≤ current version" << current.version;
        sendHttpResponse(socket, 409, "Conflict", QString("Older or same version received"));
        return;
    }

    // Версия новее — сохраняем
    QString fullPath = m_syncDirectory + "/" + relativePath;
    QDir().mkpath(QFileInfo(fullPath).absolutePath());

    QFile file(fullPath);
    if (!file.open(QIODevice::WriteOnly)) {
        sendHttpResponse(socket, 500, "Internal Server Error", QString("Cannot write file"));
        return;
    }

    file.write(body);
    file.close();

    // Обновить локальный список
    m_fileEntries[relativePath] = FileEntry{ relativePath, type, version };

    qDebug() << "Accepted new version for" << relativePath << "version:" << version;

    sendHttpResponse(socket, 200, "OK", QString("File uploaded"));

    // Уведомить других клиентов
    notifyUpdate(relativePath);
}

void SyncServer::sendHttpResponse(QTcpSocket *socket, int code, const QString &status,
                                  const QString &body, const QString &contentType)
{
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(code) + " " + status.toUtf8() + "\r\n";
    response += "Content-Type: " + contentType.toUtf8() + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.toUtf8().size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body.toUtf8();

    socket->write(response);
}

void SyncServer::sendHttpResponse(QTcpSocket *socket, int code,
                                  const QString &status,
                                  const QByteArray &body,
                                  const QString &contentType)
{
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(code) + " " + status.toUtf8() + "\r\n";
    response += "Content-Type: " + contentType.toUtf8() + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;

    socket->write(response);
}

void SyncServer::fetchFromRemote(const QString &path, std::function<void(QByteArray)> callback)
{
    QTcpSocket *remoteSocket = new QTcpSocket(this);
    QByteArray *responseData = new QByteArray;

    QTimer *timeout = new QTimer(remoteSocket);
    timeout->setSingleShot(true);
    timeout->setInterval(5000); // 5 sec timeout

    connect(remoteSocket, &QTcpSocket::connected, [=]() {
        QByteArray request = "GET " + path.toUtf8() + " HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
        qDebug() << "Sending request to example.com:" << request;
        remoteSocket->write(request);
        timeout->start();
    });

    connect(remoteSocket, &QTcpSocket::readyRead, [=]() {
        responseData->append(remoteSocket->readAll());
    });

    auto finish = [=]() {
        timeout->stop();
        int bodyIndex = responseData->indexOf("\r\n\r\n");
        QByteArray body = (bodyIndex != -1) ? responseData->mid(bodyIndex + 4) : *responseData;

        callback(body);

        remoteSocket->deleteLater();
        delete responseData;
    };

    connect(remoteSocket, &QTcpSocket::disconnected, finish);
    connect(timeout, &QTimer::timeout, finish);

    remoteSocket->connectToHost("example.com", 80);
}

void SyncServer::cleanupInactiveClients()
{
    const QDateTime now = QDateTime::currentDateTime();
    QStringList toRemove;

    for (auto it = m_registeredClients.begin(); it != m_registeredClients.end(); ++it) {
        if (it.value().secsTo(now) > 180) { // 3 минуты
            toRemove << it.key();
        }
    }

    for (const QString &ip : toRemove) {
        qDebug() << "Removing inactive client:" << ip;
        m_registeredClients.remove(ip);
    }
}

void SyncServer::notifyUpdate(const QString &relativePath, bool deleted)
{
    QJsonObject obj;
    obj["path"] = relativePath;
    obj["deleted"] = deleted;

    QJsonDocument doc(obj);
    QByteArray json = doc.toJson();

    for (auto it = m_registeredClients.begin(); it != m_registeredClients.end(); ++it) {
        const QString &clientIp = it.key();
        QHostAddress clientAddr(clientIp);

        QTcpSocket *socket = new QTcpSocket(this);

        connect(socket, &QTcpSocket::connected, [=]() {
            QByteArray request;
            request += "POST /notify HTTP/1.1\r\n";
            request += "Host: " + clientIp.toUtf8() + "\r\n";
            request += "Content-Type: application/json\r\n";
            request += "Content-Length: " + QByteArray::number(json.size()) + "\r\n";
            request += "Connection: close\r\n\r\n";
            request += json;

            socket->write(request);
        });

        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        socket->connectToHost(clientAddr, 9090);  // порт клиента
    }
}

void SyncServer::handleDelete(QTcpSocket *socket, const QMap<QString, QString> &headers)
{
    QString relativePath = headers.value("x-file-path");
    if (relativePath.isEmpty()) {
        sendHttpResponse(socket, 400, "Bad Request", QString("Missing x-file-path header"));
        return;
    }

    QString fullPath = m_syncDirectory + "/" + relativePath;
    QFile file(fullPath);

    if (file.exists() && !file.remove()) {
        sendHttpResponse(socket, 500, "Internal Server Error", QString("Failed to delete file"));
        return;
    }

    m_fileEntries.remove(relativePath);
    qDebug() << "Deleted file:" << relativePath;

    sendHttpResponse(socket, 200, "OK", QString("File deleted"));

    notifyUpdate(relativePath, /*deleted=*/true);
}

