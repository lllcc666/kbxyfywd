#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "activity_states_internal.h"
#include "battle_six.h"
#include "hook_shared.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_parser.h"
#include "packet_protocol.h"
#include "shuangtai_internal.h"
#include "story_task_recipe.h"
#include "task_zone_internal.h"
#include "ui_bridge.h"
#include "utils.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

extern bool PostScriptToUI(const std::wstring& jsCode);

// 八卦灵盘任务区 owner：运行标记、会话号和地图进入状态统一由这里维护。
std::atomic<bool> g_taskZoneRunning{false};
std::atomic<unsigned long long> g_taskZoneSession{0};
std::atomic<bool> g_taskZoneMapEntered{false};

// 八卦灵盘进度缓存：结构定义见 task_zone_internal.h，跨模块只通过函数访问。
EightTrigramsProgressState g_eightTrigramsProgress;
std::atomic<uint32_t> g_taskZoneProgressTaskId{0};
std::atomic<int> g_eightTrigramsResumeStepIndex{-1};

// ============================================================================
// 任务区（八卦灵盘）
// ============================================================================

// 皇城危机任务组号，UI 进度面板默认按该组号查询（组号 = taskinfo 里的 task id）。

// 是否为主线剧情任务组（皇城危机 / 魔晶战记 等）。UI 面板按系列切换时要按各自组号查询进度。
static bool IsMainlineStoryTaskId(uint32_t taskId) {
    if (taskId == 0) {
        return false;
    }
    for (uint32_t i = 0; i < StoryTaskRecipe::SUBTASK_COUNT; ++i) {
        if (StoryTaskRecipe::SUBTASKS[i].taskId == taskId) {
            return true;
        }
    }
    return false;
}

enum class TaskZoneStepKind {
    Talk,
    Acquire,
    AlertAcquire,
    SceneAcquire,
    RewardOnly,
    Battle,
    Flash,
    BranchOnly
};

struct EightTrigramsStep {
    uint32_t sceneId;
    uint32_t npcId;
    uint32_t dialogId;
    uint32_t chooseId;
    TaskZoneStepKind kind;
    const wchar_t* label;
};

static const EightTrigramsStep EIGHT_TRIGRAMS_STEPS[] = {
    // 固定流程配方：普通对话/奖励按预设包序列和延时推进；只有战斗等待战斗状态结束。
    // 不依赖 TASK_TALK_BACK 中的动态 dialogId，也不把 XML alert 展开成额外自动包。
    {2001, 21210, 400600101, 3, TaskZoneStepKind::Talk, L"墨乾"},
    {2005, 21511, 400600103, 0, TaskZoneStepKind::AlertAcquire, L"五彩灵芝"},
    {3001, 31101, 400600102, 0, TaskZoneStepKind::Talk, L"墨坤"},
    {2001, 21210, 400600104, 0, TaskZoneStepKind::BranchOnly, L"墨乾(拒绝)"},

    {3001, 31101, 400600201, 3, TaskZoneStepKind::Talk, L"墨坤"},
    {3002, 31231, 400600203, 0, TaskZoneStepKind::AlertAcquire, L"夺灵丹"},
    {3002, 31231, 400600204, 0, TaskZoneStepKind::Talk, L"火炎"},
    {4001, 41101, 400600202, 0, TaskZoneStepKind::Talk, L"墨坎"},

    {4001, 41101, 400600301, 3, TaskZoneStepKind::Talk, L"墨坎"},
    {4003, 41311, 400600303, 0, TaskZoneStepKind::SceneAcquire, L"菩提子"},
    {5002, 51206, 400600302, 0, TaskZoneStepKind::Talk, L"墨离"},

    {5002, 51206, 400600401, 3, TaskZoneStepKind::Talk, L"墨离"},
    {5003, 51305, 400600403, 0, TaskZoneStepKind::AlertAcquire, L"黑河水"},
    {6001, 61101, 400600402, 0, TaskZoneStepKind::Talk, L"墨震"},

    {6001, 61101, 400600501, 3, TaskZoneStepKind::Talk, L"墨震"},
    {6002, 61242, 400600506, 0, TaskZoneStepKind::Battle, L"水魂"},

    // Native 普通任务兼容链：水魂确认后，先用 400600505 向墨艮交接内丹，再选择 400600601。
    {7002, 71201, 400600505, 0, TaskZoneStepKind::Talk, L"墨艮"},
    {7002, 71201, 400600601, 3, TaskZoneStepKind::Talk, L"墨艮"},
    {8003, 81303, 400600603, 0, TaskZoneStepKind::AlertAcquire, L"火铜"},
    {9001, 91101, 400600602, 0, TaskZoneStepKind::Talk, L"墨巽"},

    {9001, 91101, 400600701, 3, TaskZoneStepKind::Talk, L"墨巽"},
    {9004, 91435, 400600703, 0, TaskZoneStepKind::AlertAcquire, L"柿果"},
    {9004, 91435, 400600704, 0, TaskZoneStepKind::Flash, L"命运大转盘"},
    {10001, 101101, 400600702, 0, TaskZoneStepKind::Talk, L"墨兑"},

    {10001, 101101, 400600801, 3, TaskZoneStepKind::Talk, L"墨兑"},
    {10002, 101201, 400600803, 0, TaskZoneStepKind::Talk, L"墨魂"},
    {10002, 101201, 400600804, 0, TaskZoneStepKind::Battle, L"墨魂"},
    {2001, 21210, 400600802, 0, TaskZoneStepKind::Talk, L"墨乾"},
};

static constexpr size_t EIGHT_TRIGRAMS_STEP_COUNT = sizeof(EIGHT_TRIGRAMS_STEPS) / sizeof(EIGHT_TRIGRAMS_STEPS[0]);

static uint32_t GetEightTrigramsGroupId(const EightTrigramsStep& step) {
    return step.dialogId / 100;
}

static std::vector<uint32_t> BuildEightTrigramsGroupOrder() {
    std::vector<uint32_t> groupOrder;
    uint32_t lastGroupId = 0;
    for (const auto& step : EIGHT_TRIGRAMS_STEPS) {
        const uint32_t groupId = GetEightTrigramsGroupId(step);
        if (groupId == 0 || groupId == lastGroupId) {
            continue;
        }
        groupOrder.push_back(groupId);
        lastGroupId = groupId;
    }
    return groupOrder;
}

static size_t FindEightTrigramsStepIndexForGroup(uint32_t groupId) {
    for (size_t i = 0; i < EIGHT_TRIGRAMS_STEP_COUNT; ++i) {
        if (GetEightTrigramsGroupId(EIGHT_TRIGRAMS_STEPS[i]) == groupId) {
            return i;
        }
    }
    return EIGHT_TRIGRAMS_STEP_COUNT;
}

