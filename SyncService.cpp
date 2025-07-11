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

    m_syncDirectory = "/home/user/sync";  // или аргумент конструктора
    m_monitor = new FileMonitor(m_syncDirectory, this);

    connect(m_monitor, &FileMonitor::fileChanged, this, [=](const FileEntry &entry){
        qDebug() << "Изменён/добавлен:" << entry.path << entry.version;
        // TODO: отправить sync-list на сервер
    });

    connect(m_monitor, &FileMonitor::fileRemoved, this, [=](const QString &relativePath){
        qDebug() << "Удалён:" << relativePath;
        // TODO: sync-list с пометкой "удалено"
    });

    m_monitor->start();
}

void SyncService::start()
{
    qDebug() << "SyncService started";
    m_pingTimer.start();
    sendPing(); // первый ping сразу
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

    connect(socket, &QTcpSocket::connected, [=]() {
        QByteArray request;
        request += "GET /download?path=" + QUrl::toPercentEncoding(relativePath) + " HTTP/1.1\r\n";
        request += "Host: syncserver\r\n";
        request += "Connection: close\r\n\r\n";
        socket->write(request);
    });

    connect(socket, &QTcpSocket::readyRead, [=]() {
        static QByteArray buffer;
        buffer += socket->readAll();

        int headerEndIndex = buffer.indexOf("\r\n\r\n");
        if (headerEndIndex == -1)
            return; // Ждём полный заголовок

        QByteArray body = buffer.mid(headerEndIndex + 4);

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

    QString path = doc.object().value("path").toString();
    if (path.isEmpty())
        return;

    qDebug() << "Received update notification for" << path;
    getFile(path);
}

