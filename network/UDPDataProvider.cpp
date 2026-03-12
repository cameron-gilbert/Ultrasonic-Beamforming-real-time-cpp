#include "UDPDataProvider.h"
#include "../model/MicrophonePacket.h"

#include <QAbstractSocket>
// #include <QDebug>  // REMOVED FOR PERFORMANCE
#include <QTimer>
#include <QVariant>

UDPDataProvider::UDPDataProvider(quint16 port, QObject *parent)
    : IDataProvider(parent),
    m_port(port)
{
    connect(&m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QUdpSocket::errorOccurred),
            this, &UDPDataProvider::onErrorOccurred);
    
    connect(&m_socket, &QUdpSocket::readyRead,
            this, &UDPDataProvider::onReadyRead);
}

void UDPDataProvider::start()
{
    if (m_socket.state() == QAbstractSocket::BoundState)
        return;

    // Bind to port 5000 on all interfaces to receive UDP datagrams
    if (!m_socket.bind(QHostAddress::Any, m_port)) {
        emit errorOccurred(QStringLiteral("UDP socket bind failed on port %1: %2")
                               .arg(m_port)
                               .arg(m_socket.errorString()));
        return;
    }
    
    // Optimize socket for high throughput.
    // At 102 mics × 93.75 frames/sec the FPGA sends ~10 MB/s.
    // Use a 32 MB OS socket buffer so bursts don't overflow before onReadyRead drains them.
    m_socket.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption, QVariant(32 * 1024 * 1024)); // 32MB buffer
    
    // qDebug() << "[UDP] Bound to port" << m_port;  // REMOVED FOR PERFORMANCE
    
    // Emit bound signal asynchronously to avoid blocking the UI thread
    QTimer::singleShot(0, this, [this]() {
        emit bound();
    });
}

void UDPDataProvider::stop()
{
    if (m_socket.state() == QAbstractSocket::BoundState) {
        m_socket.close();
        // qDebug() << "[UDP] Socket closed";  // REMOVED FOR PERFORMANCE
        emit unbound();
    }
    
    m_datagramsReceived = 0;
    m_bytesReceived = 0;
}

void UDPDataProvider::onReadyRead()
{
    const int packetSize = MicrophonePacket::TotalBytes;
    
    // Drain ALL pending datagrams every time onReadyRead fires.
    // Previously capped at 100 packets/call which caused ~35% packet loss at 10 MB/s.
    // The natural cap is hasPendingDatagrams() returning false.
    int packetsThisCall = 0;

    while (m_socket.hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(m_socket.pendingDatagramSize());
        
        QHostAddress sender;
        quint16 senderPort;
        
        qint64 bytesRead = m_socket.readDatagram(datagram.data(), datagram.size(),
                                                  &sender, &senderPort);
        
        if (bytesRead < 0) {
            qWarning() << "[UDP] Error reading datagram:" << m_socket.errorString();
            continue;
        }
        
        m_datagramsReceived++;
        m_bytesReceived += bytesRead;
        ++packetsThisCall;
        
        // Validate datagram size
        if (bytesRead != packetSize) {
            if (m_datagramsReceived <= 5) {
                qWarning() << "[UDP_RX] Unexpected datagram size:" << bytesRead 
                           << "expected:" << packetSize;
            }
            continue;
        }
        
        // Emit the packet for processing
        emit packetReceived(datagram);
    }
}

void UDPDataProvider::onErrorOccurred(QAbstractSocket::SocketError socketError)
{
    QString errorMsg = QStringLiteral("UDP socket error (%1): %2")
                           .arg(socketError)
                           .arg(m_socket.errorString());
    
    // qWarning() << "[UDP_ERROR]" << errorMsg;  // REMOVED FOR PERFORMANCE
    emit errorOccurred(errorMsg);
}