static size_t FindEightTrigramsStepIndexAfterGroup(uint32_t groupId) {
    bool groupStarted = false;
    for (size_t i = 0; i < EIGHT_TRIGRAMS_STEP_COUNT; ++i) {
        const uint32_t stepGroupId = GetEightTrigramsGroupId(EIGHT_TRIGRAMS_STEPS[i]);
        if (!groupStarted) {
            if (stepGroupId == groupId) {
                groupStarted = true;
            }
            continue;
        }
        if (stepGroupId != groupId && stepGroupId != 0) {
            return i;
        }
    }
    return EIGHT_TRIGRAMS_STEP_COUNT;
}
static size_t FindEightTrigramsStepIndexByDialogId(uint32_t dialogId) {
    for (size_t i = 0; i < EIGHT_TRIGRAMS_STEP_COUNT; ++i) {
        if (EIGHT_TRIGRAMS_STEPS[i].dialogId == dialogId) {
            return i;
        }
    }
    return EIGHT_TRIGRAMS_STEP_COUNT;
}


std::wstring BuildUInt32JsonArray(const std::vector<uint32_t>& values) {
    std::wstring result = L"[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            result += L",";
        }
        result += std::to_wstring(values[i]);
    }
    result += L"]";
    return result;
}

static void PushHuangchengWeijiProgressToUi(uint32_t taskId = HUANGCHENG_WEIJI_TASK_ID) {
    std::vector<uint32_t> acceptedSubtaskIds;
    std::vector<uint32_t> finishedSubtaskIds;
    {
        std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
        acceptedSubtaskIds = g_eightTrigramsProgress.acceptedSubtaskIds;
        finishedSubtaskIds = g_eightTrigramsProgress.finishedSubtaskIds;
    }

    std::wstring script = L"if(window.updateHuangchengWeijiProgress) { window.updateHuangchengWeijiProgress({taskId:" +
                          std::to_wstring(taskId) +
                          L", acceptedSubtaskIds:" + BuildUInt32JsonArray(acceptedSubtaskIds) +
                          L", finishedSubtaskIds:" + BuildUInt32JsonArray(finishedSubtaskIds) +
                          L"}); }";
    PostScriptToUI(script);
}

static void UpdateEightTrigramsResumeHint(size_t nextIndex) {
    g_eightTrigramsResumeStepIndex.store(static_cast<int>(nextIndex));
}
static void UpdateEightTrigramsResumeHintByDialogId(uint32_t dialogId);



static void ResetTaskZoneUserTaskListCache() {
    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    g_eightTrigramsProgress.userTaskListLoaded = false;
    g_eightTrigramsProgress.acceptedSubtaskIds.clear();
    g_eightTrigramsProgress.finishedSubtaskIds.clear();
}

static void ResetEightTrigramsProgressState() {
    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    g_eightTrigramsProgress.userTaskListLoaded = false;
    g_eightTrigramsProgress.acceptedSubtaskIds.clear();
    g_eightTrigramsProgress.finishedSubtaskIds.clear();
    g_eightTrigramsProgress.taskTalkResponseReceived = false;
    g_eightTrigramsProgress.taskTalkResponseType = 0;
    g_eightTrigramsProgress.captureNextTaskTalkResponse = false;
    g_eightTrigramsProgress.taskTalkResponseHasDialogId = false;
    g_eightTrigramsProgress.taskTalkResponseDialogId = 0;
    g_eightTrigramsProgress.taskTalkResponseNpcId = 0;
    g_eightTrigramsProgress.taskTalkResponseMatchNpcId = true;
    g_eightTrigramsProgress.capturePostBattleTaskTalkResponse = false;
    g_eightTrigramsProgress.postBattleTaskTalkResponseReceived = false;
    g_eightTrigramsProgress.postBattleTaskTalkDialogId = 0;
    g_eightTrigramsProgress.postBattleTaskTalkNpcId = 0;
    g_eightTrigramsProgress.talkCurrentId = 0;
    g_eightTrigramsProgress.talkDialogId = 0;
    g_eightTrigramsProgress.talkTrigram = 0;
    g_eightTrigramsProgress.talkExp = 0;
    g_eightTrigramsProgress.talkNpcId = 0;
    g_eightTrigramsProgress.talkItemId = 0;
    g_eightTrigramsProgress.awaitingWaterSoulReward = false;
    g_eightTrigramsProgress.waterSoulRewardReceived = false;
    g_eightTrigramsProgress.waterSoulRewardItemId = 0;
    g_eightTrigramsProgress.waterSoulRewardItemCount = 0;
}

void ResetEightTrigramsSessionState() {
    ResetEightTrigramsProgressState();
    g_eightTrigramsResumeStepIndex.store(-1);
}

static bool TryResolveEightTrigramsResumeIndex(size_t& startIndex) {
    const size_t defaultIndex = 0;
    const auto groupOrder = BuildEightTrigramsGroupOrder();

    const int resumeStepIndex = g_eightTrigramsResumeStepIndex.load();
    if (resumeStepIndex >= 0) {
        const size_t resumeIndex = static_cast<size_t>(resumeStepIndex);
        startIndex = resumeIndex > EIGHT_TRIGRAMS_STEP_COUNT ? EIGHT_TRIGRAMS_STEP_COUNT : resumeIndex;
        return true;
    }

    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    if (g_eightTrigramsProgress.userTaskListLoaded) {
        if (!g_eightTrigramsProgress.acceptedSubtaskIds.empty()) {
            const uint32_t acceptedGroupId = *std::max_element(
                g_eightTrigramsProgress.acceptedSubtaskIds.begin(),
                g_eightTrigramsProgress.acceptedSubtaskIds.end());
            const size_t acceptedIndex = FindEightTrigramsStepIndexForGroup(acceptedGroupId);
            if (acceptedIndex < EIGHT_TRIGRAMS_STEP_COUNT) {
                startIndex = acceptedIndex;
                g_eightTrigramsResumeStepIndex.store(static_cast<int>(startIndex));
                return true;
            }
        }

        if (!g_eightTrigramsProgress.finishedSubtaskIds.empty()) {
            const uint32_t finishedGroupId = *std::max_element(
                g_eightTrigramsProgress.finishedSubtaskIds.begin(),
                g_eightTrigramsProgress.finishedSubtaskIds.end());
            const size_t nextIndex = FindEightTrigramsStepIndexAfterGroup(finishedGroupId);
            if (nextIndex < EIGHT_TRIGRAMS_STEP_COUNT) {
                startIndex = nextIndex;
                g_eightTrigramsResumeStepIndex.store(static_cast<int>(startIndex));
                return true;
            }
            startIndex = EIGHT_TRIGRAMS_STEP_COUNT;
            g_eightTrigramsResumeStepIndex.store(static_cast<int>(startIndex));
            return true;
        }
    }

    if (g_eightTrigramsProgress.talkCurrentId > 0) {
        const uint32_t currentId = g_eightTrigramsProgress.talkCurrentId;
        if (currentId >= groupOrder.size()) {
            startIndex = EIGHT_TRIGRAMS_STEP_COUNT;
            return true;
        }

        const uint32_t nextGroupId = groupOrder[currentId];
        const size_t nextIndex = FindEightTrigramsStepIndexForGroup(nextGroupId);
        if (nextIndex < EIGHT_TRIGRAMS_STEP_COUNT) {
            startIndex = nextIndex;
            g_eightTrigramsResumeStepIndex.store(static_cast<int>(startIndex));
            return true;
        }
    }

    startIndex = defaultIndex;
    return false;
}

