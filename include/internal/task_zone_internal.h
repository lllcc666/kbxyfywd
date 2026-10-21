#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "packet_types.h"

// ============================================================================
// 八卦灵盘 / 任务区 内部共享
// ============================================================================
//
// 八卦灵盘与主线任务共用 TASK_TALK_BACK 等 opcode，两边都要拿到回包，因此
// 这里把"两边都会用到的"状态、节流常量和发包原语集中声明：
//   定义在 src/hook/eight_trigrams.cpp，主线任务模块（story_task.cpp）与
//   hook 核心（wpe_hook.cpp 的响应分发）都通过本头访问。

// 对话/点击的节流间隔：服务端按顺序推进，发太快会丢掉中间节点。
constexpr DWORD EIGHT_TRIGRAMS_TALK_DELAY_MS = 400;
constexpr DWORD EIGHT_TRIGRAMS_CLICK_DELAY_MS = 400;
constexpr DWORD EIGHT_TRIGRAMS_RESULT_DELAY_MS = 400;
constexpr DWORD EIGHT_TRIGRAMS_BATTLE_START_WAIT_MS = 10000;
// 水魂交丹对话：点击 61242 后服务端多数情况下不开战，而是下发交丹对白（「不要再攻击我啦」），
// 必须用该对话 id 回一句 TASK_TALK(61242,[dialogId]) 才会给妖怪内丹并推进到 7002。
// 400600509 来自运行时抓包；若本次回包给出别的 dialogId，优先沿用回包值。
constexpr uint32_t EIGHT_TRIGRAMS_WATER_SOUL_DIALOG_ID = 400600509;
// 皇城危机任务的 task_id：八卦灵盘与主线任务都用它查进度。
constexpr uint32_t HUANGCHENG_WEIJI_TASK_ID = 2001000;
constexpr DWORD EIGHT_TRIGRAMS_WATER_SOUL_DIALOG_WAIT_MS = 3000;

// 任务线程的入参：sessionId 用来挡掉上一轮的迟到回包。
struct EightTrigramsTaskData {
    unsigned long long sessionId;
};

// 任务区运行标记：进图等待、流程停止都靠它判断。
extern std::atomic<bool> g_taskZoneRunning;
extern std::atomic<unsigned long long> g_taskZoneSession;
extern std::atomic<bool> g_taskZoneMapEntered;
extern std::atomic<uint32_t> g_taskZoneProgressTaskId;
extern std::atomic<int> g_eightTrigramsResumeStepIndex;

// 八卦灵盘进度缓存：用户任务列表、对话进度、水魂奖励等待。
// 由 eight_trigrams.cpp 独占，跨模块只通过下面的函数访问。
struct EightTrigramsProgressState {
    std::mutex mutex;
    bool userTaskListLoaded = false;
    std::vector<uint32_t> acceptedSubtaskIds;
    std::vector<uint32_t> finishedSubtaskIds;
    bool taskTalkResponseReceived = false;
    uint32_t taskTalkResponseType = 0;
    uint32_t taskTalkResponseDialogId = 0;
    bool captureNextTaskTalkResponse = false;
    bool taskTalkResponseHasDialogId = false;
    uint32_t taskTalkResponseNpcId = 0;
    bool taskTalkResponseMatchNpcId = true;
    bool capturePostBattleTaskTalkResponse = false;
    bool postBattleTaskTalkResponseReceived = false;
    uint32_t postBattleTaskTalkDialogId = 0;
    uint32_t postBattleTaskTalkNpcId = 0;
    uint32_t talkCurrentId = 0;
    uint32_t talkDialogId = 0;
    uint32_t talkTrigram = 0;
    uint32_t talkExp = 0;
    uint32_t talkNpcId = 0;
    uint32_t talkItemId = 0;
    bool awaitingWaterSoulReward = false;
    bool waterSoulRewardReceived = false;
    uint32_t waterSoulRewardItemId = 0;
    uint32_t waterSoulRewardItemCount = 0;
};

extern EightTrigramsProgressState g_eightTrigramsProgress;

// [1,2,3] 形式的下标数组，供前端进度展示使用。
std::wstring BuildUInt32JsonArray(const std::vector<uint32_t>& values);

// 任务区面板文案 + 状态同步。
void UpdateTaskZoneUi(const std::wstring& text, bool running);

// 对话/点击 NPC：与八卦灵盘和主线任务共用的两条发包路径。
BOOL SendTaskZoneTalkPacket(uint32_t npcId, uint32_t dialogId, uint32_t chooseId = 0,
                            bool waitForBack = false, DWORD timeoutMs = 0,
                            bool matchNpcId = true);
BOOL SendTaskZoneClickPacket(uint32_t npcId, bool battleMode = false);

// 会话复位：hook 初始化与停止拦截时清掉上一轮的对话/奖励等待。
void ResetEightTrigramsSessionState();

// 响应分发入口（wpe_hook.cpp 的 InitializeDefaultHandlers 注册这两个）。
void ProcessTaskZoneUserTaskListResponse(const GamePacket& packet);
void ProcessEightTrigramsTaskTalkResponse(const GamePacket& packet);
