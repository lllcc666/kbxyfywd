/**
 * @file battle_packets.h
 * @brief 通用战斗发包入口（所有战斗类型共用）。
 *
 * 这些函数原先寄居在 battle_six.cpp，主线任务 / 双台谷 / 通用战斗结束都要
 * include 万妖盛会的头才能拿到，属于分割后的反向依赖。现在统一归到
 * battle.cpp（通用战斗模块）。
 */
#pragma once

#include <windows.h>

#include <cstdint>

BOOL SendBattlePacket(uint32_t spiritId, uint32_t useId = 0,
                      uint8_t extraParam = 0, uint32_t forcedCounter = 0);

/// AS3 BattleControl.clearBattleView() 末尾的 OP_GATEWAY_BATTLE_END.send。
BOOL SendNormalBattleEndPacket();
BOOL SendBattleReadyPacket();
BOOL SendBattlePlayOverPacket();