static std::wstring BuildTaskZoneStatus(const EightTrigramsStep& step, size_t index, size_t total) {
    std::wstring status = L"任务区：";
    status += std::to_wstring(index + 1);
    status += L"/";
    status += std::to_wstring(total);
    status += L" ";

    switch (step.kind) {
        case TaskZoneStepKind::Talk:
            status += L"对话 ";
            break;
        case TaskZoneStepKind::Acquire:
        case TaskZoneStepKind::AlertAcquire:
        case TaskZoneStepKind::SceneAcquire:
        case TaskZoneStepKind::RewardOnly:
            status += (step.kind == TaskZoneStepKind::RewardOnly ? L"前往 " : L"获取 ");
            break;
        case TaskZoneStepKind::Battle:
            status += L"战斗 ";
            break;
        case TaskZoneStepKind::Flash:
            status += L"动画 ";
            break;
        case TaskZoneStepKind::BranchOnly:
            status += L"结果 ";
            break;
    }

    status += step.label;

    if (step.sceneId != 0) {
        std::wstring mapName = GetMapName(static_cast<int>(step.sceneId));
        if (mapName.empty()) {
            mapName = std::to_wstring(step.sceneId);
        }
        status += L" @";
        status += mapName;
    }
    return status;
}

void ProcessTaskZoneUserTaskListResponse(const GamePacket& packet) {
    if (packet.body.empty()) {
        return;
    }

    size_t offset = 0;
    std::string newestFlag;
    if (!ReadPacketString(packet.body.data(), packet.body.size(), offset, newestFlag)) {
        return;
    }

    std::vector<uint32_t> acceptedSubtaskIds;
    std::vector<uint32_t> finishedSubtaskIds;

    if (offset + 4 > packet.body.size()) {
        return;
    }
    const uint32_t progressTaskId = g_taskZoneProgressTaskId.load();
    const int acceptedCount = ReadInt32LE(packet.body.data(), offset);
    for (int i = 0; i < acceptedCount; ++i) {
        if (offset + 8 > packet.body.size()) {
            return;
        }

        const uint32_t subtaskId = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
        const int taskParamCount = ReadInt32LE(packet.body.data(), offset);
        for (int j = 0; j < taskParamCount; ++j) {
            if (offset + 8 > packet.body.size()) {
                return;
            }
            ReadInt32LE(packet.body.data(), offset);
            ReadInt32LE(packet.body.data(), offset);
        }

        const uint32_t subtaskTaskId = subtaskId - (subtaskId % 1000);
        if (subtaskTaskId == progressTaskId) {
            acceptedSubtaskIds.push_back(subtaskId);
        }
    }

    if (offset + 4 > packet.body.size()) {
        return;
    }
    const int finishedCount = ReadInt32LE(packet.body.data(), offset);
    for (int i = 0; i < finishedCount; ++i) {
        if (offset + 8 > packet.body.size()) {
            return;
        }

        const uint32_t subtaskId = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
        ReadInt32LE(packet.body.data(), offset);  // finishTime

        const uint32_t subtaskTaskId = subtaskId - (subtaskId % 1000);
        if (subtaskTaskId == progressTaskId) {
            finishedSubtaskIds.push_back(subtaskId);
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
        g_eightTrigramsProgress.acceptedSubtaskIds = std::move(acceptedSubtaskIds);
        g_eightTrigramsProgress.finishedSubtaskIds = std::move(finishedSubtaskIds);
        g_eightTrigramsProgress.userTaskListLoaded = true;
    }

    // 进度推送必须在释放 g_eightTrigramsProgress.mutex 之后进行：
    // PushHuangchengWeijiProgressToUi 自己会再取这把非递归锁，嵌套会自死锁。
    // 主线卡片按系列切换，任何主线组查询都要回推，不能只认 2001000。
    if (IsMainlineStoryTaskId(progressTaskId)) {
        PushHuangchengWeijiProgressToUi(progressTaskId);
    }
}

void ProcessEightTrigramsTaskTalkResponse(const GamePacket& packet) {
    size_t offset = 0;
    uint32_t dialogId = 0;
    uint32_t npcId = 0;
    uint32_t currentId = 0;
    uint32_t trigram = 0;
    uint32_t exp = 0;
    uint32_t itemId = 0;
    bool hasDialogId = false;
    bool hasNpcId = false;
    bool hasCurrentId = false;
    bool hasTrigram = false;
    bool hasExp = false;
    bool hasItemId = false;

    auto readUInt32 = [&packet, &offset](uint32_t& value) -> bool {
        if (offset + 4 > packet.body.size()) {
            return false;
        }
        value = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
        return true;
    };

    // AS3 TaskParse.case 15: [coin, exp, cultivate, itemCount, (type,id,count)...].
    // 水魂奖励在点击战后回话的确认按钮后返回；第一次 NPC 点击也兼容直接奖励。
    if (packet.params == 15) {
        uint32_t ignored = 0;
        if (!readUInt32(ignored) || !readUInt32(ignored) || !readUInt32(ignored)) {
            return;
        }

        uint32_t itemCount = 0;
        uint32_t rewardItemId = 0;
        uint32_t rewardItemCount = 0;
        if (offset < packet.body.size()) {
            if (!readUInt32(itemCount) || itemCount > 1024) {
                return;
            }
            for (uint32_t i = 0; i < itemCount; ++i) {
                uint32_t itemType = 0;
                uint32_t itemIdValue = 0;
                uint32_t itemCountValue = 0;
                if (!readUInt32(itemType) ||
                    !readUInt32(itemIdValue) ||
                    !readUInt32(itemCountValue)) {
                    return;
                }
                if (itemIdValue == 400059 && itemCountValue > 0) {
                    rewardItemId = itemIdValue;
                    rewardItemCount = itemCountValue;
                }
            }
        }

        bool responseCaptured = false;
        bool rewardCaptured = false;
        {
            std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
            if (g_taskZoneRunning.load() &&
                g_eightTrigramsProgress.captureNextTaskTalkResponse) {
                g_eightTrigramsProgress.captureNextTaskTalkResponse = false;
                g_eightTrigramsProgress.taskTalkResponseHasDialogId = false;
                g_eightTrigramsProgress.taskTalkResponseReceived = true;
                g_eightTrigramsProgress.taskTalkResponseType = packet.params;
                g_eightTrigramsProgress.taskTalkResponseDialogId = 0;
                responseCaptured = true;

                // 兼容服务端直接在第一次点击后发奖励的情况。
                if (rewardItemId == 400059 && rewardItemCount > 0) {
                    g_eightTrigramsProgress.waterSoulRewardReceived = true;
                    g_eightTrigramsProgress.waterSoulRewardItemId = rewardItemId;
                    g_eightTrigramsProgress.waterSoulRewardItemCount = rewardItemCount;
                }
            }
            if (g_taskZoneRunning.load() &&
                g_eightTrigramsProgress.awaitingWaterSoulReward) {
                g_eightTrigramsProgress.awaitingWaterSoulReward = false;
                g_eightTrigramsProgress.waterSoulRewardReceived = true;
                g_eightTrigramsProgress.waterSoulRewardItemId = rewardItemId;
                g_eightTrigramsProgress.waterSoulRewardItemCount = rewardItemCount;
                g_eightTrigramsProgress.taskTalkResponseReceived = true;
                g_eightTrigramsProgress.taskTalkResponseType = packet.params;
                rewardCaptured = true;
            }
        }
        if (responseCaptured || rewardCaptured) {
            UpdateTaskZoneUi(rewardItemCount > 0
                                 ? L"任务区：已获得妖怪内丹"
                                 : L"任务区：已收到水魂任务回话",
                             true);
        }
        return;
    }

    switch (packet.params) {
        case 1:
        case 5:
        case 22: {
            uint32_t itemCount = 0;
            if (!readUInt32(npcId) || !readUInt32(itemCount)) {
                return;
            }
            for (uint32_t i = 0; i < itemCount; ++i) {
                uint32_t itemType = 0;
                uint32_t itemIdValue = 0;
                uint32_t itemCountValue = 0;
                if (!readUInt32(itemType) ||
                    !readUInt32(itemIdValue) ||
                    !readUInt32(itemCountValue)) {
                    return;
                }
            }
            if (!readUInt32(dialogId)) {
                return;
            }
            hasNpcId = true;
            hasDialogId = true;
            break;
        }
        case 2:
        case 14:
        case 17:
            if (!readUInt32(dialogId)) {
                return;
            }
            hasDialogId = true;
            break;
        case 3:
            if (!readUInt32(dialogId)) {
                return;
            }
            hasDialogId = true;
            break;
        case 7: {
            uint32_t boardId = 0;
            if (!readUInt32(boardId) ||
                !readUInt32(dialogId) ||
                !readUInt32(npcId)) {
                return;
            }
            hasDialogId = true;
            hasNpcId = true;
            break;
        }
        case 11:
            if (!readUInt32(dialogId) ||
                !readUInt32(currentId) ||
                !readUInt32(trigram) ||
                !readUInt32(exp) ||
                !readUInt32(npcId) ||
                !readUInt32(itemId)) {
                return;
            }
            hasDialogId = true;
            hasNpcId = true;
            hasCurrentId = true;
            hasTrigram = true;
            hasExp = true;
            hasItemId = true;
            break;
        case 21: {
            uint32_t paramCount = 0;
            if (!readUInt32(npcId) || !readUInt32(paramCount)) {
                return;
            }
            for (uint32_t i = 0; i < paramCount; ++i) {
                uint32_t itemType = 0;
                uint32_t itemIdValue = 0;
                if (!readUInt32(itemType) || !readUInt32(itemIdValue)) {
                    return;
                }
            }
            if (!readUInt32(dialogId)) {
                return;
            }
            hasNpcId = true;
            hasDialogId = true;
            break;
        }
        case 26:
            if (!readUInt32(dialogId)) {
                return;
            }
            hasDialogId = true;
            break;
        default:
            return;
    }

    // 805/806 是结果对话本身，回包只作记录，不参与战后确认时序。
    bool postBattleResponseCaptured = false;
    if (hasDialogId && (dialogId == 400600805 || dialogId == 400600806)) {
        std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
        if (g_taskZoneRunning.load() &&
            g_eightTrigramsProgress.capturePostBattleTaskTalkResponse) {
            g_eightTrigramsProgress.capturePostBattleTaskTalkResponse = false;
            g_eightTrigramsProgress.postBattleTaskTalkResponseReceived = true;
            g_eightTrigramsProgress.postBattleTaskTalkDialogId = dialogId;
            g_eightTrigramsProgress.postBattleTaskTalkNpcId = hasNpcId ? npcId : 101301;
            postBattleResponseCaptured = true;
        }
    }
    if (postBattleResponseCaptured) {
        UpdateTaskZoneUi(L"任务区：收到墨魂战后回话", true);
        return;
    }

    // 水魂战后对话的打开包和确认包由任务线程按固定参数发送，
    // 不等待服务端先返回 dialogId。
    bool responseCaptured = false;
    {
        std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
        if (g_taskZoneRunning.load() &&
            g_eightTrigramsProgress.captureNextTaskTalkResponse) {
            g_eightTrigramsProgress.captureNextTaskTalkResponse = false;
            g_eightTrigramsProgress.taskTalkResponseReceived = true;
            g_eightTrigramsProgress.taskTalkResponseType = packet.params;
            g_eightTrigramsProgress.taskTalkResponseHasDialogId = hasDialogId;
            g_eightTrigramsProgress.taskTalkResponseDialogId = hasDialogId ? dialogId : 0;
            g_eightTrigramsProgress.taskTalkResponseNpcId = hasNpcId ? npcId : 0;
            g_eightTrigramsProgress.talkDialogId = hasDialogId ? dialogId : 0;
            if (hasNpcId) {
                g_eightTrigramsProgress.talkNpcId = npcId;
            }
            responseCaptured = true;
        }
    }
    if (responseCaptured) {
        return;
    }

    uint32_t expectedDialogId = 0;
    uint32_t expectedNpcId = 0;
    bool matchNpcId = true;
    {
        std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
        expectedDialogId = g_eightTrigramsProgress.taskTalkResponseDialogId;
        expectedNpcId = g_eightTrigramsProgress.taskTalkResponseNpcId;
        matchNpcId = g_eightTrigramsProgress.taskTalkResponseMatchNpcId;
        if (!hasDialogId ||
            dialogId != expectedDialogId ||
            (matchNpcId && hasNpcId && npcId != expectedNpcId)) {
            return;
        }

        g_eightTrigramsProgress.taskTalkResponseReceived = true;
        g_eightTrigramsProgress.taskTalkResponseType = packet.params;
        g_eightTrigramsProgress.taskTalkResponseDialogId = dialogId;
        g_eightTrigramsProgress.talkDialogId = dialogId;
        if (hasNpcId) {
            g_eightTrigramsProgress.taskTalkResponseNpcId = npcId;
            g_eightTrigramsProgress.talkNpcId = npcId;
        }
        if (hasCurrentId) {
            g_eightTrigramsProgress.talkCurrentId = currentId;
        }
        if (hasTrigram) {
            g_eightTrigramsProgress.talkTrigram = trigram;
        }
        if (hasExp) {
            g_eightTrigramsProgress.talkExp = exp;
        }
        if (hasItemId) {
            g_eightTrigramsProgress.talkItemId = itemId;
        }
    }

}
void UpdateTaskZoneUi(const std::wstring& text, bool running) {
    UIBridge::Instance().UpdateHelperText(text);
    std::wstring script = L"if(window.updateTaskZoneStatus) { window.updateTaskZoneStatus('" +
                          UIBridge::EscapeJsonString(text) + L"', " +
                          (running ? L"true" : L"false") + L"); }";
    PostScriptToUI(script);
}

BOOL QueryTaskZoneUserTaskListProgress(uint32_t taskId) {
    if (taskId == 0) {
        return FALSE;
    }

    g_taskZoneProgressTaskId.store(taskId);
    ResetTaskZoneUserTaskListCache();

    auto packet = PacketBuilder()
        .SetOpcode(Opcode::USER_TASK_LIST_SEND)
        .Build();

    const BOOL result = SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()), Opcode::USER_TASK_LIST_BACK, 3000);
    if (!result) {
        g_taskZoneProgressTaskId.store(0);
        return FALSE;
    }
    return TRUE;
}

