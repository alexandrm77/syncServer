#include "SyncController.h"
#include "SyncServer.h"
#include "SyncService.h"
#include "SyncServiceHelper.h"

#include <QDebug>

SyncController::SyncController(QObject *parent)
    : QObject(parent)
{
}

SyncController::~SyncController()
{
    clearCurrentMode();
}

void SyncController::clearCurrentMode()
{
    if (m_server) {
        m_server->stop();
        m_server->deleteLater();
        m_server = nullptr;
    }

    if (m_client) {
        m_client->deleteLater();
        m_client = nullptr;
    }

    if (m_clientHelper) {
        m_clientHelper->deleteLater();
        m_clientHelper = nullptr;
    }

    m_mode = Mode::None;
}

SyncController::Mode SyncController::currentMode() const
{
    return m_mode;
}

void SyncController::switchToMode(SyncController::Mode mode)
{
    if (mode == m_mode)
        return;

    qDebug() << "Switching mode from" << static_cast<int>(m_mode) << "to" << static_cast<int>(mode);

    clearCurrentMode();
    m_mode = mode;

    if (mode == Mode::Server) {
        m_server = new SyncServer(this);
        if (!m_server->listen(QHostAddress::AnyIPv4, 8080)) {
            qCritical() << "Failed to listen on port 8080";
            m_server->deleteLater();
            m_server = nullptr;
            m_mode = Mode::None;
        } else {
            emit modeChanged(m_mode);
        }
    } else if (mode == Mode::Client) {
        m_clientHelper = new SyncServiceHelper(this);
        connect(m_clientHelper, &SyncServiceHelper::discovered, this, [this](SyncService *svc) {
            m_client = svc;
            emit modeChanged(m_mode);
        });
        m_clientHelper->start();
    } else {
        emit modeChanged(m_mode);
    }
}
