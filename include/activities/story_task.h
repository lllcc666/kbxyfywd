#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "packet_types.h"

// 主线剧情任务（皇城危机、魔晶战记等）自动执行。
//
// 与八卦灵盘的区别（关键）：
//   八卦灵盘在 AS3 里有内置自动模块（AutoBaGuaView），执行顺序由它固定，所以
//   native 用的是「固定配方表 + 固定发包」。
//   主线任务走的是普通任务窗口，它是服务端驱动的：客户端点击 NPC -> 服务端下发
//   dialogId（TASK_TALK_BACK / NPCTASKLIST_BACK）-> 客户端回 TASK_TALK(dialogId)
//   -> 服务端再下发下一个 dialogId。
//   所以本引擎不需要把 XML 顺序当发包队列，而是：
//     1. 按 taskinfo 的目标场景进图、点该子任务的入口 NPC；
//     2. 服务端推什么 dialogId，就按 story_task_recipe 里该节点的类型回应。
//   发包契约来自 AS3 TaskControl.taskDialogComplete()：
//     普通节点      -> TASK_TALK_SEND(npcId, [dialogId])
//     选项节点      -> TRAIN_INFO_SEND(params=7, [dialogId, acceptFlag]) 后同上
//     战斗节点      -> CLICK_NPC(params=sid)（sid 由 MonsterManger.combine1/2 计算）

/// 启动主线任务自动执行；subtaskIds 为空表示按配方表顺序跑全部未完成子任务。
BOOL StartStoryTaskAsync(const std::vector<uint32_t>& subtaskIds);

/// 停止主线任务自动执行。
VOID StopStoryTask();

/// TASK_TALK_BACK(1315860) 回包；由 wpe_hook 现有分发链转发进来。
void StoryTaskOnDialogResponse(const GamePacket& packet);

/// NPCTASKLIST_BACK(1315858) 回包（点击 NPC 后的对话入口）。
void StoryTaskOnNpcTaskListResponse(const GamePacket& packet);

/// TASK_FINISHED_BACK(1315847) 回包（子任务完成通知）。
void StoryTaskOnTaskFinishedResponse(const GamePacket& packet);
