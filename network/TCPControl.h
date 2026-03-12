#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QHostAddress>

class TCPControl : public QObject
{
    Q_OBJECT
public:
    explicit TCPControl(const QHostAddress &host,
                              quint16 port,
                              QObject *parent = nullptr);

    void connectToBoard();
    void disconnectFromBoard();

    // Send parameter update: [Signature:4][ParamID:4][ParamValue:4]
    // Returns false if not connected or write fails
    bool sendParameter(quint32 paramId, quint32 paramValue);

    QHostAddress remoteAddress() const;
    quint16      remotePort() const;

signals:
    void connected();
    void disconnected();
    void errorOccurred(const QString &msg);
    void ackReceived(quint32 paramId, quint32 paramValue);

private slots:
    void onConnected();
    void onDisconnected();
    void onErrorOccurred(QAbstractSocket::SocketError socketError);
    void onReadyRead();

private:
    QTcpSocket   m_socket;
    QHostAddress m_host;
    quint16      m_port;

    static constexpr quint32 SIG_CONTROL_REQUEST = 0x5555CCCC;
    static constexpr quint32 SIG_CONTROL_ACK     = 0xAAAA3333;
    static constexpr int CONTROL_PACKET_SIZE     = 12;

    QByteArray makePacket(quint32 paramId, quint32 paramValue);
    void processAck(const QByteArray &data);
};