// UI 线程不能直接做阻塞等待：游戏与本界面在同一进程、同一 UI 线程上跑，
// 在 UI 线程里阻塞会连 socket 回包处理一起卡住，3 秒内必然拿不到
// USER_TASK_LIST_BACK，于是「刷新进度」总是报失败；而「完成所选」是开 worker
// 线程查询的，所以在 UI 线程外做等待就正常。这里把刷新也挪到独立线程。
struct TaskZoneProgressQueryArgs {
    uint32_t taskId;
};

// 进度查询去重：查询共用 g_taskZoneProgressTaskId 单槽和同一份缓存，
// 并发两路会互相清缓存、按错误的 taskId 过滤回包。连点「刷新」只跑一路。
static std::atomic<bool> g_taskZoneProgressQueryBusy{false};

static DWORD WINAPI TaskZoneProgressQueryThreadProc(LPVOID lpParam) {
    std::unique_ptr<TaskZoneProgressQueryArgs> args(
        static_cast<TaskZoneProgressQueryArgs*>(lpParam));
    const uint32_t taskId = args->taskId;
    if (QueryTaskZoneUserTaskListProgress(taskId)) {
        UIBridge::Instance().UpdateHelperText(L"任务区：主线任务进度已刷新");
    } else {
        UIBridge::Instance().UpdateHelperText(L"任务区：读取主线任务进度失败");
    }
    g_taskZoneProgressQueryBusy.store(false);
    return 0;
}

