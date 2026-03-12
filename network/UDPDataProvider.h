#pragma once

#include "IDataProvider.h"
#include <QUdpSocket>
#include <QHostAddress>

class UDPDataProvider : public IDataProvider
{
    Q_OBJECT
public:
    explicit UDPDataProvider(quint16 port,
                             QObject *parent = nullptr);

    void start() override;
    void stop() override;

signals:
    void bound();
    void unbound();

private slots:
    void onReadyRead();
    void onErrorOccurred(QAbstractSocket::SocketError socketError);

private:
    QUdpSocket  m_socket;
    quint16     m_port;
    
    // Statistics
    quint64 m_datagramsReceived = 0;
    quint64 m_bytesReceived = 0;
};
