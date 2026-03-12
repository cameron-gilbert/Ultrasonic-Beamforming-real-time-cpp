#include "SimulatedDataProvider.h"
#include "../model/MicrophonePacket.h"
#include <QtEndian>
#include <cmath>

SimulatedDataProvider::SimulatedDataProvider(QObject *parent)
    : IDataProvider(parent)
{
    // emit one mic packet every tick
    m_timer.setInterval(10); // (ms) -> 100 packets/sec
    connect(&m_timer, &QTimer::timeout, this, &SimulatedDataProvider::generatePacket);
}

void SimulatedDataProvider::start()
{
    m_frameNumber = 0;
    m_micIndex    = 0;
    m_timer.start();
}

void SimulatedDataProvider::stop()
{
    m_timer.stop();
}

void SimulatedDataProvider::generatePacket()
{
    QByteArray raw;
    raw.resize(MicrophonePacket::TotalBytes);

    uchar *ptr = reinterpret_cast<uchar*>(raw.data());

    // header
    qToLittleEndian<quint16>(m_micIndex, ptr);

    // frame number
    qToLittleEndian<quint32>(m_frameNumber, ptr + 2);

    // reserved = zero
    memset(ptr + 6, 0, 64);

    // payload
    uchar *dataPtr = ptr + MicrophonePacket::HeaderBytes;

    // different mic = different phase so distinct traces can be seen
    const double pi   = 3.14159265358979323846;
    const double freq = 5.0;         // cycles per packet
    const double amp  = 12000.0;     // int16 range-safe
    const double phase = (m_micIndex / 102.0) * 2.0 * pi;

    for (int i = 0; i < MicrophonePacket::SampleCount; ++i) {
        double t = static_cast<double>(i) / MicrophonePacket::SampleCount;
        qint16 s = static_cast<qint16>(amp * std::sin(2.0 * pi * freq * t + phase));
        qToLittleEndian<qint16>(s, dataPtr + i * static_cast<int>(sizeof(qint16)));
    }

    emit packetReceived(raw);

    // advance mic index, next frame number every 102 packets
    if (++m_micIndex >= 102) {
        m_micIndex = 0;
        ++m_frameNumber;
    }
}
