#include "SyncService.h"
#include "FileMonitor.h"
#include <QTcpSocket>
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

SyncService::SyncService(const QHostAddress &serverAddress,
                         quint16 serverPort,
                         QObject *parent)
    : QObject(parent), m_serverAddress(serverAddress), m_serverPort(serverPort)
{
    m_pingTimer.setInterval(30 * 1000); // 30 секунд
    connect(&m_pingTimer, &QTimer::timeout, this, &SyncService::sendPing);

    m_syncDirectory = QDir::homePath()+ "/test/client";
    m_monitor = new FileMonitor(m_syncDirectory, this);

    connect(m_monitor, &FileMonitor::fileChanged, this, [=](const FileEntry &entry){
        qDebug() << "Изменён/добавлен:" << entry.path << entry.version;
        if (m_ignoreNextChange.contains(entry.path)) {
            qDebug() << "Ignoring fileChanged for:" << entry.path;
            m_ignoreNextChange.remove(entry.path);
            return;
        }
        // Отправим sync-list с изменённым файлом
        sendSyncListToServer({ entry });
    });

    connect(m_monitor, &FileMonitor::fileRemoved, this, [=](const QString &relativePath){
        if (m_ignoreNextChange.contains(relativePath)) {
            qDebug() << "Ignoring fileRemoved for:" << relativePath;
            m_ignoreNextChange.remove(relativePath);
            return;
        }

        qDebug() << "Удалён:" << relativePath;

        FileEntry deletedEntry;
        deletedEntry.path = relativePath;
        deletedEntry.version = 0;
        deletedEntry.type = "deleted";

        sendSyncListToServer({ deletedEntry });

        // Дополнительно отправим POST /delete
        sendDeleteRequest(relativePath);
    });

    m_monitor->start();
}

void SyncService::start()
{
    qDebug() << "SyncService started";
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
    socket->connectToHost(m_serverAddress, m_serverPort);
}

void SyncService::sendSyncListToServer(const QList<FileEntry> &files)
{
    QJsonArray arr;
    for (const FileEntry &entry : files) {
        QJsonObject obj;
        obj["path"] = entry.path;
        obj["version"] = QString::number(entry.version);
        obj["type"] = entry.type;
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
            // Сервер принял — загружаем файл
            for (const FileEntry &entry : files) {
                if (entry.type != "deleted")  // Только для существующих
                    uploadFile(entry);
            }
        }
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    connect(socket, &QTcpSocket::errorOccurred, [=](QAbstractSocket::SocketError err) {
        qWarning() << "SyncService: socket error:" << err << socket->errorString();
    });

    socket->connectToHost(m_serverAddress, m_serverPort);
}

void SyncService::uploadFile(const FileEntry &entry)
{
    QFile file(m_syncDirectory + "/" + entry.path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open file for upload:" << entry.path;
        return;
    }

    QByteArray fileData = file.readAll();

    QTcpSocket *socket = new QTcpSocket(this);

    connect(socket, &QTcpSocket::connected, [=]() {
        QByteArray request;
        request += "POST /upload HTTP/1.1\r\n";
        request += "Host: syncserver\r\n";
        request += "Content-Length: " + QByteArray::number(fileData.size()) + "\r\n";
        request += "X-File-Path: " + entry.path.toUtf8() + "\r\n";
        request += "X-File-Version: " + QByteArray::number(entry.version) + "\r\n";
        request += "X-File-Type: " + entry.type.toUtf8() + "\r\n";
        request += "Content-Type: application/octet-stream\r\n";
        request += "Connection: close\r\n\r\n";
        request += fileData;

        socket->write(request);
    });

    connect(socket, &QTcpSocket::readyRead, [=]() {
        QByteArray response = socket->readAll();
        qDebug() << "Upload response:" << response;
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

    socket->connectToHost(m_serverAddress, m_serverPort);
}

void SyncService::getFile(const QString &relativePath)
{
    QTcpSocket *socket = new QTcpSocket(this);
    QByteArray *buffer = new QByteArray;

    connect(socket, &QTcpSocket::connected, [=]() {
        QByteArray request;
        request += "GET /download?path=" + QUrl::toPercentEncoding(relativePath) + " HTTP/1.1\r\n";
        request += "Host: syncserver\r\n";
        request += "Connection: close\r\n\r\n";
        socket->write(request);
    });

    connect(socket, &QTcpSocket::readyRead, [=]() {
        buffer->append(socket->readAll());

        int headerEndIndex = buffer->indexOf("\r\n\r\n");
        if (headerEndIndex == -1)
            return;

        QByteArray body = buffer->mid(headerEndIndex + 4);

        // Сохраняем файл
        QString fullPath = m_syncDirectory + "/" + relativePath;
        QDir().mkpath(QFileInfo(fullPath).absolutePath());
        QFile file(fullPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(body);
            file.close();
            qDebug() << "Downloaded file:" << relativePath;
        } else {
            qWarning() << "Failed to save downloaded file:" << fullPath;
        }

        delete buffer;
    });

    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

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

    if (path.isEmpty())
        return;

    if (deleted) {
        qDebug() << "Received deletion notification for" << path;
        QString fullPath = m_syncDirectory + "/" + path;
        QFile::remove(fullPath);

        // Добавляем в список игнорирования
        m_ignoreNextChange.insert(path);
    } else {
        qDebug() << "Received update notification for" << path;
        // Тоже добавим сюда, чтобы избежать лишнего fileChanged
        m_ignoreNextChange.insert(path);
        getFile(path);
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

void SyncService::sendDeleteRequest(const QString &relativePath)
{
    QTcpSocket *socket = new QTcpSocket(this);

    connect(socket, &QTcpSocket::connected, [=]() {
        QByteArray request;
        request += "POST /delete HTTP/1.1\r\n";
        request += "Host: syncserver\r\n";
        request += "X-File-Path: " + relativePath.toUtf8() + "\r\n";
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