BOOL QueryTaskZoneUserTaskListProgressAsync(uint32_t taskId) {
    if (taskId == 0) {
        return FALSE;
    }
    // 已有刷新在跑：忽略本次点击，避免两路查询互相干扰。
    bool expected = false;
    if (!g_taskZoneProgressQueryBusy.compare_exchange_strong(expected, true)) {
        UIBridge::Instance().UpdateHelperText(L"任务区：进度正在刷新中...");
        return TRUE;
    }
    TaskZoneProgressQueryArgs* args = new TaskZoneProgressQueryArgs{taskId};
    HANDLE hThread = CreateThread(nullptr, 0, TaskZoneProgressQueryThreadProc, args, 0, nullptr);
    if (!hThread) {
        delete args;
        g_taskZoneProgressQueryBusy.store(false);
        return FALSE;
    }
    CloseHandle(hThread);
    return TRUE;
}

// 进图是全局响应事件，这里不用 ResponseWaiter，避免被其他自动流程抢走。
static bool WaitForEightTrigramsMapEnter(DWORD timeoutMs) {
    const DWORD startTick = GetTickCount();
    while (g_taskZoneRunning.load()) {
        if (g_taskZoneMapEntered.load()) {
            return true;
        }

        if (GetTickCount() - startTick >= timeoutMs) {
            return false;
        }

        Sleep(50);
    }

    return false;
}

// 水魂战斗结束后，AS3 先消费 TASK_TALK_BACK(type=15) 的奖励，再推进到墨艮。
static bool WaitForEightTrigramsWaterSoulReward(
    unsigned long long sessionId,
    DWORD timeoutMs) {
    const DWORD startTick = GetTickCount();
    while (g_taskZoneRunning.load() && g_taskZoneSession.load() == sessionId) {
        {
            std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
            if (g_eightTrigramsProgress.waterSoulRewardReceived) {
                return true;
            }
        }

        if (GetTickCount() - startTick >= timeoutMs) {
            return false;
        }
        Sleep(50);
    }
    return false;
}


[[maybe_unused]] static bool WaitForEightTrigramsPostBattleDialog(
    unsigned long long sessionId,
    DWORD timeoutMs) {
    const DWORD startTick = GetTickCount();
    while (g_taskZoneRunning.load() && g_taskZoneSession.load() == sessionId) {
        {
            std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
            if (g_eightTrigramsProgress.postBattleTaskTalkResponseReceived) {
                return true;
            }
        }

        if (GetTickCount() - startTick >= timeoutMs) {
            return false;
        }
        Sleep(50);
    }
    return false;
}

[[maybe_unused]] static void UpdateEightTrigramsResumeHintByDialogId(uint32_t dialogId) {
    const size_t stepIndex = FindEightTrigramsStepIndexByDialogId(dialogId);
    if (stepIndex < EIGHT_TRIGRAMS_STEP_COUNT) {
        UpdateEightTrigramsResumeHint(stepIndex + 1);
    }
}



// 布防水魂交丹对话采集：点击 61242 之前调用，随后收到的 TASK_TALK_BACK 会写入进度状态。
static void ArmEightTrigramsWaterSoulDialogCapture() {
    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    g_eightTrigramsProgress.captureNextTaskTalkResponse = true;
    g_eightTrigramsProgress.taskTalkResponseReceived = false;
    g_eightTrigramsProgress.taskTalkResponseHasDialogId = false;
    g_eightTrigramsProgress.taskTalkResponseDialogId = 0;
    g_eightTrigramsProgress.taskTalkResponseNpcId = 0;
}

// 未走交丹确认路径时收回对话采集标记，避免后续无关回包被当成水魂对话写入进度状态。
static void DisarmEightTrigramsWaterSoulDialogCapture() {
    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    g_eightTrigramsProgress.captureNextTaskTalkResponse = false;
}

// 取水魂交丹对话 id：回包可用且指向水魂时用回包值，否则退回抓包确认的固定 id。
static uint32_t TakeEightTrigramsWaterSoulDialogId() {
    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    g_eightTrigramsProgress.captureNextTaskTalkResponse = false;

    const uint32_t dialogId = g_eightTrigramsProgress.taskTalkResponseHasDialogId
                                  ? g_eightTrigramsProgress.taskTalkResponseDialogId
                                  : 0;
    const uint32_t npcId = g_eightTrigramsProgress.taskTalkResponseNpcId;
    if (g_eightTrigramsProgress.taskTalkResponseReceived &&
        dialogId >= 400600500 && dialogId < 400600600 &&
        (npcId == 0 || npcId == 61242)) {
        return dialogId;
    }

    return EIGHT_TRIGRAMS_WATER_SOUL_DIALOG_ID;
}

