#include "PacketDecoder.h"
#include <QtEndian>
// #include <QDebug> // REMOVED FOR PERFORMANCE
// #include <QElapsedTimer> // REMOVED FOR PERFORMANCE

static constexpr quint32 SIG = 0x76543210;

bool PacketDecoder::decode(const QByteArray &raw, MicrophonePacket &out)
{
    if (raw.size() < MicrophonePacket::TotalBytes)
        return false;

    const uchar *ptr = reinterpret_cast<const uchar*>(raw.constData());

    out.swFrameNumber = qFromBigEndian<quint32>(ptr);
    out.micIndex = qFromBigEndian<quint16>(ptr + 4);
    out.hwFrameNumber = qFromBigEndian<quint32>(ptr + 6);
    out.signature = qFromBigEndian<quint32>(ptr + 10);

    // Validate signature
    if (out.signature != MicrophonePacket::ExpectedSignature) {
        return false;
    }

    // Offset 14: Reserved bytes (56 bytes)
    out.reserved.resize(56);
    memcpy(out.reserved.data(), ptr + 14, 56);

    // Sample data: 512 x int16 samples (little-endian) - convert to normalized float
    const uchar *dataPtr = ptr + MicrophonePacket::HeaderBytes;
    out.samples.resize(MicrophonePacket::SampleCount);
    for (int i = 0; i < MicrophonePacket::SampleCount; ++i) {
        qint16 rawSample = qFromLittleEndian<qint16>(dataPtr + i * static_cast<int>(sizeof(qint16)));
        // Normalize to [-1.0, 1.0] range for float processing
        out.samples[i] = rawSample / 32768.0f;
    }
    return true;
}
