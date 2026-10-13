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
//   主线任务走的是普通任务窗口，是「客户端本地推进 + 服务端驱动」混合：
//     1. 点击 NPC 后服务端可能先回 NPCTASKLIST(type=1) 的可接任务列表，玩家点「接受」
//        才发 ACCEPT_TASK，所以未接的任务必须先接上；
//     2. 服务端回首个 dialogId，客户端本地把该节点里的多句 desc 逐句点完，
//        末尾才回一次 TASK_TALK(dialogId)；
//     3. alert 提示节点不发包，由客户端本地推进。
//   所以本引擎：
//     1. 按 taskinfo 的目标场景进图、点该子任务的入口 NPC；
//     2. 若服务端给出可接任务列表，补发 ACCEPT_TASK(subtaskId) 并重新点 NPC；
//     3. 服务端推什么 dialogId，就按 story_task_recipe 里该节点的类型回应。
//   发包契约来自 AS3：
//     普通节点      -> TASK_TALK_SEND(npcId, [dialogId])
//     提示节点      -> 不发包（TaskTips.dispos 只在 sendback 有值时才回调发包）
//     战斗节点      -> CLICK_NPC(params=sid)（sid 由 MonsterManger.combine1/2 计算）
//
// 注意：taskprop 里 <choose flag> 是对话按钮编号（flag=1 继续、flag=2 关窗），
// 不是 TRAIN_INFO 的参数；chooseId 只由服务端回包给出，本流程拿不到，故不发 TRAIN_INFO。

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