// 交丹确认前登记奖励等待：type 15 回包会写入 waterSoulRewardReceived。
static void ArmEightTrigramsWaterSoulRewardWait() {
    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    g_eightTrigramsProgress.awaitingWaterSoulReward = true;
    g_eightTrigramsProgress.waterSoulRewardReceived = false;
}

// 奖励等待窗口结束或流程中断时收回等待标记，避免后续无关回包误置奖励状态。
static void ClearEightTrigramsWaterSoulRewardWait() {
    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    g_eightTrigramsProgress.awaitingWaterSoulReward = false;
}

static bool EightTrigramsWaterSoulRewardReceived() {
    std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
    return g_eightTrigramsProgress.waterSoulRewardReceived;
}
BOOL SendTaskZoneTalkPacket(
    uint32_t npcId,
    uint32_t dialogId,
    uint32_t chooseId,
    bool waitForBack,
    DWORD timeoutMs,
    bool matchNpcId) {
    (void)waitForBack;
    (void)timeoutMs;
    (void)matchNpcId;

    // 八卦灵盘是固定流程：普通对话只按预设包序列发送，
    // 不等待 TASK_TALK_BACK，也不从回包动态提取下一个 dialogId。
    if (chooseId != 0) {
        auto trainInfoPacket = PacketBuilder()
            .SetOpcode(Opcode::TRAIN_INFO_SEND)
            .SetParams(7)
            .WriteUInt32(dialogId)
            .WriteUInt32(chooseId)
            .Build();

        if (!SendPacket(0,
                        trainInfoPacket.data(),
                        static_cast<DWORD>(trainInfoPacket.size()))) {
            return FALSE;
        }
        Sleep(EIGHT_TRIGRAMS_TALK_DELAY_MS);
    }

    auto packet = PacketBuilder()
        .SetOpcode(Opcode::TASK_TALK_SEND)
        .SetParams(npcId)
        .WriteUInt32(dialogId)
        .Build();

    if (!SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()))) {
        return FALSE;
    }

    Sleep(EIGHT_TRIGRAMS_TALK_DELAY_MS);
    return TRUE;
}

BOOL SendTaskZoneClickPacket(uint32_t npcId, bool battleMode) {
    uint32_t counter = battleMode ? static_cast<uint32_t>(g_battleCounter.load()) : 0;
    if (battleMode && counter == 0) {
        counter = 1;
    }

    auto packet = PacketBuilder()
        .SetOpcode(Opcode::CLICK_NPC)
        .SetParams(npcId)
        .WriteUInt32(counter)
        .Build();

    if (!SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()))) {
        return FALSE;
    }

    if (!battleMode) {
        Sleep(EIGHT_TRIGRAMS_CLICK_DELAY_MS);
    }
    return TRUE;
}


static constexpr int MAX_CONSECUTIVE_RESTARTS = 3;

static constexpr DWORD RESTART_BACKOFF_BASE_MS = 1000;
static constexpr DWORD RESTART_BACKOFF_MAX_MS = 5000;
static constexpr DWORD BATTLE_DRAIN_TIMEOUT_MS = 15000;

// 水魂点击后的等待：等交丹对白到达即返回，超时也会返回，由调用方统一发确认包。
enum class EightTrigramsWaterSoulOutcome {
    BattleStarted,
    DialogReply,
    Timeout
};

static EightTrigramsWaterSoulOutcome WaitForEightTrigramsWaterSoulOutcome(
    unsigned long long sessionId,
    DWORD timeoutMs) {
    const DWORD startTick = GetTickCount();
    while (g_taskZoneRunning.load() && g_taskZoneSession.load() == sessionId) {
        const BattleData battle = PacketParser::GetCurrentBattleSnapshot();
        if (battle.active && !battle.myPets.empty() && !battle.otherPets.empty()) {
            return EightTrigramsWaterSoulOutcome::BattleStarted;
        }

        {
            std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
            if (g_eightTrigramsProgress.taskTalkResponseReceived) {
                return EightTrigramsWaterSoulOutcome::DialogReply;
            }
        }

        if (GetTickCount() - startTick >= timeoutMs) {
            return EightTrigramsWaterSoulOutcome::Timeout;
        }
        Sleep(100);
    }

    return EightTrigramsWaterSoulOutcome::Timeout;
}

// 点击水魂并收口：发送侧抓包显示水魂段同样零战斗包
//（只有 CLICK_NPC(61242,[0]) + TASK_TALK(61242,[400600509])），
// 服务端在点击后只下发交丹对白（「不要再攻击我啦」），
// 回 TASK_TALK(61242,[dialogId]) 才会给妖怪内丹并推进到 7002。
// 因此这里不做战斗检测，也不进入战斗循环。
// 返回 false 表示流程中断或包发送失败（调用方应结束本步）。
static bool RunEightTrigramsWaterSoulInteraction(
    unsigned long long sessionId,
    uint32_t npcId) {
    ArmEightTrigramsWaterSoulDialogCapture();
    if (!SendTaskZoneClickPacket(npcId, false)) {
        UpdateTaskZoneUi(L"任务区：点击水魂失败", false);
        return false;
    }

    // 等交丹对白到达（或超时）后统一走确认包，回包稍慢也不会卡死流程。
    WaitForEightTrigramsWaterSoulOutcome(sessionId, EIGHT_TRIGRAMS_BATTLE_START_WAIT_MS);

    // 点击回包可能已经直接给了内丹（服务端直接把交丹奖励放在点击响应里）：
    // 那就不要再补发确认对话，否则会多一个多余的任务请求。
    bool rewardAlreadyReceived = false;
    {
        std::lock_guard<std::mutex> lock(g_eightTrigramsProgress.mutex);
        rewardAlreadyReceived = g_eightTrigramsProgress.waterSoulRewardReceived;
    }

    uint32_t waterSoulDialogId = 0;
    if (rewardAlreadyReceived) {
        // 点击即给奖励：收回采集标记，避免后续无关回包被当成水魂对话。
        DisarmEightTrigramsWaterSoulDialogCapture();
    } else {
        waterSoulDialogId = TakeEightTrigramsWaterSoulDialogId();
        ArmEightTrigramsWaterSoulRewardWait();
        if (!SendTaskZoneTalkPacket(npcId, waterSoulDialogId)) {
            ClearEightTrigramsWaterSoulRewardWait();
            UpdateTaskZoneUi(L"任务区：确认水魂交丹对话失败", false);
            return false;
        }

        WaitForEightTrigramsWaterSoulReward(
            sessionId,
            EIGHT_TRIGRAMS_WATER_SOUL_DIALOG_WAIT_MS);
        ClearEightTrigramsWaterSoulRewardWait();
    }
    Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);

    // 回显实际使用的 dialogId，便于运行时抓包核对服务端下发的对话编号。
    UpdateTaskZoneUi(rewardAlreadyReceived
                         ? L"任务区：水魂点击已直接发放妖怪内丹"
                         : L"任务区：水魂交丹确认 " + std::to_wstring(waterSoulDialogId) +
                               (EightTrigramsWaterSoulRewardReceived() ? L"（已获得妖怪内丹）"
                                                                       : L"（等待奖励回包）"),
                     true);
    return true;
}

