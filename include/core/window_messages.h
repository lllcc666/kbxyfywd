#pragma once

#include <windows.h>

namespace AppMessage {

constexpr UINT kExecuteJs = WM_USER + 101;
constexpr UINT kDecomposeComplete = WM_USER + 102;
constexpr UINT kDecomposeHexPacket = WM_USER + 103;
constexpr UINT kDailyTaskComplete = WM_USER + 104;

/// 批量刷新封包列表。
/// 封包是高频事件（战斗中每秒可达数百个），每包一次 kExecuteJs 会让 UI 线程
/// 排队执行成百上千次跨进程 ExecuteScript。改为在 socket 线程只做入队，
/// 由 UI 线程收到本消息后一次性下发整批。
constexpr UINT kFlushPacketEntries = WM_USER + 105;

}  // namespace AppMessage
