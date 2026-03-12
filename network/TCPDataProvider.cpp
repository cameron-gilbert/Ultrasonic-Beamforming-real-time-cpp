#include "TCPDataProvider.h"
#include "../model/MicrophonePacket.h"

#include <QAbstractSocket>
// #include <QDebug> // REMOVED FOR PERFORMANCE

TCPDataProvider::TCPDataProvider(quint16 port, QObject *parent)
    : IDataProvider(parent),
    m_port(port)
{
    connect(&m_server, &QTcpServer::newConnection,
            this, &TCPDataProvider::onNewConnection);
}

void TCPDataProvider::start()
{
    if (m_server.isListening())
        return;

    if (!m_server.listen(QHostAddress::Any, m_port)) {
        emit errorOccurred(QStringLiteral("Data server listen failed: %1")
                               .arg(m_server.errorString()));
    }
}

void TCPDataProvider::stop()
{
    if (m_socket) {
        disconnect(m_socket, nullptr, this, nullptr); // Disconnect all signals first
        m_socket->disconnectFromHost();
        if (m_socket->state() != QAbstractSocket::UnconnectedState) {
            m_socket->waitForDisconnected(1000); // Wait up to 1 second
        }
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_server.close();
    m_buffer.clear();
}

void TCPDataProvider::onNewConnection()
{
    // only one FPGA connection
    if (m_socket) {
        m_socket->disconnectFromHost();
        m_socket->deleteLater();
        m_socket = nullptr;
    }

    m_socket = m_server.nextPendingConnection();
    
    // Optimize for high throughput - large receive buffer
    m_socket->setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, 512 * 1024);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);

    connect(m_socket, &QTcpSocket::readyRead,
            this, &TCPDataProvider::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &TCPDataProvider::onSocketDisconnected);
    connect(m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &TCPDataProvider::onErrorOccurred);

    emit connected();
}

void TCPDataProvider::onReadyRead()
{
    if (!m_socket)
        return;

    qint64 bytesAvailable = m_socket->bytesAvailable();
    QByteArray newData = m_socket->readAll();
    
    // REMOVED FOR PERFORMANCE:
    // static int readCount = 0;
    // readCount++;
    // if (readCount <= 10) {
    //     qDebug() << "[TCP_RX]" << readCount << ": Read" << newData.size() << "bytes, total buffered:" << (m_buffer.size() + newData.size());
    // }
    
    m_buffer.append(newData);
    const int packetSize = MicrophonePacket::TotalBytes;

    int packetsExtracted = 0;
    while (m_buffer.size() >= packetSize) {
        QByteArray packet = m_buffer.left(packetSize);
        m_buffer.remove(0, packetSize);
        emit packetReceived(packet);
        packetsExtracted++;
    }
    
    // REMOVED FOR PERFORMANCE:
    // if (readCount <= 10 && packetsExtracted > 0) {
    //     qDebug() << "[TCP_RX] Extracted" << packetsExtracted << "packets, remaining buffer:" << m_buffer.size();
    // }
}

void TCPDataProvider::onSocketDisconnected()
{
    if (m_socket) {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    emit disconnected();
}

void TCPDataProvider::onErrorOccurred(QAbstractSocket::SocketError)
{
    emit errorOccurred(QStringLiteral("Data server error: %1")
                           .arg(m_socket ? m_socket->errorString()
                                         : m_server.errorString()));
}

QHostAddress TCPDataProvider::peerAddress() const
{
    return m_socket ? m_socket->peerAddress() : QHostAddress();
}

quint16 TCPDataProvider::peerPort() const
{
    return m_socket ? m_socket->peerPort() : 0;
}
