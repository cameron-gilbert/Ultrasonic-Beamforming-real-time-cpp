#pragma once
#include <QVector>
#include <QtGlobal>

struct MicrophonePacket
{
    // UDP packet structure - 70 byte header + 512 x 16-bit samples
    static constexpr int HeaderBytes   = 70;
    static constexpr int SampleCount   = 512;
    static constexpr int DataBytes     = SampleCount * static_cast<int>(sizeof(qint16));   // 1024
    static constexpr int TotalBytes    = HeaderBytes + DataBytes;                           // 1094
    static constexpr quint32 ExpectedSignature = 0x76543210;  // Packet validation signature

    // header fields (new format from professor)
    quint32 swFrameNumber = 0;  // software frame counter from FPGA
    quint16 micIndex      = 0;  // microphone/channel index (0-101)
    quint32 hwFrameNumber = 0;  // hardware DMA frame counter
    quint32 signature     = 0;  // validation signature (should be 0x76543210)
    QVector<uchar> reserved;    // reserved bytes (56 bytes)

    // payload (stored as float for precision in beamforming)
    QVector<float> samples;  // 512 normalized samples [-1.0, 1.0] from one microphone
};