static DWORD WINAPI EightTrigramsTaskThreadProc(LPVOID lpParam) {
    std::unique_ptr<EightTrigramsTaskData> taskData(static_cast<EightTrigramsTaskData*>(lpParam));
    unsigned long long sessionId = taskData->sessionId;
    int autoRestartCount = 0;
    size_t lastRestartStepIndex = EIGHT_TRIGRAMS_STEP_COUNT;
    bool flowCompleted = false;
    bool flowFailed = false;

    while (g_taskZoneRunning.load()) {
        const size_t stepCount = sizeof(EIGHT_TRIGRAMS_STEPS) / sizeof(EIGHT_TRIGRAMS_STEPS[0]);
        size_t startIndex = 0;
        uint32_t currentSceneId = 0;
        std::wstring startText;

        if (startIndex >= stepCount) {
            startIndex = 0;
        }

        if (g_eightTrigramsResumeStepIndex.load() >= static_cast<int>(stepCount)) {
            flowCompleted = true;
            g_eightTrigramsResumeStepIndex.store(static_cast<int>(stepCount));
            break;
        }

        flowFailed = false;

        ResetEightTrigramsProgressState();
        g_taskZoneMapEntered = false;
        UpdateTaskZoneUi(L"任务区：读取当前进度...", true);
        const bool progressLoaded = QueryTaskZoneUserTaskListProgress(4006000);
        bool hasResumeIndex = TryResolveEightTrigramsResumeIndex(startIndex);
        if (!progressLoaded && !hasResumeIndex) {
            UpdateTaskZoneUi(L"任务区：读取当前进度失败", false);
            flowFailed = true;
            goto CLEANUP;
        }

        if (startIndex >= stepCount) {
            flowCompleted = true;
            g_eightTrigramsResumeStepIndex.store(static_cast<int>(stepCount));
            break;
        }

        startText = (startIndex == 0) ? L"任务区：开始执行八卦灵盘..." : L"任务区：继续执行八卦灵盘...";
        UpdateTaskZoneUi(startText, true);
        Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);

        for (size_t i = startIndex; i < stepCount && g_taskZoneRunning.load(); ++i) {
            if (g_taskZoneSession.load() != sessionId) {
                goto CLEANUP;
            }

            UpdateEightTrigramsResumeHint(i);
            const EightTrigramsStep& step = EIGHT_TRIGRAMS_STEPS[i];
            UpdateTaskZoneUi(BuildTaskZoneStatus(step, i - startIndex, stepCount - startIndex), true);

            // 场景进入可以等待场景回包确认；普通对话不等待 TASK_TALK_BACK。
            if (step.sceneId != 0 && step.sceneId != currentSceneId) {
                std::wstring mapName = GetMapName(static_cast<int>(step.sceneId));
                if (mapName.empty()) {
                    mapName = std::to_wstring(step.sceneId);
                }
                UpdateTaskZoneUi(L"任务区：进入 " + mapName + L"...", true);
                g_taskZoneMapEntered = false;
                if (!SendEnterScenePacket(static_cast<int>(step.sceneId)) ||
                    !WaitForEightTrigramsMapEnter(5000)) {
                    UpdateTaskZoneUi(L"任务区：进入场景失败", false);
                    flowFailed = true;
                    break;
                }
                currentSceneId = step.sceneId;
                Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);
            }

            if (step.kind == TaskZoneStepKind::Flash) {
                if (!SendTaskZoneTalkPacket(91435, 400600704)) {
                    UpdateTaskZoneUi(L"任务区：转盘收口失败", false);
                    flowFailed = true;
                    break;
                }
                UpdateEightTrigramsResumeHint(i + 1);
                continue;
            }

            if (step.kind == TaskZoneStepKind::BranchOnly) {
                UpdateEightTrigramsResumeHint(i + 1);
                continue;
            }

            if (step.kind == TaskZoneStepKind::Battle) {
                if (step.dialogId == 400600506) {
                    // 水魂 61242 与 AS3 同为一条路径：AutoBaGuaView step14 点击 61242 ->
                    // MapControl.onClickNPCNew -> TaskControl.getTaskDialog 发出 CLICK_NPC(61242,[0])。
                    // 旧的 0x10000817 是 combine1(1,0,8,23) 的怪物 sid，和 61242 无关，服务端无法定位目标。
                    // 服务端对该点击有两条响应：开战，或只下发交丹对白（「不要再攻击我啦」）。
                    // 交丹对白必须回 TASK_TALK(61242,[dialogId])，否则拿不到妖怪内丹，流程会停在这里。
                    if (!RunEightTrigramsWaterSoulInteraction(sessionId, step.npcId)) {
                        flowFailed = true;
                        break;
                    }
                } else {
                    // 墨魂不走战斗：发送侧抓包（墨魂段 83-86 行）与第三方实现一致，全程零战斗包——
                    // 没有 BATTLE_READY(1186049) / USER_OP(1186050) / BATTLE_END_SEND(1186323)。
                    // 战斗由服务端在收到上一 Talk 步的 TASK_TALK(101201,[400600803])（「少废话，看打！」）
                    // 后自行结算，native 既不用驱动也不用检测。
                    // 400600804 在 taskprop_4006000.xml 里只有 <alert>、没有 <desc>，是纯目标提示节点；
                    // 补发 TASK_TALK(101201,[400600804]) 会让服务端重推 803，界面卡在「少废话，看打！」。
                    // 这里只补战后确认 TASK_TALK(101301,[400600805])，与第三方 803 -> 805 一致。
                    if (step.dialogId == 400600804 &&
                        !SendTaskZoneTalkPacket(101301, 400600805)) {
                        UpdateTaskZoneUi(L"任务区：确认墨魂战后结果失败", false);
                        flowFailed = true;
                        break;
                    }
                    Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);
                }

                UpdateEightTrigramsResumeHint(i + 1);
                continue;
            }

            const bool shouldClickNpc =
                step.kind == TaskZoneStepKind::Talk ||
                step.kind == TaskZoneStepKind::Acquire ||
                step.kind == TaskZoneStepKind::AlertAcquire;
            if (shouldClickNpc && !SendTaskZoneClickPacket(step.npcId, false)) {
                UpdateTaskZoneUi(L"任务区：点击目标失败", false);
                flowFailed = true;
                break;
            }

            if (step.kind == TaskZoneStepKind::SceneAcquire) {
                const uint32_t specialAreaNpcId = step.npcId + 1;
                if (!SendTaskZoneClickPacket(specialAreaNpcId, false)) {
                    UpdateTaskZoneUi(L"任务区：获取失败", false);
                    flowFailed = true;
                    break;
                }
                UpdateEightTrigramsResumeHint(i + 1);
                continue;
            }

            if (step.kind == TaskZoneStepKind::AlertAcquire ||
                step.kind == TaskZoneStepKind::Acquire ||
                step.kind == TaskZoneStepKind::Talk) {
                if (!SendTaskZoneTalkPacket(step.npcId, step.dialogId, step.chooseId)) {
                    UpdateTaskZoneUi(L"任务区：固定对话发送失败", false);
                    flowFailed = true;
                    break;
                }
                UpdateEightTrigramsResumeHint(i + 1);
                continue;
            }

            if (step.dialogId != 0) {
                if (!SendTaskZoneTalkPacket(step.npcId, step.dialogId, step.chooseId)) {
                    UpdateTaskZoneUi(L"任务区：固定对话发送失败", false);
                    flowFailed = true;
                    break;
                }
                UpdateEightTrigramsResumeHint(i + 1);
            }
        }

    CLEANUP:
        if (flowFailed &&
            g_taskZoneRunning.load() &&
            g_taskZoneSession.load() == sessionId) {
            size_t resolvedIndex = 0;
            const bool hasResolvedIndex = TryResolveEightTrigramsResumeIndex(resolvedIndex);
            if (hasResolvedIndex && resolvedIndex >= stepCount) {
                flowCompleted = true;
                g_eightTrigramsResumeStepIndex.store(static_cast<int>(EIGHT_TRIGRAMS_STEP_COUNT));
                break;
            }

            if (!hasResolvedIndex) {
                const int checkpoint = g_eightTrigramsResumeStepIndex.load();
                resolvedIndex = (checkpoint >= 0 && checkpoint < static_cast<int>(stepCount))
                    ? static_cast<size_t>(checkpoint)
                    : 0;
            }
            if (resolvedIndex != lastRestartStepIndex) {
                lastRestartStepIndex = resolvedIndex;
                autoRestartCount = 0;
            }

            if (autoRestartCount < MAX_CONSECUTIVE_RESTARTS) {
                BattleData activeBattle = PacketParser::GetCurrentBattleSnapshot();
                if (!activeBattle.myPets.empty() || !activeBattle.otherPets.empty()) {
                    UpdateTaskZoneUi(L"任务区：等待残留战斗完全结束后再恢复...", true);
                    const DWORD drainStart = GetTickCount();
                    while (g_taskZoneRunning.load() &&
                           g_taskZoneSession.load() == sessionId) {
                        activeBattle = PacketParser::GetCurrentBattleSnapshot();
                        if (activeBattle.myPets.empty() && activeBattle.otherPets.empty()) {
                            break;
                        }
                        if (GetTickCount() - drainStart >= BATTLE_DRAIN_TIMEOUT_MS) {
                            break;
                        }
                        Sleep(100);
                    }
                    if (!g_taskZoneRunning.load() || g_taskZoneSession.load() != sessionId) {
                        break;
                    }
                    activeBattle = PacketParser::GetCurrentBattleSnapshot();
                    if (!activeBattle.myPets.empty() || !activeBattle.otherPets.empty()) {
                        UpdateTaskZoneUi(L"任务区：残留战斗未结束，停止自动重启", false);
                        break;
                    }
                }

                ++autoRestartCount;
                g_taskZoneMapEntered = false;
                ResponseWaiter::CancelWait();
                sessionId = g_taskZoneSession.fetch_add(1) + 1;

                int backoffShift = autoRestartCount - 1;
                if (backoffShift > 2) {
                    backoffShift = 2;
                }
                DWORD backoffMs = RESTART_BACKOFF_BASE_MS << backoffShift;
                if (backoffMs > RESTART_BACKOFF_MAX_MS) {
                    backoffMs = RESTART_BACKOFF_MAX_MS;
                }

                std::wstring retryText = L"任务区：步骤 ";
                retryText += std::to_wstring(resolvedIndex + 1);
                retryText += L" 中断，等待 ";
                retryText += std::to_wstring(backoffMs);
                retryText += L"ms 后恢复（第 ";
                retryText += std::to_wstring(autoRestartCount);
                retryText += L" 次）...";
                UpdateTaskZoneUi(retryText, true);

                DWORD waited = 0;
                while (g_taskZoneRunning.load() && waited < backoffMs) {
                    DWORD delay = backoffMs - waited;
                    if (delay > 100) {
                        delay = 100;
                    }
                    Sleep(delay);
                    waited += delay;
                }
                continue;
            }
        }
    }

    const bool stillRunning = g_taskZoneRunning.exchange(false);
    g_taskZoneMapEntered = false;
    if (flowCompleted) {
        g_eightTrigramsResumeStepIndex.store(static_cast<int>(EIGHT_TRIGRAMS_STEP_COUNT));
        UpdateTaskZoneUi(L"任务区：八卦灵盘流程已完成", false);
    } else if (flowFailed && stillRunning) {
        UpdateTaskZoneUi(L"任务区：流程中断", false);
    } else {
        UpdateTaskZoneUi(L"任务区：已停止", false);
    }

    return 0;
}

