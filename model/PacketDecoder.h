#pragma once

#include <QByteArray>
#include "MicrophonePacket.h"

class PacketDecoder
{
public:
    // Decode raw bytes from UDP into a MicrophonePacket
    // Returns false if size is wrong or data is incomplete
    static bool decode(const QByteArray &raw, MicrophonePacket &out);
};
