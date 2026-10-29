#pragma once
#include "packet_types.h"

/// 封包接收端接口：接收线程只依赖这个抽象，不依赖具体分发器。
class IPacketSink {
public:
    virtual ~IPacketSink() = default;
    virtual void OnPacket(const GamePacket& packet) = 0;
};