BOOL StartEightTrigramsTaskAsync() {
    if (g_taskZoneRunning.load()) {
        UpdateTaskZoneUi(L"任务区：八卦灵盘已在运行中", true);
        return FALSE;
    }

    if (g_LastGameSocket == 0 || g_userId.load() == 0) {
        UpdateTaskZoneUi(L"任务区：请先进入游戏", false);
        return FALSE;
    }

    if (g_battleSixAuto.IsInBattle() || g_battleSixAuto.IsAutoMatching() || g_shuangtaiAuto.IsRunning()) {
        UpdateTaskZoneUi(L"任务区：请先停止其他自动战斗", false);
        return FALSE;
    }

    size_t cachedStartIndex = 0;
    if (TryResolveEightTrigramsResumeIndex(cachedStartIndex) &&
        cachedStartIndex >= EIGHT_TRIGRAMS_STEP_COUNT) {
        UpdateTaskZoneUi(L"任务区：八卦灵盘已经完成", false);
        return FALSE;
    }
    const unsigned long long sessionId = g_taskZoneSession.fetch_add(1) + 1;
    g_taskZoneRunning = true;
    g_taskZoneMapEntered = false;
    ResetEightTrigramsProgressState();

    EightTrigramsTaskData* taskData = new EightTrigramsTaskData();
    taskData->sessionId = sessionId;

    HANDLE hThread = CreateThread(nullptr, 0, EightTrigramsTaskThreadProc, taskData, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }

    delete taskData;
    g_taskZoneRunning = false;
    g_taskZoneMapEntered = false;
    UpdateTaskZoneUi(L"任务区：启动失败", false);
    return FALSE;
}

VOID StopEightTrigramsTask() {
    if (!g_taskZoneRunning.load()) {
        UpdateTaskZoneUi(L"任务区：已停止", false);
        return;
    }

    g_taskZoneRunning = false;
    g_taskZoneMapEntered = false;
    g_taskZoneSession.fetch_add(1);
    ResponseWaiter::CancelWait();
    UpdateTaskZoneUi(L"任务区：已停止", false);
}

// 主线任务选择：UI 勾选的子任务按配方表顺序去重排序；空表示跑全部未完成。
