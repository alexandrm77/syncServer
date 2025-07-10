#include "SyncServer.h"
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include <QTimer>
#include <QTcpSocket>

SyncServer::SyncServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &SyncServer::handleNewConnection);
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

    QByteArray request = buffer.left(headerEndIndex + 4);
    // Можно расширить, чтобы обработать body для POST, но пока только заголовки
    handleClientRequest(socket, buffer);

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

void SyncServer::handleClientRequest(QTcpSocket *socket, const QByteArray &data)
{
    qDebug() << "Request:" << data;

    if (data.startsWith("GET /register")) {
        handleRegisterRequest(socket->peerAddress());
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nRegistered\n");
        socket->disconnectFromHost();
        return;
    }

    if (data.startsWith("GET /sync-list")) {
        handleSyncListRequest(socket);
        return;
    }

    if (data.startsWith("GET /download")) {
        QString requestStr = QString::fromUtf8(data);
        QString pathPart = requestStr.split(" ")[1];
        QUrl url(pathPart);
        QUrlQuery query(url);
        QString fileName = query.queryItemValue("file");
        handleDownloadRequest(socket, fileName);
        return;
    }

    if (data.startsWith("POST /upload")) {
        handleUploadRequest(socket, data);
        return;
    }

    socket->write("HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\n\r\nUnknown command\n");
    socket->disconnectFromHost();
}

void SyncServer::handleRegisterRequest(const QHostAddress &addr)
{
    QString ip = addr.toString();
    m_registeredClients[ip] = QDateTime::currentDateTime();
    qDebug() << "Registered client:" << ip;
}

void SyncServer::handleSyncListRequest(QTcpSocket *socket)
{
    QDir dir(".");
    QFileInfoList files = dir.entryInfoList(QDir::Files);
    QByteArray body;

    for (const QFileInfo &info : files) {
        QString line = info.fileName() + " " + info.lastModified().toString(Qt::ISODate) + "\n";
        body.append(line.toUtf8());
    }

    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n" + body;
    socket->write(response);
    socket->disconnectFromHost();
}

void SyncServer::handleDownloadRequest(QTcpSocket *socket, const QString &fileName)
{
    if (fileName.isEmpty()) {
        socket->write("HTTP/1.1 400 Bad Request\r\n\r\nNo file specified\n");
        socket->disconnectFromHost();
        return;
    }

    QFile file(fileName);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) {
            socket->write("HTTP/1.1 500 Internal Server Error\r\n\r\nCannot open file\n");
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
                socket->write("HTTP/1.1 404 Not Found\r\n\r\nFile not found and not fetched\n");
            }
            socket->disconnectFromHost();
        });
    }
}

void SyncServer::handleUploadRequest(QTcpSocket *socket, const QByteArray &rawRequest)
{
    int sepIndex = rawRequest.indexOf("\r\n\r\n");
    if (sepIndex == -1) {
        socket->write("HTTP/1.1 400 Bad Request\r\n\r\nMalformed request\n");
        socket->disconnectFromHost();
        return;
    }

    QByteArray body = rawRequest.mid(sepIndex + 4);
    int filenameEnd = body.indexOf('\n');

    if (filenameEnd == -1) {
        socket->write("HTTP/1.1 400 Bad Request\r\n\r\nNo filename specified\n");
        socket->disconnectFromHost();
        return;
    }

    QString fileName = QString::fromUtf8(body.left(filenameEnd)).trimmed();
    QByteArray fileData = body.mid(filenameEnd + 1);

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly)) {
        socket->write("HTTP/1.1 500 Internal Server Error\r\n\r\nCannot write file\n");
        socket->disconnectFromHost();
        return;
    }
    file.write(fileData);
    file.close();

    m_fileVersions[fileName] = QFileInfo(file).lastModified();

    qDebug() << "Received uploaded file:" << fileName;
    socket->write("HTTP/1.1 200 OK\r\n\r\nUpload complete\n");
    socket->disconnectFromHost();
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
