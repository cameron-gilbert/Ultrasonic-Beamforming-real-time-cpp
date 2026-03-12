#pragma once

#include "IDataProvider.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>

class TCPDataProvider : public IDataProvider
{
    Q_OBJECT
public:
    explicit TCPDataProvider(quint16 port,
                             QObject *parent = nullptr);

    void start() override;
    void stop() override;

    QHostAddress peerAddress() const;
    quint16      peerPort() const;

signals:
    void connected();
    void disconnected();

private slots:
    void onNewConnection();
    void onSocketDisconnected();
    void onReadyRead();
    void onErrorOccurred(QAbstractSocket::SocketError socketError);

private:
    QTcpServer  m_server;
    QTcpSocket *m_socket = nullptr;
    QByteArray  m_buffer;   // accumulates bytes until entire frame completed
    quint16     m_port;
};
