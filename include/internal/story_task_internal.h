#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>

#include "packet_types.h"

// ============================================================================
// 主线剧情自动任务 内部共享
// ============================================================================
//
// 定义在 src/hook/story_task.cpp。hook 核心（wpe_hook.cpp）在 ENTER_SCENE_BACK
// 回包里需要置位 g_storyTaskMapEntered，因此这些标记必须在此处声明。

// 主线流程运行标记与会话号：会话号用来挡掉上一轮的迟到回包。
extern std::atomic<bool> g_storyTaskRunning;
extern std::atomic<unsigned long long> g_storyTaskSession;
// 进图完成标记：置位时的会话号一并记下来，同样用于挡迟到回包。
extern std::atomic<bool> g_storyTaskMapEntered;
extern std::atomic<unsigned long long> g_storyTaskMapEnterSession;
// 当前子任务与当前对话节点。
extern std::atomic<uint32_t> g_storyTaskCurrentSubtask;
extern std::atomic<uint32_t> g_storyTaskCurrentDialog;
// 主线流程当前所在场景：连续子任务同场景时不能重复发 ENTER_SCENE。
extern std::atomic<uint32_t> g_storyTaskCurrentScene;

// 剧情战斗（任务节点里的 <battle id="...">）驱动状态。
// 服务端不会替玩家打完，客户端必须自己走 READY→USER_OP→PLAY_OVER→END_SEND。
extern std::atomic<bool> g_storyBattleActive;
extern std::atomic<uint32_t> g_storyBattleDialogId;
extern std::atomic<uint32_t> g_storyBattleNpcId;
// 点战斗节点的时间：服务端迟迟不开战就别再挂着这套状态。
extern std::atomic<DWORD> g_storyBattleArmTick;
// 这一场战斗的开始时间：BATTLE_END 迟迟不来时用它兜底解除接管状态。
extern std::atomic<DWORD> g_storyBattleActiveTick;

// 剧情战斗回包处理：hook 核心在战斗 opcode 分发时调用。
void HandleStoryBattlePacket(const GamePacket& packet);
