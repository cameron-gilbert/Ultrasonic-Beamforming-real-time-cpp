#include "TCPControl.h"
#include <QAbstractSocket>
#include <QtEndian>
// #include <QDebug> // REMOVED FOR PERFORMANCE

TCPControl::TCPControl(const QHostAddress &host,
                                   quint16 port,
                                   QObject *parent)
    : QObject(parent),
    m_socket(this),
    m_host(host),
    m_port(port)
{
    connect(&m_socket, &QTcpSocket::connected,
            this, &TCPControl::onConnected);

    connect(&m_socket, &QTcpSocket::disconnected,
            this, &TCPControl::onDisconnected);

    connect(&m_socket,
            &QTcpSocket::errorOccurred,
            this,
            &TCPControl::onErrorOccurred);

    connect(&m_socket, &QTcpSocket::readyRead,
            this, &TCPControl::onReadyRead);
}

void TCPControl::connectToBoard()
{
    if (m_socket.state() == QAbstractSocket::ConnectedState ||
        m_socket.state() == QAbstractSocket::ConnectingState) {
        return;
    }

    // Increase connection timeout tolerance for embedded systems
    m_socket.connectToHost(m_host, m_port);
    
    // Note: Qt's default timeout is ~30 seconds which should be sufficient
    // for FPGA to complete boot and start listening on port 6000
}

void TCPControl::disconnectFromBoard()
{
    if (m_socket.state() == QAbstractSocket::ConnectedState ||
        m_socket.state() == QAbstractSocket::ConnectingState) {
        m_socket.disconnectFromHost();
        // Wait for graceful disconnect
        if (m_socket.state() != QAbstractSocket::UnconnectedState) {
            m_socket.waitForDisconnected(1000);
        }
    }
}

bool TCPControl::sendParameter(quint32 paramId, quint32 paramValue)
{
    if (m_socket.state() != QAbstractSocket::ConnectedState)
        return false;

    QByteArray packet = makePacket(paramId, paramValue);
    const qint64 written = m_socket.write(packet);
    m_socket.flush();
    return (written == packet.size());
}

// INTERNAL
QByteArray TCPControl::makePacket(quint32 paramId, quint32 paramValue)
{
    // 12 byte control packet:
    // [0..3]   = Signature (0x5555CCCC)
    // [4..7]   = Parameter ID
    // [8..11]  = Parameter Value
    QByteArray buf;
    buf.resize(CONTROL_PACKET_SIZE);

    uchar *p = reinterpret_cast<uchar*>(buf.data());

    qToBigEndian<quint32>(SIG_CONTROL_REQUEST, p + 0);
    qToBigEndian<quint32>(paramId,             p + 4);
    qToBigEndian<quint32>(paramValue,          p + 8);

    return buf;
}

void TCPControl::processAck(const QByteArray &data)
{
    if (data.size() != CONTROL_PACKET_SIZE) {
        return;
    }

    const uchar *p = reinterpret_cast<const uchar*>(data.constData());

    quint32 signature = qFromBigEndian<quint32>(p + 0);
    quint32 paramId   = qFromBigEndian<quint32>(p + 4);
    quint32 paramVal  = qFromBigEndian<quint32>(p + 8);

    if (signature == SIG_CONTROL_ACK) {
        emit ackReceived(paramId, paramVal);
    }
}

void TCPControl::onReadyRead()
{
    while (m_socket.bytesAvailable() >= CONTROL_PACKET_SIZE) {
        QByteArray ack = m_socket.read(CONTROL_PACKET_SIZE);
        processAck(ack);
    }
}

void TCPControl::onConnected()
{
    emit connected();
}

void TCPControl::onDisconnected()
{
    emit disconnected();
}

void TCPControl::onErrorOccurred(QAbstractSocket::SocketError)
{
    emit errorOccurred(QStringLiteral("Control TCP error: %1")
                           .arg(m_socket.errorString()));
}

QHostAddress TCPControl::remoteAddress() const
{
    return m_socket.peerAddress();
}

quint16 TCPControl::remotePort() const
{
    return m_socket.peerPort();
}
