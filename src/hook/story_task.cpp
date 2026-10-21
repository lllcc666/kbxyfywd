#include <windows.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "activity_states_internal.h"
#include "battle_six.h"
#include "hook_shared.h"
#include "managed_thread.h"
#include "packet_builder.h"
#include "packet_parser.h"
#include "packet_protocol.h"
#include "shuangtai_internal.h"
#include "story_task_internal.h"
#include "task_zone_internal.h"
#include "story_task.h"
#include "story_task_recipe.h"
#include "ui_bridge.h"
#include "utils.h"
#include "wpe_hook.h"
#include "wpe_hook_internal.h"

extern bool PostScriptToUI(const std::wstring& jsCode);

// 本文件后半段定义的辅助函数：主线战斗收尾与对话推进先用到它们。
static bool EnsureStoryMagicList(unsigned long long sessionId, DWORD timeoutMs);
static uint32_t FirstStoryMagicId();
static std::vector<uint32_t> NormalizeStorySelection(const std::vector<uint32_t>& subtaskIds);

// ============================================================================
// 主线剧情自动任务（皇城危机、魔晶战记等）
//
// 与八卦灵盘的分工：
//   八卦灵盘有 AS3 内置自动模块，配方式是固定 recipe；主线任务没有，走普通任务
//   窗口，是服务端驱动的对话框流程。因此这里只做两件事：
//     1) 按配方表进图、点该子任务的入口 NPC；
//     2) 服务端推来 dialogId 时，按该节点类型回应（TASK_TALK / TRAIN_INFO+TASK_TALK
//        / 战斗点击），直到该子任务完成通知到达，再进入下一个子任务。
// ============================================================================

// 主线任务会话 owner：运行标记、会话号、当前子任务与当前对话节点由这里维护。
std::atomic<uint32_t> g_storyTaskCurrentSubtask{0};
std::atomic<uint32_t> g_storyTaskCurrentDialog{0};
// 主线流程当前所在场景：连续子任务在同一场景时（2001003/2001004 都在 2002）
// 不能重复发 ENTER_SCENE，否则等不到 ENTER_SCENE_BACK 会白白超时。
std::atomic<uint32_t> g_storyTaskCurrentScene{0};

struct StoryTaskState {
    std::mutex mutex;
    // 服务端下发的对话节点队列（等待线程逐个消费）。
    // 用队列而不是单槽：TASK_TALK_BACK 的 type==10 一次给两个 dialogId
    // （dialogOne/dialogTwo），AS3 也是排进 dialogList 后逐个推进。
    struct PendingDialog {
        uint32_t dialogId = 0;
        uint32_t npcId = 0;
        uint32_t type = 0;
        // type 2/14/17 的 alert 节点：服务端把 flag 设成 npcid 时，AS3 在玩家
        // 关掉提示后才回 TASK_TALK(npcid=flag)，此时即使是 alert 也必须回包。
        bool forceReply = false;
    };
    std::deque<PendingDialog> pendingDialogs;
    // 子任务完成通知。
    uint32_t finishedSubtaskId = 0;
    bool finishedSubtaskReady = false;
    // 已完成/已接子任务集合（来自 USER_TASK_LIST）。
    std::vector<uint32_t> acceptedSubtaskIds;
    std::vector<uint32_t> finishedSubtaskIds;
    bool userTaskListLoaded = false;
    // NPCTASKLIST(type=1) 给出的可接子任务；工作线程据此补发 ACCEPT_TASK。
    std::vector<uint32_t> pendingAcceptSubtaskIds;
    // 最近一次 NPC_LIST_BACK 给出的场景 NPC id（含服务端动态生成的任务 NPC）。
    std::vector<uint32_t> sceneNpcIds;
    bool sceneNpcListReady = false;
    // 已学法术 id（GETMAGICINDEX_BACK 下发）。主线「用粒子光线射击」类节点
    // 只能从这里取 actionId，本地配置没有 id 映射。
    std::vector<uint32_t> learnedMagicIds;
    bool magicIndexLoaded = false;
    // UI 勾选的子任务；空 = 跑全部未完成。
    std::vector<uint32_t> selectedSubtaskIds;
};

StoryTaskState g_storyTaskState;

// 主线任务的进度推送：只读主线状态，单独加锁，避免与 USER_TASK_LIST 回包链互锁。
static void PushStoryTaskProgressToUi() {
    std::vector<uint32_t> acceptedSubtaskIds;
    std::vector<uint32_t> finishedSubtaskIds;
    {
        std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
        acceptedSubtaskIds = g_storyTaskState.acceptedSubtaskIds;
        finishedSubtaskIds = g_storyTaskState.finishedSubtaskIds;
    }

    std::wstring script = L"if(window.updateHuangchengWeijiProgress) { window.updateHuangchengWeijiProgress({taskId:" +
                          std::to_wstring(HUANGCHENG_WEIJI_TASK_ID) +
                          L", acceptedSubtaskIds:" + BuildUInt32JsonArray(acceptedSubtaskIds) +
                          L", finishedSubtaskIds:" + BuildUInt32JsonArray(finishedSubtaskIds) +
                          L"}); }";
    PostScriptToUI(script);
}

static bool IsStorySubtaskSelected(uint32_t subtaskId) {
    std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
    if (g_storyTaskState.selectedSubtaskIds.empty()) {
        return true;
    }
    return std::find(g_storyTaskState.selectedSubtaskIds.begin(),
                     g_storyTaskState.selectedSubtaskIds.end(),
                     subtaskId) != g_storyTaskState.selectedSubtaskIds.end();
}

static const StoryTaskRecipe::Node* FindStoryNode(uint32_t dialogId) {
    for (uint32_t i = 0; i < StoryTaskRecipe::NODE_COUNT; ++i) {
        if (StoryTaskRecipe::NODES[i].dialogId == dialogId) {
            return &StoryTaskRecipe::NODES[i];
        }
    }
    return nullptr;
}

static bool IsStorySubtaskFinishedInCache(uint32_t subtaskId) {
    std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
    return std::find(g_storyTaskState.finishedSubtaskIds.begin(),
                     g_storyTaskState.finishedSubtaskIds.end(),
                     subtaskId) != g_storyTaskState.finishedSubtaskIds.end();
}

// 主线任务的进图等待：只看主线自己的运行标记与进图标记。
// 不能复用 WaitForEightTrigramsMapEnter，那个函数以 g_taskZoneRunning 为循环条件，
// 主线流程下该标记始终为 false，会立即返回失败。
static bool WaitForStoryMapEnter(unsigned long long sessionId, DWORD timeoutMs) {
    const DWORD startTick = GetTickCount();
    while (g_storyTaskRunning.load() && g_storyTaskSession.load() == sessionId) {
        if (g_storyTaskMapEntered.load() && g_storyTaskMapEnterSession.load() == sessionId) {
            return true;
        }
        if (GetTickCount() - startTick >= timeoutMs) {
            return false;
        }
        Sleep(50);
    }
    return false;
}

static void ResetStoryTaskState() {
    std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
    g_storyTaskState.pendingDialogs.clear();
    g_storyTaskState.pendingAcceptSubtaskIds.clear();
    g_storyTaskState.finishedSubtaskId = 0;
    g_storyTaskState.finishedSubtaskReady = false;
}

static void UpdateStoryTaskUi(const std::wstring& text, bool running) {
    // 停止后运行标记已清，任何迟到的 running=true 更新都不能把面板/停止按钮
    // 复活（否则用户得点两次才能真正结束）。
    const bool effectiveRunning = running && g_storyTaskRunning.load();
    UIBridge::Instance().UpdateHelperText(text);
    std::wstring script = L"if(window.updateHuangchengWeijiStatus) { window.updateHuangchengWeijiStatus('" +
                          UIBridge::EscapeJsonString(text) + L"', " +
                          (effectiveRunning ? L"true" : L"false") + L"); }";
    PostScriptToUI(script);
}

// 取服务端下发的下一个对话节点。
static bool TakeStoryPendingDialog(unsigned long long sessionId,
                                   uint32_t& dialogId,
                                   uint32_t& npcId,
                                   DWORD timeoutMs,
                                   bool* forceReply = nullptr) {
    const DWORD startTick = GetTickCount();
    while (g_storyTaskRunning.load() && g_storyTaskSession.load() == sessionId) {
        {
            std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
            if (!g_storyTaskState.pendingDialogs.empty()) {
                const StoryTaskState::PendingDialog front =
                    g_storyTaskState.pendingDialogs.front();
                g_storyTaskState.pendingDialogs.pop_front();
                dialogId = front.dialogId;
                npcId = front.npcId;
                if (forceReply != nullptr) {
                    *forceReply = front.forceReply;
                }
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

// 压入一个待回应的对话节点。dialogId 为 0 时忽略（AS3 对 dialogId==0 是改发
// GET_TASK_DIALOG，不在这里处理）。
static void PushStoryPendingDialog(uint32_t dialogId, uint32_t npcId, uint32_t type,
                                   bool forceReply = false) {
    if (dialogId == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
    StoryTaskState::PendingDialog entry;
    entry.dialogId = dialogId;
    entry.npcId = npcId;
    entry.type = type;
    entry.forceReply = forceReply;
    g_storyTaskState.pendingDialogs.push_back(entry);
}

// 取走一条 NPCTASKLIST(type=1) 给出的可接子任务。
static bool TakeStoryPendingAcceptSubtask(uint32_t& subtaskId) {
    std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
    if (g_storyTaskState.pendingAcceptSubtaskIds.empty()) {
        return false;
    }
    subtaskId = g_storyTaskState.pendingAcceptSubtaskIds.front();
    g_storyTaskState.pendingAcceptSubtaskIds.erase(
        g_storyTaskState.pendingAcceptSubtaskIds.begin());
    return true;
}

// 不消费地看一眼服务端是否已经下发了对话节点（接任务等待用）。
static bool HasStoryPendingDialog() {
    std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
    return !g_storyTaskState.pendingDialogs.empty();
}

// AS3 TaskControl.onAcceptTask：OP_CLIENT_REQ_ACCEPTTASK.send，params=subtaskId，无 body。
static bool SendStoryAcceptTaskPacket(uint32_t subtaskId) {
    if (subtaskId == 0) {
        return false;
    }
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::ACCEPT_TASK_SEND)
        .SetParams(subtaskId)
        .Build();
    if (!SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()))) {
        return false;
    }
    Sleep(EIGHT_TRIGRAMS_TALK_DELAY_MS);
    return true;
}

// 战斗节点的怪物 sid 计算，与 AS3 MonsterManger 一致：
//   battle.id == -1   -> 用当前 NPC 作为目标
//   battle.id < 10000 -> combine1(1,0,id,level)
//   否则              -> combine2(2,id)
static uint32_t BuildStoryBattleSid(const StoryTaskRecipe::Node& node, uint32_t npcId) {
    const int spiritId = node.battleId;
    if (spiritId == -1) {
        return npcId != 0 ? npcId : 1;
    }
    if (spiritId <= 0) {
        return 0;
    }
    if (spiritId < 10000) {
        const int level = static_cast<int>(node.battleLevel);
        return static_cast<uint32_t>((1 << 28 & 4026531840) |
                                     (0 << 22 & 263192576) |
                                     (spiritId << 8 & 4194048) |
                                     (level & 0xFF));
    }
    return static_cast<uint32_t>((2 << 28 & 4026531840) | (spiritId & 268435455));
}

// 查某子任务在某场景里该「报到」的任务对象（配方 STAGES）。
// 真机链路：进长安城皇宫(2002) 后向唐太宗(21301)报到，服务端才下发 200100307。
static uint32_t FindStoryStageNpc(uint32_t subtaskId, uint32_t scene) {
    for (uint32_t i = 0; i < StoryTaskRecipe::STAGE_COUNT; ++i) {
        const StoryTaskRecipe::Stage& stage = StoryTaskRecipe::STAGES[i];
        if (stage.subtaskId == subtaskId && stage.scene == scene) {
            return stage.npc;
        }
    }
    return 0;
}

// 该子任务在该场景里值得依次尝试的入口对象（配方 ENTRY_CANDIDATES，首选在前）。
static std::vector<uint32_t> GetStoryEntryCandidates(uint32_t subtaskId, uint32_t scene) {
    std::vector<uint32_t> candidates;
    for (uint32_t i = 0; i < StoryTaskRecipe::ENTRY_CANDIDATE_COUNT; ++i) {
        const StoryTaskRecipe::EntryCandidate& candidate =
            StoryTaskRecipe::ENTRY_CANDIDATES[i];
        if (candidate.subtaskId == subtaskId && candidate.scene == scene) {
            candidates.push_back(candidate.npc);
        }
    }
    return candidates;
}

// 上报 NPC 状态（AS3 MsgDoc.OP_CLIENT_NPC_STATUS，params=npcId，body 为空）。
// 客户端每加载一个 NPC 精灵都会上报（Npc.as:186 / MultipleNPC.as:755），服务端据此
// 推进任务；抓包里进新场景后先发 NPC_STATUS(21319/21301)，服务端才回下一个对白。
static bool SendStoryNpcStatusPacket(uint32_t npcId) {
    if (npcId == 0) {
        return true;
    }
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::NPC_STATUS_SEND)
        .SetParams(npcId)
        .Build();
    if (!SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()))) {
        return false;
    }
    Sleep(EIGHT_TRIGRAMS_CLICK_DELAY_MS);
    return true;
}

// 请求场景 NPC 列表。AS3 的进图回调 MainModel.onEnterSceneBack 在 ENTER_SCENE_BACK
// 成功后必定发 REQ_NPCS_LIST → OP_CLIENT_NPC_LIST.send(params=0, 空 body)。
// 抓包（2026-10-18）逐条对应：ENTER_SCENE(2002) → ENTER_SCENE_BACK → PLAYER_LIST(1184317)
// → NPC_LIST(1184782) → NPC_LIST_BACK=[3,21302,21301,21319]。
static bool RequestStorySceneNpcList(unsigned long long sessionId, DWORD timeoutMs) {
    if (g_storyTaskSession.load() != sessionId) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
        g_storyTaskState.sceneNpcListReady = false;
        g_storyTaskState.sceneNpcIds.clear();
    }
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::NPC_LIST_SEND)
        .SetParams(0)
        .Build();
    return SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()),
                      Opcode::NPC_LIST_BACK, timeoutMs) != FALSE;
}

// 取回 NPC_LIST_BACK 解析出的场景 NPC 列表。
// SendPacket 的等待发生在收包钩子里（NotifyResponse 早于业务分发），所以回包到达后
// 解析仍可能晚一两拍，这里再短轮询等 sceneNpcListReady。
static bool TakeStorySceneNpcList(unsigned long long sessionId, DWORD timeoutMs,
                                  std::vector<uint32_t>& npcIds) {
    const DWORD startTick = GetTickCount();
    while (g_storyTaskRunning.load() && g_storyTaskSession.load() == sessionId) {
        {
            std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
            if (g_storyTaskState.sceneNpcListReady) {
                npcIds = g_storyTaskState.sceneNpcIds;
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

// 进场景后向该场景的 NPC 逐个上报状态。
// 关键：任务推进依赖的常是**服务端动态生成**的任务 NPC（抓包里的 21319 在
// data/npc.xml 里根本不存在），只能从 NPC_LIST_BACK 拿到，本地配方猜不出来。
// 拿不到列表时退回配方 STAGES 里的报到 NPC，保证老流程还能走。
static void ReportStorySceneNpcStatus(unsigned long long sessionId, uint32_t sceneId) {
    std::vector<uint32_t> npcIds;
    const bool gotList = RequestStorySceneNpcList(sessionId, 2000) &&
                         TakeStorySceneNpcList(sessionId, 1500, npcIds);
    if (gotList && !npcIds.empty()) {
        UpdateTaskZoneUi(L"任务区：上报场景 NPC 状态（" +
                             std::to_wstring(npcIds.size()) + L" 个）", true);
        for (uint32_t npcId : npcIds) {
            if (!g_storyTaskRunning.load() || g_storyTaskSession.load() != sessionId) {
                return;
            }
            SendStoryNpcStatusPacket(npcId);
        }
        return;
    }

    const uint32_t stageNpc =
        FindStoryStageNpc(g_storyTaskCurrentSubtask.load(), sceneId);
    if (stageNpc != 0) {
        UpdateTaskZoneUi(L"任务区：向 " + std::to_wstring(stageNpc) + L" 报到", true);
        SendStoryNpcStatusPacket(stageNpc);
    }
}

// 切换到该节点指定的场景并等待进图回包。
// AS3 的 desc 可带 targetScene（200100304 的「等……等一下！」带 targetScene="2002"），
// 玩家推进这句对白后 TaskDialog.dialogComplete 会带着 sceneId 派发 DIALOGFINISHED，
// TaskControl.onDialogFinished 随即 toOtherScene(2002)。抓包对应「回 TASK_TALK
// (21204,[200100304]) → ENTER_SCENE(2002)」，少这一步就会一直卡在长安城城墙边。
static bool SwitchStoryScene(unsigned long long sessionId, uint32_t sceneId) {
    if (sceneId == 0) {
        return true;
    }
    if (g_storyTaskSession.load() != sessionId) {
        return false;
    }
    // 已经在该场景里就不用再切（同一场景连着跑多个子任务时很常见）。
    if (g_storyTaskCurrentScene.load() == sceneId) {
        return true;
    }
    std::wstring mapName = GetMapName(static_cast<int>(sceneId));
    if (mapName.empty()) {
        mapName = std::to_wstring(sceneId);
    }
    UpdateStoryTaskUi(L"任务区：切换到 " + mapName, true);
    g_storyTaskMapEntered = false;
    if (!SendEnterScenePacket(static_cast<int>(sceneId)) ||
        !WaitForStoryMapEnter(sessionId, 8000)) {
        UpdateTaskZoneUi(L"任务区：切换场景失败 " + mapName, false);
        return false;
    }
    g_storyTaskCurrentScene.store(sceneId);
    Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);
    // 进图后先拉场景 NPC 列表，再逐个上报状态：服务端据此才继续下发下一句对白。
    ReportStorySceneNpcStatus(sessionId, sceneId);
    return true;
}

// 施法射击场景对象：AS3 MapControl.onAction:231
//   SEND_ACTION.send=1184784, params=1,
//   body=[userId, actionId, destX, destY, flag=1, destId]
// 玩家侧的操作是「先选法术光标（CursorTool20<special>），再点对象（actionshoot）」，
// 对象只在自己 special 与光标编号一致时才响应（MultipleNPC.onUseAction:1112）。
// 因此 actionId 必须等于目标的 special。
// 纯点击对这类对象完全无效：日志里 2001005 点大树 21101 后 12 秒零回包，
// 就是因为封印（21108）要的是施法而不是点击。
static bool SendStoryActionPacket(uint32_t actionId, uint32_t destX, uint32_t destY,
                                  uint32_t destId) {
    if (actionId == 0) {
        return false;
    }
    const uint32_t userId = g_userId.load();
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::SEND_ACTION_SEND)
        .SetParams(1)
        .WriteUInt32(userId)
        .WriteUInt32(actionId)
        .WriteUInt32(destX)
        .WriteUInt32(destY)
        .WriteUInt32(1)  // flag：普通场景动作
        .WriteUInt32(destId)
        .Build();
    if (!SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()))) {
        return false;
    }
    Sleep(EIGHT_TRIGRAMS_CLICK_DELAY_MS);
    return true;
}

// 对提示里点名的场景对象施法射击（「用【粒子光线】射击【封印】」类节点）。
// 任务区轮询节奏：服务端回包通常几百毫秒内到，100ms 够细又不刷屏。
constexpr DWORD STORY_DIALOG_POLL_STEP_MS = 100;
// 施法（SEND_ACTION）之后等对白的时间：抓包里施法不会直接推对白，通常要点一下 NPC。
constexpr DWORD STORY_SHOOT_DIALOG_WAIT_MS = 1200;
// 逐个点提示对象时，每个对象最多等这么久（点对了 1 秒内就回）。
constexpr DWORD STORY_ALERT_TARGET_WAIT_MS = 1000;
// 拉「已学法术列表」的超时；只在配方没给出法术 id 时才走到。
constexpr DWORD STORY_MAGIC_INDEX_TIMEOUT_MS = 2000;
// 残留对白清理上限：进子任务前最多连清 4 个，动作之间最多连清 3 个。
constexpr int STORY_DIALOG_DRAIN_LIMIT = 4;
constexpr int STORY_DIALOG_DRAIN_BETWEEN_ACTIONS = 3;

// 等到服务端推出下一个对白节点；返回 true 表示队列里已经有一个待回应的节点。
static bool WaitStoryDialog(unsigned long long sessionId, DWORD timeoutMs) {
    const DWORD startTick = GetTickCount();
    while (GetTickCount() - startTick < timeoutMs) {
        if (!g_storyTaskRunning.load() || g_storyTaskSession.load() != sessionId) {
            return false;
        }
        if (HasStoryPendingDialog()) {
            return true;
        }
        Sleep(STORY_DIALOG_POLL_STEP_MS);
    }
    return HasStoryPendingDialog();
}

// 施法用的法术 id：配方里直接给出（由抓包核实的法术名→actionId 生成），
// 只有配方没算出来时才回头拉「已学法术列表」。
static uint32_t ResolveStoryMagic(unsigned long long sessionId, uint32_t magic) {
    if (magic != 0) {
        return magic;
    }
    EnsureStoryMagicList(sessionId, STORY_MAGIC_INDEX_TIMEOUT_MS);
    return FirstStoryMagicId();
}

// 施法类提示（「快点对着【五指山顶】的【五行阵】使用【魔晶射线】」）：
// SEND_ACTION 是唯一推进手段，点任何对象都不算数。
// 抓包 200200408：SEND_ACTION(params=1, body=[userId,2,449,311,1,0]) 之后客户端立刻
// CLICK_NPC(21509)，服务端才推 200200409；2001005 也一样（射击 → 点 21108 → 200100505）。
// 所以这里发完法术后，如果 1.2 秒内没有新对白，就补点一下当前对白 NPC。
static bool ShootStoryAlertTarget(unsigned long long sessionId,
                                  const StoryTaskRecipe::Node& node, uint32_t npcId) {
    if (g_storyTaskSession.load() != sessionId) {
        return false;
    }
    const uint32_t magic = ResolveStoryMagic(sessionId, node.shootSpecial);
    if (magic == 0) {
        UpdateTaskZoneUi(L"任务区：取不到法术，射击节点 " + std::to_wstring(node.dialogId) +
                             L" 跳过",
                         false);
        return false;
    }
    UpdateTaskZoneUi(L"任务区：施放法术 " + std::to_wstring(magic) + L" 于 " +
                         std::to_wstring(node.shootX) + L"," +
                         std::to_wstring(node.shootY),
                     true);
    SendStoryActionPacket(magic, node.shootX, node.shootY, node.shootNpc);
    if (WaitStoryDialog(sessionId, STORY_SHOOT_DIALOG_WAIT_MS)) {
        return true;
    }
    if (npcId != 0) {
        UpdateTaskZoneUi(L"任务区：施法后补点 " + std::to_wstring(npcId), true);
        SendTaskZoneClickPacket(npcId, false);
        Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);
    }
    return true;
}

// 收到 alert 节点时按配方里 alert 正文点名的场景对象补一次点击。
// AS3 的 alert 只展示提示、自己不发包（TaskView.showTaskAlert → TaskAlert.show），
// 但提示的内容就是叫玩家去点某个对象（200100309「接下来就听听眼前这位【唐太宗】」）。
// 抓包证实玩家点完后服务端才继续下发；没有可点对象（alertNpc==0）时才是纯提示。
static bool HasStoryAlertTargets(uint32_t dialogId) {
    for (uint32_t i = 0; i < StoryTaskRecipe::ALERT_TARGET_COUNT; ++i) {
        if (StoryTaskRecipe::ALERT_TARGETS[i].dialogId == dialogId &&
            StoryTaskRecipe::ALERT_TARGETS[i].npc != 0) {
            return true;
        }
    }
    return false;
}

// 提示里点到的全部场景对象逐个试。收集类提示的目标有好几个、还分散在多个场景
// （抓包 200200302「在双叉岭、大唐、五指山找到四页经书残页」：2001 的经书残页一
// 21205、2004 的二/三 21403/21404、2003 的四 21104），点错完全静默，
// 所以谁有回包就停在谁身上。先试当前场景的对象，再按表里的顺序切图。
static bool RunStoryAlertTargets(unsigned long long sessionId, uint32_t dialogId) {
    // 严格按配方表顺序试（表内已按「点名的具体程度」排过：同一系列收集物在前，
    // 正文里顺带提到的角色名在后），必要就切图，不为了省一次切图去打乱这个顺序。
    for (uint32_t i = 0; i < StoryTaskRecipe::ALERT_TARGET_COUNT; ++i) {
        const StoryTaskRecipe::AlertTarget& target =
            StoryTaskRecipe::ALERT_TARGETS[i];
        if (target.dialogId != dialogId || target.npc == 0) {
            continue;
        }
        if (!g_storyTaskRunning.load() || g_storyTaskSession.load() != sessionId) {
            return false;
        }
        const bool sameScene =
            target.scene == 0 || g_storyTaskCurrentScene.load() == target.scene;
        if (!sameScene) {
            if (!SwitchStoryScene(sessionId, target.scene)) {
                continue;
            }
            // 进图先报到：抓包里的猴子对白 200200303 就是 NPC_STATUS(21406) 推出来的。
            ReportStorySceneNpcStatus(sessionId, target.scene);
            Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);
            if (HasStoryPendingDialog()) {
                return true;
            }
        }
        UpdateTaskZoneUi(L"任务区：点击提示对象 " + std::to_wstring(target.npc), true);
        if (!SendTaskZoneClickPacket(target.npc, false)) {
            continue;
        }
        // 点对了通常 1 秒内就回包（经书残页回的是 6 个 int 的拾取对白）。
        if (WaitStoryDialog(sessionId, STORY_ALERT_TARGET_WAIT_MS)) {
            return true;
        }
    }
    return false;
}

static bool FollowStoryAlertNpc(unsigned long long sessionId,
                                const StoryTaskRecipe::Node& node, uint32_t npcId) {
    if (g_storyTaskSession.load() != sessionId) {
        return false;
    }
    // 需要施法的提示（「用【粒子光线】射击【封印】」「使用【魔晶射线】」）：点击无效，
    // 必须发 SEND_ACTION。shootSpecial 非 0 表示只认法术、没有具体目标对象。
    if (node.shootNpc != 0 || node.shootSpecial != 0) {
        return ShootStoryAlertTarget(sessionId, node, npcId);
    }
    // 多目标提示（收集类）：配方表里列了这个节点的目标就按表逐个点。
    if (HasStoryAlertTargets(node.dialogId)) {
        return RunStoryAlertTargets(sessionId, node.dialogId);
    }
    // 目标在另一个场景（200100308「长安城的【城门】那里有人」→ 2001 的城门口 21202，
    // 而此刻已随 200100304 进了 2002）：先切回去再点，否则点了也没人应答。
    if (node.alertScene != 0 && g_storyTaskCurrentScene.load() != node.alertScene) {
        if (!SwitchStoryScene(sessionId, node.alertScene)) {
            return false;
        }
    }
    if (node.alertNpc == 0) {
        UpdateTaskZoneUi(L"任务区：提示节点 " + std::to_wstring(node.dialogId), true);
        return true;
    }
    UpdateTaskZoneUi(L"任务区：点击提示对象 " + std::to_wstring(node.alertNpc), true);
    if (!SendTaskZoneClickPacket(node.alertNpc, false)) {
        UpdateTaskZoneUi(L"任务区：点击提示对象失败", false);
        return false;
    }
    return true;
}

// 已学法术 id（GETMAGICINDEX_BACK 下发）。
// 当前主线射击节点的 actionId 直接取目标 special（见 SendStoryActionPacket 注释），
// 这份列表用于回包可核对，不要用来替换 special 判定。
static void RememberStoryLearnedMagicIds(const std::vector<uint32_t>& magicIds) {
    std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
    g_storyTaskState.learnedMagicIds = magicIds;
    g_storyTaskState.magicIndexLoaded = true;
}

// 按节点类型回应服务端下发的 dialogId。
// 剧情战斗驱动（普通战斗，非万妖盛会/非双台谷）。
// 抓包 2001007（点虎魔王影子开战，battle id=10002）的完整链路：
//   CLICK_NPC(0x20002712) → BATTLE_START(1317120) → BATTLE_READY(1186049)
//   → BATTLE_BUFS(1317131) → BATTLE_ROUND_START(1317121) → USER_OP(1186050,[目标sid,技能id])
//   → BATTLE_ROUND(1317122) → BATTLE_PLAY_OVER(1186056) → BATTLE_END(1317125)
//   → BATTLE_NEWEXP(1317130) → BATTLE_END_SEND(1186323)
//   → TASK_TALK(21502,200100705) → TASK_CURRENT + TASK_FINISHED(2001007)。
// 少走任何一步服务端都不往下推，子任务就卡在这场战斗里——「你逃不掉的」跑不完正是这个原因。
static void SendStoryBattleUserOpPacket() {
    const BattleData snapshot = PacketParser::GetCurrentBattleSnapshot();
    int32_t targetSid = 2;
    const auto hasOther =
        snapshot.otherActiveIndex >= 0 &&
        snapshot.otherActiveIndex < static_cast<int32_t>(snapshot.otherPets.size());
    if (hasOther) {
        const int32_t sid = snapshot.otherPets[snapshot.otherActiveIndex].sid;
        if (sid > 0) {
            targetSid = sid;
        }
    }
    if (targetSid == 2) {
        for (const auto& pet : snapshot.otherPets) {
            if (pet.sid > 0) {
                targetSid = pet.sid;
                break;
            }
        }
    }

    // 取技能：主宠「有 PP 里威力最高」→ 主宠任意技能 → 友方其它宠物。
    // 快照里技能数组偶尔为空（换宠、解析不全），所以必须留兜底，否则回合一发不出去就整场卡死。
    const auto pickSkillFromPet = [](const BattleEntity& pet) -> int32_t {
        int32_t best = 0;
        int bestPower = -1;
        for (const auto& skill : pet.skills) {
            if (skill.id == 0 || skill.pp <= 0) {
                continue;
            }
            const int power = GetSkillPower(static_cast<int>(skill.id), 0);
            if (best == 0 || power > bestPower) {
                best = static_cast<int32_t>(skill.id);
                bestPower = power;
            }
        }
        if (best != 0) {
            return best;
        }
        for (const auto& skill : pet.skills) {
            if (skill.id != 0) {
                return static_cast<int32_t>(skill.id);
            }
        }
        return 0;
    };

    int32_t skillId = 0;
    if (snapshot.myActiveIndex >= 0 &&
        snapshot.myActiveIndex < static_cast<int32_t>(snapshot.myPets.size())) {
        skillId = pickSkillFromPet(snapshot.myPets[snapshot.myActiveIndex]);
    }
    for (size_t i = 0; skillId == 0 && i < snapshot.myPets.size(); ++i) {
        skillId = pickSkillFromPet(snapshot.myPets[i]);
    }

    if (skillId == 0) {
        UpdateTaskZoneUi(L"任务区：战斗里取不到可用技能，等下一回合重试", false);
        return;
    }

    // params=0 是技能攻击，body=[目标 sid, 技能 id]（抓包 #22 的 [2, 2259]）。
    auto packet = PacketBuilder()
        .SetOpcode(Opcode::USER_OP_SEND)
        .SetParams(0)
        .WriteInt32(targetSid)
        .WriteInt32(skillId)
        .Build();
    SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()));
}

struct StoryBattleFinishData {
    unsigned long long sessionId;
    uint32_t dialogId;
    uint32_t npcId;
};

// 战斗收尾：先回 BATTLE_END_SEND，再把这场战斗对应的任务节点重新回一次。
// 抓包 #25 收到 BATTLE_END、#26-28 收经验包、#30 才发 END_SEND，#33 才回 705。
// END_SEND 无论任务是否已停都要发：服务端还在等它，不回就会把角色卡在战斗里。
static void FinishStoryBattle(unsigned long long sessionId, uint32_t dialogId,
                              uint32_t npcId) {
    auto* data = new StoryBattleFinishData{sessionId, dialogId, npcId};
    HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
        std::unique_ptr<StoryBattleFinishData> d(
            static_cast<StoryBattleFinishData*>(lpParam));
        Sleep(500);
        SendNormalBattleEndPacket();
        // 只有任务还在同一个会话里跑，这个节点才值得补回；停止或换会话都不补。
        if (d->dialogId != 0 && d->npcId != 0 && g_storyTaskRunning.load() &&
            g_storyTaskSession.load() == d->sessionId) {
            Sleep(200);
            SendTaskZoneTalkPacket(d->npcId, d->dialogId);
        }
        return 0;
    }, data, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete data;
    }
}

void HandleStoryBattlePacket(const GamePacket& packet) {
    // 万妖盛会 / 双台谷自带回合驱动，剧情战斗不去抢。
    if (g_battleSixAuto.IsInBattle() || g_battleSixAuto.IsAutoMatching() ||
        g_shuangtaiAuto.IsRunning()) {
        return;
    }
    // BATTLE_END 一直不来（断线、被踢出战斗）时兜底解除接管，别让状态永久挂着。
    if (g_storyBattleActive.load() &&
        GetTickCount() - g_storyBattleActiveTick.load() > 180000) {
        g_storyBattleActive.store(false);
    }

    switch (packet.opcode) {
        case Opcode::BATTLE_START: {
            // 只接管「任务战斗节点点出来的那一场」：没有待收尾的节点就不插手别人的战斗。
            if (!g_storyTaskRunning.load() || g_storyBattleDialogId.load() == 0) {
                break;
            }
            // 点完战斗节点很久都没开战，这套待收尾状态已经过期，别再让它接管别的战斗。
            if (GetTickCount() - g_storyBattleArmTick.load() > 15000) {
                g_storyBattleDialogId.store(0);
                g_storyBattleNpcId.store(0);
                break;
            }
            g_storyBattleActive.store(true);
            g_storyBattleActiveTick.store(GetTickCount());
            // AS3 BattleControl.onSpiritEnterOver：进战斗先回 READY，服务端才会推回合。
            SendBattleReadyPacket();
            break;
        }
        case Opcode::BATTLE_ROUND_START:
        case Opcode::BATTLE_CHANGE_SPIRIT_ROUND: {
            // 已经开打就一律把回合打完：中途停手会让服务端既不结算也不结束。
            if (g_storyBattleActive.load()) {
                SendStoryBattleUserOpPacket();
            }
            break;
        }
        case Opcode::BATTLE_ROUND: {
            // AS3 onBattlePlayOver：一回合演出结束要确认一次，否则服务端不推下一回合。
            if (g_storyBattleActive.load()) {
                SendBattlePlayOverPacket();
            }
            break;
        }
        case Opcode::BATTLE_END: {
            // 这里不看 g_storyTaskRunning：停止任务后到达的 BATTLE_END 也要收尾。
            if (!g_storyBattleActive.exchange(false)) {
                break;
            }
            const uint32_t dialogId = g_storyBattleDialogId.exchange(0);
            const uint32_t npcId = g_storyBattleNpcId.exchange(0);
            g_storyBattleArmTick.store(0);
            UpdateTaskZoneUi(L"任务区：剧情战斗结束，继续任务流程", true);
            FinishStoryBattle(g_storyTaskSession.load(), dialogId, npcId);
            break;
        }
        default:
            break;
    }
}

static bool ReplyStoryDialogNode(unsigned long long sessionId, uint32_t dialogId,
                                 uint32_t npcId, bool forceReply) {
    if (g_storyTaskSession.load() != sessionId) {
        return false;
    }

    const StoryTaskRecipe::Node* node = FindStoryNode(dialogId);

    // alert 是纯提示节点（TaskXMLParser 解析成 alert，由 TaskAlert/TaskTips 展示）。
    // 普通 alert 不发包；但提示里点名的场景对象要补点一次，否则玩家侧流程停住。
    // forceReply 例外：服务端在回包里给了 flag（type 2/14/17），说明关掉提示后要回
    // TASK_TALK(flag,[dialogId])，此时按普通对白处理。
    if (!forceReply && node != nullptr && node->kind == StoryTaskRecipe::KIND_ALERT) {
        return FollowStoryAlertNpc(sessionId, *node, npcId);
    }

    // 配方表里没有的节点（例如临时/动态节点）：当作普通对白回应，保持流程前进。
    if (node == nullptr) {
        UpdateTaskZoneUi(L"任务区：回应未登记对白 " + std::to_wstring(dialogId), true);
        return SendTaskZoneTalkPacket(npcId, dialogId) != FALSE;
    }

    switch (node->kind) {
        case StoryTaskRecipe::KIND_BATTLE: {
            // 同一个战斗节点被重复回应（重复点击把它又排队了一次）时只开一次战，
            // 否则会连开两场，第二场没有节点可收尾。
            if (g_storyBattleActive.load() && g_storyBattleDialogId.load() == dialogId) {
                UpdateTaskZoneUi(L"任务区：战斗已在进行，忽略重复的战斗节点 " +
                                     std::to_wstring(dialogId),
                                 true);
                return true;
            }
            const uint32_t sid = BuildStoryBattleSid(*node, npcId);
            if (sid == 0) {
                break;
            }
            // 战斗由服务端在点击后自行结算；这里只发点击，随后等待完成通知。
            // 记下这条节点：打完仗还要再回一次 TASK_TALK，服务端才判定该子任务完成。
            g_storyBattleDialogId.store(dialogId);
            g_storyBattleNpcId.store(npcId);
            g_storyBattleArmTick.store(GetTickCount());
            UpdateTaskZoneUi(L"任务区：发起任务战斗 " + std::to_wstring(node->battleId), true);
            return SendBattlePacket(sid) != FALSE;
        }
        default:
            // 含 <choose> 的节点也走这里：AS3 TaskDialog.onClickPersonSay 对 flag 1/2/3
            // 是本地分支（flag=1 继续、flag=2 直接关窗），都不发 TRAIN_INFO；
            // 只有服务端回包带 chooseId 时才发，本流程拿不到该字段，故只回 TASK_TALK。
            break;
    }

    if (!SendTaskZoneTalkPacket(npcId, dialogId)) {
        return false;
    }
    // 推进这句对白后按节点自带的 targetScene 切场景（AS3 DIALOGFINISHED 的后续动作）。
    return SwitchStoryScene(sessionId, node->targetScene);
}

// 已学法术列表（GET_MAGIC_INDEX_BACK）必要时拉一次。
// 抓包：客户端先发 GET_MAGIC_INDEX(1184797, params=0, 空 body)，服务端按 params=magicCount
// 回 6 组 [id, type, state, num]（1,3,26,29,36,19）。射击用的 actionId 只能从这里取。
static bool EnsureStoryMagicList(unsigned long long sessionId, DWORD timeoutMs) {
    {
        std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
        if (g_storyTaskState.magicIndexLoaded) {
            return true;
        }
    }
    auto packet = PacketBuilder().SetOpcode(Opcode::GET_MAGIC_INDEX_SEND).SetParams(0).Build();
    if (!SendPacket(0, packet.data(), static_cast<DWORD>(packet.size()),
                    Opcode::GET_MAGIC_INDEX_BACK, timeoutMs)) {
        return false;
    }
    const DWORD startTick = GetTickCount();
    while (g_storyTaskRunning.load() && g_storyTaskSession.load() == sessionId) {
        {
            std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
            if (g_storyTaskState.magicIndexLoaded) {
                return true;
            }
        }
        if (GetTickCount() - startTick >= timeoutMs) {
            break;
        }
        Sleep(50);
    }
    return false;
}

static uint32_t FirstStoryMagicId() {
    std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
    return g_storyTaskState.learnedMagicIds.empty()
               ? 0
               : g_storyTaskState.learnedMagicIds.front();
}

// 回掉一个已挂起的对话节点（前置动作之间用，保证下一步是在服务端推进后的状态上发出）。
static bool ReplyOnePendingStoryDialog(unsigned long long sessionId, DWORD waitMs) {
    uint32_t dialogId = 0;
    uint32_t npcId = 0;
    bool forceReply = false;
    if (!TakeStoryPendingDialog(sessionId, dialogId, npcId, waitMs, &forceReply)) {
        return false;
    }
    UpdateTaskZoneUi(L"任务区：前置动作推进对话 " + std::to_wstring(dialogId), true);
    ReplyStoryDialogNode(sessionId, dialogId, npcId, forceReply);
    Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);
    return true;
}

// 把已经挂起的对话就地回掉（前置动作前后的清场），最多处理 limit 个。
static void DrainStoryDialogs(unsigned long long sessionId, int limit) {
    for (int i = 0; i < limit && HasStoryPendingDialog(); ++i) {
        if (!ReplyOnePendingStoryDialog(sessionId, 0)) {
            return;
        }
    }
}

// 执行子任务级前置动作（配方 PRE_ACTIONS）。
// 依据抓包：alert 节点只由客户端本地展示，服务端从头到尾不下发（200100504 是当时的
// 待执行步，全包零下发），所以「射击封印」「点木牌」这类动作必须进子任务后主动做完，
// 不能挂在「收到该 dialogId 回包」上——那是永远走不到的死分支。
// 动作之间若引出对话就当场回掉：抓包 2001005 是「射击 → 服务端推 200100505」，
// 后面的动作要基于推进后的状态发，否则顺序会互相吞掉。
static bool RunStoryPreActions(unsigned long long sessionId, uint32_t subtaskId,
                               uint32_t scene) {
    // 先清掉上个子任务可能残留的对话节点，否则后面的「已挂着对话 = 本步已推进」判断会误判。
    DrainStoryDialogs(sessionId, STORY_DIALOG_DRAIN_LIMIT);
    if (scene == 0) {
        return false;
    }
    bool acted = false;
    for (uint32_t i = 0; i < StoryTaskRecipe::PRE_ACTION_COUNT; ++i) {
        const StoryTaskRecipe::PreAction& action = StoryTaskRecipe::PRE_ACTIONS[i];
        if (action.subtaskId != subtaskId || action.scene != scene) {
            continue;
        }
        if (!g_storyTaskRunning.load() || g_storyTaskSession.load() != sessionId) {
            return false;
        }
        // 上一个动作引出的对话先回掉，再发下一个动作。
        DrainStoryDialogs(sessionId, STORY_DIALOG_DRAIN_BETWEEN_ACTIONS);
        if (!g_storyTaskRunning.load() || g_storyTaskSession.load() != sessionId) {
            return false;
        }
        if (action.kind == StoryTaskRecipe::PRE_SHOOT) {
            const uint32_t magic = ResolveStoryMagic(sessionId, action.magic);
            if (!g_storyTaskRunning.load() || g_storyTaskSession.load() != sessionId) {
                return false;
            }
            if (magic == 0) {
                // 拿不到法术列表就退化为点击，至少保留一次触发机会。
                UpdateTaskZoneUi(L"任务区：射击目标 " + std::to_wstring(action.npc) +
                                     L"（未取到法术，改点击）",
                                 true);
                if (!SendTaskZoneClickPacket(action.npc, false)) {
                    return false;
                }
            } else {
                UpdateTaskZoneUi(L"任务区：施法射击 " + std::to_wstring(action.npc) +
                                     L"（法术 " + std::to_wstring(magic) + L"）",
                                 true);
                SendStoryActionPacket(magic, action.x, action.y, action.npc);
            }
        } else {
            UpdateTaskZoneUi(L"任务区：点击任务对象 " + std::to_wstring(action.npc), true);
            if (!SendTaskZoneClickPacket(action.npc, false)) {
                return false;
            }
        }
        acted = true;
        Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);
    }
    return acted;
}

// 执行单个子任务：进图 -> 点入口 NPC -> 服务端对话流 -> 等完成通知。
static bool RunStorySubtask(unsigned long long sessionId, const StoryTaskRecipe::Subtask& subtask) {
    if (g_storyTaskSession.load() != sessionId) {
        return false;
    }

    // 上一场剧情战斗没打完就进了新子任务时，不能清「正在战斗」标记：
    // 那一场的 BATTLE_END 还要靠它发 BATTLE_END_SEND，否则角色会卡在战斗里。
    if (!g_storyBattleActive.load()) {
        g_storyBattleDialogId.store(0);
        g_storyBattleNpcId.store(0);
        g_storyBattleArmTick.store(0);
    }

    std::wstring mapName = GetMapName(static_cast<int>(subtask.scene));
    if (mapName.empty()) {
        mapName = std::to_wstring(subtask.scene);
    }
    UpdateStoryTaskUi(L"任务区：进入 " + mapName + L" 执行子任务 " +
                          std::to_wstring(subtask.subtaskId), true);

    if (subtask.scene != 0 && g_storyTaskCurrentScene.load() != subtask.scene) {
        g_storyTaskMapEntered = false;
        if (!SendEnterScenePacket(static_cast<int>(subtask.scene)) ||
            !WaitForStoryMapEnter(sessionId, 8000)) {
            UpdateStoryTaskUi(L"任务区：进入场景失败 " + mapName, false);
            return false;
        }
        g_storyTaskCurrentScene.store(subtask.scene);
        Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);
        // 与真实客户端一致：进图后先拉场景 NPC 列表，再逐个上报状态。
        // 服务端要等这些上报才继续推该子任务的对白（抓包 21319 就是动态任务 NPC）。
        ReportStorySceneNpcStatus(sessionId, subtask.scene);
    }

    // alert 类动作步（射击封印、点木牌）服务端从不主动下发，必须进图后主动做完，
    // 否则该子任务永远卡在这一步（抓包：200100504 全包零下发）。
    RunStoryPreActions(sessionId, subtask.subtaskId,
                       subtask.scene != 0 ? subtask.scene
                                          : g_storyTaskCurrentScene.load());
    // 点入口对象：服务端只对「当前子任务真正的触发对象」回包，点错完全静默
    // （抓包：2001003 点皇榜 21203 立刻回包；日志：2001005 点大树 21101 零回包）。
    // 所以按配方候选逐个试，收到任意任务回包（对话/可接/完成）就停在该对象上。
    std::vector<uint32_t> entryCandidates =
        GetStoryEntryCandidates(subtask.subtaskId, subtask.scene);
    if (entryCandidates.empty() && subtask.entryNpc != 0) {
        entryCandidates.push_back(subtask.entryNpc);
    }
    // 前置动作已经把对话推出来了就不要再点入口：抓包里 2001005 是先射击、
    // 再收到服务端推 200100505 的，此时再点任何入口都是多余动作（会把同一个
    // 对话再排队一次）。没有新对话就照旧逐个试入口，射击失手时还能靠点击兜底
    // ——抓包 #7 CLICK_NPC(21108) 确实能直接引出 200100505。
    if (!HasStoryPendingDialog() && !entryCandidates.empty()) {
        uint32_t clickedNpc = 0;
        for (size_t i = 0; i < entryCandidates.size(); ++i) {
            if (!g_storyTaskRunning.load() || g_storyTaskSession.load() != sessionId) {
                return false;
            }
            const uint32_t npcId = entryCandidates[i];
            if (!SendTaskZoneClickPacket(npcId, false)) {
                UpdateTaskZoneUi(L"任务区：点击入口 NPC 失败", false);
                return false;
            }
            clickedNpc = npcId;

            // 主线任务多以「接任务」开场：服务端用 NPCTASKLIST(type=1) 给出可接列表，
            // 不补发 ACCEPT_TASK 就停在接任务窗口，后面所有对话都不会来。
            // 两个退出条件：拿到可接任务立刻发；或服务端直接给了对话节点（本步不用接任务）。
            const DWORD acceptWaitMs = 2500;
            const DWORD acceptDeadline = GetTickCount() + acceptWaitMs;
            bool accepted = false;
            bool responded = false;
            while (g_storyTaskRunning.load() && g_storyTaskSession.load() == sessionId &&
                   GetTickCount() < acceptDeadline) {
                uint32_t acceptId = 0;
                if (!TakeStoryPendingAcceptSubtask(acceptId)) {
                    if (HasStoryPendingDialog()) {
                        responded = true;
                        break;
                    }
                    Sleep(100);
                    continue;
                }
                UpdateStoryTaskUi(L"任务区：接受子任务 " + std::to_wstring(acceptId), true);
                if (!SendStoryAcceptTaskPacket(acceptId)) {
                    UpdateTaskZoneUi(L"任务区：接任务发包失败", false);
                    return false;
                }
                Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);
                // 接完任务要重新点一次 NPC 才会下发给这个子任务的首个对话节点。
                SendTaskZoneClickPacket(npcId, false);
                accepted = true;
                responded = true;
                break;
            }
            if (responded) {
                if (i > 0) {
                    UpdateTaskZoneUi(L"任务区：入口对象 " + std::to_wstring(npcId) +
                                       L" 有回应（第 " + std::to_wstring(i + 1) + L" 个候选）",
                                   true);
                }
                break;
            }
            if (accepted) {
                break;
            }
            UpdateTaskZoneUi(L"任务区：入口对象 " + std::to_wstring(npcId) +
                               L" 无回应，试下一个",
                           true);
        }
        if (clickedNpc == 0) {
            UpdateTaskZoneUi(L"任务区：没有可尝试的入口对象", false);
        }
    }

    // 服务端对话流：每收到一个 dialogId 就回应一次，直到完成通知或长时间无对话。
    const DWORD dialogWaitMs = 8000;
    // 收集类子任务要跨 3～4 个场景逐个点目标对象，单点静默最多等 1 秒，
    // 60 秒不够用（2002003 失落的经文实测 8 个目标 + 切图 ≈ 50 秒）。
    const DWORD subtaskTimeoutMs = 90000;
    const DWORD subtaskStart = GetTickCount();
    uint32_t answeredCount = 0;
    // 最近一个对白的 NPC：服务端静默时用它补点一次（见下面的 retryClickCount）。
    uint32_t lastTalkNpc = subtask.entryNpc;
    int retryClickCount = 0;

    while (g_storyTaskRunning.load() && g_storyTaskSession.load() == sessionId) {
        if (IsStorySubtaskFinishedInCache(subtask.subtaskId)) {
            return true;
        }
        // 战斗最长豁免 3 个超时周期（约 180 秒）：既够打完一场戏，又不会在战斗卡死时无限等。
        const DWORD elapsedMs = GetTickCount() - subtaskStart;
        const bool battleRunning =
            g_storyBattleActive.load() && elapsedMs < subtaskTimeoutMs * 3;
        if (elapsedMs >= subtaskTimeoutMs && !battleRunning) {
            UpdateTaskZoneUi(L"任务区：子任务超时 " + std::to_wstring(subtask.subtaskId), false);
            return IsStorySubtaskFinishedInCache(subtask.subtaskId);
        }

        uint32_t dialogId = 0;
        uint32_t npcId = subtask.entryNpc;
        bool forceReply = false;
        // 战斗期间服务端只在回合里回包，对话会停很久，等待时间要放开。
        const DWORD dialogWait = battleRunning ? 3000 : dialogWaitMs;
        if (!TakeStoryPendingDialog(sessionId, dialogId, npcId, dialogWait, &forceReply)) {
            if (battleRunning) {
                continue;  // 仗还没打完，继续等
            }
            // 一段时间没有新对话：可能服务端已结算，收尾确认一次完成状态。
            if (IsStorySubtaskFinishedInCache(subtask.subtaskId)) {
                return true;
            }
            // 也有一部分对白要靠「再点一次 NPC」才会下发：抓包里 2001007 回完 703
            // 服务端就静默，玩家再点一次虎魔王才拿到 704。所以先补点再判定结束。
            if (retryClickCount < 2 && lastTalkNpc != 0) {
                ++retryClickCount;
                UpdateTaskZoneUi(L"任务区：没有新对白，补点 " +
                                     std::to_wstring(lastTalkNpc),
                                 true);
                SendTaskZoneClickPacket(lastTalkNpc, false);
                Sleep(EIGHT_TRIGRAMS_RESULT_DELAY_MS);
                continue;
            }
            UpdateTaskZoneUi(L"任务区：对话流结束 " + std::to_wstring(subtask.subtaskId), true);
            return false;
        }

        g_storyTaskCurrentDialog.store(dialogId);
        ++answeredCount;
        lastTalkNpc = npcId == 0 ? subtask.entryNpc : npcId;
        retryClickCount = 0;
        if (!ReplyStoryDialogNode(sessionId, dialogId, npcId == 0 ? subtask.entryNpc : npcId,
                                  forceReply)) {
            UpdateTaskZoneUi(L"任务区：回应对话失败 " + std::to_wstring(dialogId), false);
            return false;
        }
    }

    return false;
}

static DWORD WINAPI StoryTaskThreadProc(LPVOID lpParam) {
    std::unique_ptr<EightTrigramsTaskData> taskData(static_cast<EightTrigramsTaskData*>(lpParam));
    const unsigned long long sessionId = taskData->sessionId;
    bool flowCompleted = false;
    bool flowFailed = false;

    ResetStoryTaskState();
    g_storyTaskMapEntered = false;
    g_storyTaskCurrentScene.store(0);

    UpdateStoryTaskUi(L"任务区：读取主线任务进度...", true);
    // 进度按任务组查询（USER_TASK_LIST 一次只回一个组的已接/已完成列表），
    // 配方表覆盖多个任务组时逐个查并累积到本地完成集合。
    {
        std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
        g_storyTaskState.acceptedSubtaskIds.clear();
        g_storyTaskState.finishedSubtaskIds.clear();
    }
    uint32_t lastQueriedTaskId = 0;
    for (uint32_t i = 0; i < StoryTaskRecipe::SUBTASK_COUNT; ++i) {
        const uint32_t taskId = StoryTaskRecipe::SUBTASKS[i].taskId;
        if (taskId == 0 || taskId == lastQueriedTaskId) {
            continue;
        }
        lastQueriedTaskId = taskId;
        if (!g_storyTaskRunning.load() || g_storyTaskSession.load() != sessionId) {
            return 0;
        }
        if (!QueryTaskZoneUserTaskListProgress(taskId)) {
            continue;
        }
        // 先在自己的锁下取出八挂进度快照，再并入主线集合：
        // g_eightTrigramsProgress 的 vector 会被 socket 线程改写，不能无锁读。
        std::vector<uint32_t> acceptedSnapshot;
        std::vector<uint32_t> finishedSnapshot;
        bool listLoaded = false;
        {
            std::lock_guard<std::mutex> progressLock(g_eightTrigramsProgress.mutex);
            acceptedSnapshot = g_eightTrigramsProgress.acceptedSubtaskIds;
            finishedSnapshot = g_eightTrigramsProgress.finishedSubtaskIds;
            listLoaded = g_eightTrigramsProgress.userTaskListLoaded;
        }
        std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
        for (uint32_t id : acceptedSnapshot) {
            if (std::find(g_storyTaskState.acceptedSubtaskIds.begin(),
                          g_storyTaskState.acceptedSubtaskIds.end(), id) ==
                g_storyTaskState.acceptedSubtaskIds.end()) {
                g_storyTaskState.acceptedSubtaskIds.push_back(id);
            }
        }
        for (uint32_t id : finishedSnapshot) {
            if (std::find(g_storyTaskState.finishedSubtaskIds.begin(),
                          g_storyTaskState.finishedSubtaskIds.end(), id) ==
                g_storyTaskState.finishedSubtaskIds.end()) {
                g_storyTaskState.finishedSubtaskIds.push_back(id);
            }
        }
        g_storyTaskState.userTaskListLoaded = listLoaded;
    }
    PushStoryTaskProgressToUi();

    for (uint32_t i = 0; i < StoryTaskRecipe::SUBTASK_COUNT && g_storyTaskRunning.load(); ++i) {
        if (g_storyTaskSession.load() != sessionId) {
            break;
        }
        const StoryTaskRecipe::Subtask& subtask = StoryTaskRecipe::SUBTASKS[i];

        // 跳过未勾选或已完成的子任务。
        if (!IsStorySubtaskSelected(subtask.subtaskId)) {
            continue;
        }
        if (IsStorySubtaskFinishedInCache(subtask.subtaskId)) {
            continue;
        }

        g_storyTaskCurrentSubtask.store(subtask.subtaskId);
        if (!RunStorySubtask(sessionId, subtask)) {
            // 用户已停止或会话已切换：直接退出。否则下面的失败提示会把 UI 刷回
            // 「运行中」，停止按钮重新变可用，用户得再点一次才能真正结束。
            if (!g_storyTaskRunning.load() || g_storyTaskSession.load() != sessionId) {
                break;
            }
            if (IsStorySubtaskFinishedInCache(subtask.subtaskId)) {
                continue;
            }
            // 单步失败：记录后继续下一个，避免整条链断在一个非阻塞子任务上。
            UpdateStoryTaskUi(L"任务区：子任务未确认完成 " + std::to_wstring(subtask.subtaskId), true);
            flowFailed = true;
            continue;
        }
    }

    // 会话号先校验再清运行标记：被 Stop 掉或已被新会话取代的旧线程，
    // 不能把新会话的 g_storyTaskRunning 清零（旧线程可能仍卡在进图/查询等待里）。
    if (g_storyTaskSession.load() != sessionId) {
        return 0;
    }
    const bool stillRunning = g_storyTaskRunning.exchange(false);
    g_storyTaskMapEntered = false;
    if (!flowFailed) {
        flowCompleted = true;
    }

    if (flowCompleted) {
        UpdateStoryTaskUi(L"任务区：主线任务流程已执行完毕", false);
    } else if (stillRunning) {
        UpdateStoryTaskUi(L"任务区：主线任务流程结束（部分子任务未确认完成）", false);
    } else {
        UpdateStoryTaskUi(L"任务区：已停止", false);
    }
    return 0;
}

BOOL StartStoryTaskAsync(const std::vector<uint32_t>& subtaskIds) {
    if (g_storyTaskRunning.load() || g_taskZoneRunning.load()) {
        UpdateStoryTaskUi(L"任务区：已有任务区流程在运行", true);
        return FALSE;
    }

    if (g_battleSixAuto.IsInBattle() || g_battleSixAuto.IsAutoMatching() || g_shuangtaiAuto.IsRunning()) {
        UpdateStoryTaskUi(L"任务区：请先停止其他自动战斗", false);
        return FALSE;
    }

    if (g_LastGameSocket == 0 || g_userId.load() == 0) {
        UpdateStoryTaskUi(L"任务区：请先进入游戏", false);
        return FALSE;
    }

    const unsigned long long sessionId = g_storyTaskSession.fetch_add(1) + 1;
    g_storyTaskRunning = true;
    g_storyTaskMapEntered = false;
    ResetStoryTaskState();
    {
        std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
        g_storyTaskState.selectedSubtaskIds = NormalizeStorySelection(subtaskIds);
    }

    EightTrigramsTaskData* taskData = new EightTrigramsTaskData();
    taskData->sessionId = sessionId;
    HANDLE hThread = CreateThread(nullptr, 0, StoryTaskThreadProc, taskData, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
        return TRUE;
    }

    delete taskData;
    g_storyTaskRunning = false;
    UpdateStoryTaskUi(L"任务区：启动失败", false);
    return FALSE;
}

VOID StopStoryTask() {
    if (!g_storyTaskRunning.load()) {
        UpdateStoryTaskUi(L"任务区：已停止", false);
        return;
    }
    // 先切会话号再清运行标记：否则 worker 可能在「运行标记已清、会话号未变」的
    // 窗口里按会话号匹配成功，走到收尾分支把状态写成「流程已执行完毕」。
    g_storyTaskSession.fetch_add(1);
    g_storyTaskRunning = false;
    g_storyTaskMapEntered = false;
    g_storyTaskCurrentScene.store(0);
    // 战斗中途停止：清掉「待收尾的节点」，但保留 g_storyBattleActive ——
    // 服务端仍在等这一场的 BATTLE_END_SEND，清了会让角色卡在战斗界面。
    g_storyBattleDialogId.store(0);
    g_storyBattleNpcId.store(0);
    g_storyBattleArmTick.store(0);
    ResponseWaiter::CancelWait();
    UpdateStoryTaskUi(L"任务区：已停止", false);
}

void StoryTaskOnDialogResponse(const GamePacket& packet) {
    if (!g_storyTaskRunning.load()) {
        return;
    }
    if (packet.body.size() < 4) {
        return;
    }
    // AS3 TaskControl.opDialogBackType10 的占位值：两条 dialog 里等于它的那条不推送。
    static constexpr uint32_t kDialogPlaceholder = 9999999;

    size_t offset = 0;
    uint32_t npcId = 0;
    bool forceReply = false;

    switch (packet.params) {
        case 1:
        case 5:
        case 22: {
            // npcid, itemCount, (type,id,count)..., dialogId
            if (offset + 4 > packet.body.size()) return;
            npcId = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
            const int itemCount = ReadInt32LE(packet.body.data(), offset);
            for (int i = 0; i < itemCount; ++i) {
                if (offset + 12 > packet.body.size()) return;
                ReadInt32LE(packet.body.data(), offset);
                ReadInt32LE(packet.body.data(), offset);
                ReadInt32LE(packet.body.data(), offset);
            }
            if (offset + 4 > packet.body.size()) return;
            PushStoryPendingDialog(
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset)), npcId,
                packet.params);
            return;
        }
        case 21: {
            // npcid, count, (type,id)..., dialogId（每项 2 个 int，与 case 1/5/22 不同）
            if (offset + 4 > packet.body.size()) return;
            npcId = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
            const int paramCount = ReadInt32LE(packet.body.data(), offset);
            for (int i = 0; i < paramCount; ++i) {
                if (offset + 8 > packet.body.size()) return;
                ReadInt32LE(packet.body.data(), offset);
                ReadInt32LE(packet.body.data(), offset);
            }
            if (offset + 4 > packet.body.size()) return;
            PushStoryPendingDialog(
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset)), npcId,
                packet.params);
            return;
        }
        case 2:
        case 14:
        case 17: {
            // AS3 TaskParse:219-238：dialogId, [account, (type,id,number)*, flag]。
            // flag 是「关掉提示后要回哪句对白」的目标 NPC：TaskView.showTaskAlert 把
            // params.flag 塞进 needToChangeMouse.sendback，玩家关掉提示后
            // onDialogComplete(currentDialogID) 会回 TASK_TALK(flag, [dialogId])。
            // 也就是说这类 alert 节点**必须回包**（与纯提示的 alert 不同）。
            if (offset + 4 > packet.body.size()) return;
            const uint32_t dialogId =
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
            if (offset + 4 <= packet.body.size()) {
                const int account = ReadInt32LE(packet.body.data(), offset);
                for (int i = 0; i < account; ++i) {
                    if (offset + 12 > packet.body.size()) break;
                    ReadInt32LE(packet.body.data(), offset);
                    ReadInt32LE(packet.body.data(), offset);
                    ReadInt32LE(packet.body.data(), offset);
                }
                if (offset + 4 <= packet.body.size()) {
                    const uint32_t flag =
                        static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
                    if (flag != 0) {
                        npcId = flag;
                        forceReply = true;
                    }
                }
            }
            PushStoryPendingDialog(dialogId, npcId, packet.params, forceReply);
            return;
        }
        case 3:
            if (offset + 4 > packet.body.size()) return;
            PushStoryPendingDialog(
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset)), 0,
                packet.params);
            return;
        case 10: {
            // AS3 TaskParse:320-324 / TaskControl.opDialogBackType10：
            //   dialogOne, dialogTwo, npcid
            // 两条都排进 dialogList，逐条推进（等于 9999999 的那条丢弃）。
            if (offset + 12 > packet.body.size()) return;
            const uint32_t dialogOne =
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
            const uint32_t dialogTwo =
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
            npcId = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
            if (dialogOne != kDialogPlaceholder) {
                PushStoryPendingDialog(dialogOne, npcId, packet.params);
            }
            if (dialogTwo != kDialogPlaceholder) {
                PushStoryPendingDialog(dialogTwo, npcId, packet.params);
            }
            return;
        }
        case 26:
            // AS3 用 currentNPCID 兜底（TaskControl:459 附近），这里保持 npcId=0，
            // 由调用方退回当前子任务的入口 NPC。
            if (offset + 4 > packet.body.size()) return;
            PushStoryPendingDialog(
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset)), 0,
                packet.params);
            return;
        case 7: {
            if (offset + 12 > packet.body.size()) return;
            ReadInt32LE(packet.body.data(), offset);  // boardId
            const uint32_t dialogId =
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
            npcId = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
            PushStoryPendingDialog(dialogId, npcId, packet.params);
            return;
        }
        case 8: {
            if (offset + 4 > packet.body.size()) return;
            PushStoryPendingDialog(
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset)), 0,
                packet.params);
            return;
        }
        default:
            return;
    }
}

void StoryTaskOnNpcTaskListResponse(const GamePacket& packet) {
    if (!g_storyTaskRunning.load() || packet.body.size() < 4) {
        return;
    }
    // AS3 TaskParse.onReqNPCTaskList：body = [dialogId, (type, taskcount, ...)]。
    //   type == 1 → 接取清单：tasklist 是 taskcount 个子任务号，之后跟 canReceive、taskId；
    //               只有 canReceive == 1 玩家点「接受」才补发 ACCEPT_TASK。
    //   type == 0 → 任务列表窗口：tasklist 每条是 [dialogId, state]，**要用每条自己的
    //               dialogId 取 taskprop 并发 TASK_TALK**（TaskControl.loadPropsListBack:951），
    //               body 首个 int 只是该 NPC 的默认对白标识，不是本步要回的对话。
    const uint32_t npcId = packet.params;
    const uint32_t currentSubtask = g_storyTaskCurrentSubtask.load();

    size_t offset = 0;
    const uint32_t headerDialogId =
        static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));

    int listType = -1;
    int taskCount = 0;
    if (offset + 8 <= packet.body.size()) {
        listType = ReadInt32LE(packet.body.data(), offset);
        taskCount = ReadInt32LE(packet.body.data(), offset);
        if (taskCount == -1) {
            // specialType 分支：真正的条数在下一个 int，负数表示八卦/特殊入口。
            if (offset + 4 > packet.body.size()) {
                taskCount = 0;
            } else {
                taskCount = ReadInt32LE(packet.body.data(), offset);
            }
        }
    }

    uint32_t listDialogId = 0;
    uint32_t sameGroupDialogId = 0;
    std::vector<uint32_t> acceptSubtaskIds;
    bool canReceive = false;

    if (listType == 0 && taskCount > 0) {
        for (int i = 0; i < taskCount; ++i) {
            if (offset + 8 > packet.body.size()) {
                break;
            }
            const uint32_t entryDialogId =
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
            ReadInt32LE(packet.body.data(), offset);  // state：AS3 只透传给列表项，不过滤
            // dialogId / 100 = 子任务号（200100101 -> 2001001），与本步匹配的才是目标。
            const uint32_t entrySubtask = entryDialogId / 100;
            if (currentSubtask != 0 && entrySubtask == currentSubtask) {
                listDialogId = entryDialogId;
            } else if (currentSubtask != 0 &&
                       entrySubtask / 1000 == currentSubtask / 1000 &&
                       (sameGroupDialogId == 0 || entryDialogId < sameGroupDialogId)) {
                // 退一步：同组内取 dialogId 最小的那条（AS3 按 dialogId 升序排列，
                // 对应"列表里最靠前、尚未完成"的节点），避免随机落到组内靠后的子任务。
                sameGroupDialogId = entryDialogId;
            }
        }
    } else if (listType == 1 && taskCount > 0) {
        for (int i = 0; i < taskCount; ++i) {
            if (offset + 4 > packet.body.size()) {
                break;
            }
            acceptSubtaskIds.push_back(
                static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset)));
        }
        // AS3 TaskParse:544/545：type=1 的 tasklist 之后紧跟 canReceive 与 taskId。
        // canReceive != 1 表示这个 NPC 上已经在做该任务，AS3 只弹「已经接受了任务」，
        // 不补发 ACCEPT_TASK（TaskAcceptMachine.onClickAccept:130），只能等服务端推对话。
        if (offset + 4 <= packet.body.size()) {
            canReceive = ReadInt32LE(packet.body.data(), offset) == 1;
        }
        if (!canReceive) {
            acceptSubtaskIds.clear();
        } else if (currentSubtask != 0) {
            // 一个 NPC 可能同时给出多个可接子任务；只接本步要跑的那条，匹配不到就不接，
            // 避免把同一 NPC 上的其它任务一起接下来。
            std::vector<uint32_t> matched;
            for (uint32_t id : acceptSubtaskIds) {
                if (id == currentSubtask) {
                    matched.push_back(id);
                }
            }
            acceptSubtaskIds = std::move(matched);
        }
    }

    // 取本步该回的对话节点：
    //   type==1 且有条目 → AS3 只弹接取窗（onNPCTaskListBack 直接 return），没有对话；
    //   type==0 且有条目 → 优先本子任务条目，其次同组条目，都没有就不回（窗口由玩家点）；
    //   其余（无 type 字段 / taskcount<=0）→ 用 body 首个 int 作为默认对白标识。
    uint32_t dialogId = 0;
    if (listType == 0 && taskCount > 0) {
        dialogId = (listDialogId != 0) ? listDialogId : sameGroupDialogId;
    } else if (!(listType == 1 && taskCount > 0)) {
        dialogId = headerDialogId;
    }

    if (dialogId == 0 && acceptSubtaskIds.empty()) {
        return;
    }

    // 诊断回显：把服务端真实下发的字段打到状态栏，便于对照 AS3 TaskParse.onReqNPCTaskList
    // 逐字段核对（该回包布局在真机上仍未验证，见 docs/task-automation-mainline-dialog-contract.md §4）。
    {
        std::wstring dump = L"任务区：NPCTASKLIST npc=" + std::to_wstring(npcId) +
                            L" type=" + std::to_wstring(listType) +
                            L" count=" + std::to_wstring(taskCount) +
                            L" canReceive=" + (canReceive ? L"1" : L"0") +
                            L" 采用dialogId=" + std::to_wstring(dialogId) +
                            L" headerDialogId=" + std::to_wstring(headerDialogId) +
                            L" bodyInts=[";
        for (size_t i = 0; i + 4 <= packet.body.size() && i < 40; i += 4) {
            if (i != 0) {
                dump += L",";
            }
            dump += std::to_wstring(ReadInt32LE(packet.body.data(), i));
        }
        dump += L"]";
        UpdateTaskZoneUi(dump, true);
    }

    {
        std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
        if (listType == 1) {
            // 每次 type=1 回包整体替换，避免上一个 NPC 的残留。
            g_storyTaskState.pendingAcceptSubtaskIds = acceptSubtaskIds;
        }
    }
    if (dialogId != 0) {
        // 点 NPC 后直接下发的对话节点（NPCTASKLIST 的 header/dialogId）。
        PushStoryPendingDialog(dialogId, npcId, 0);
    }
}

// NPC_LIST_BACK（1315854）：body = [count, npcId...]（MainModel.onNpcListBack:1289）。
// 这是自动化唯一能拿到「服务端动态任务 NPC」的途径 —— 抓包里推进 2001003 的
// NPC_STATUS(21319) 对应的 id 在 data/npc.xml 中不存在，只出现在这条回包里。
void StoryTaskOnNpcListResponse(const GamePacket& packet) {
    if (!g_storyTaskRunning.load() || packet.body.size() < 4) {
        return;
    }
    size_t offset = 0;
    const int count = ReadInt32LE(packet.body.data(), offset);
    if (count <= 0) {
        return;
    }
    std::vector<uint32_t> npcIds;
    // 按剩余字节数收紧 reserve，count 只是包里的字段，不能直接当分配量。
    npcIds.reserve((std::min)(static_cast<size_t>(count), packet.body.size() / sizeof(uint32_t)));
    for (int i = 0; i < count; ++i) {
        if (offset + 4 > packet.body.size()) {
            break;
        }
        const uint32_t npcId =
            static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
        if (npcId != 0) {
            npcIds.push_back(npcId);
        }
    }
    std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
    g_storyTaskState.sceneNpcIds = std::move(npcIds);
    g_storyTaskState.sceneNpcListReady = true;
}

// GET_MAGIC_INDEX_BACK（1316125）：params=magicCount，body 每组 [id, type, state, num]。
// AS3 MainModel.onGetMagicIndxeBack:534 逐组读四项，type==1 是可施放法术。
// 主线「射击」类节点需要的 actionId 只能固定从这取（本地数据没有 id 映射）。
void StoryTaskOnMagicIndexResponse(const GamePacket& packet) {
    if (!g_storyTaskRunning.load()) {
        return;
    }
    const int count = static_cast<int>(packet.params);
    if (count <= 0) {
        return;
    }
    std::vector<uint32_t> magicIds;
    size_t offset = 0;
    for (int i = 0; i < count; ++i) {
        if (offset + 16 > packet.body.size()) {
            break;
        }
        const uint32_t id = static_cast<uint32_t>(ReadInt32LE(packet.body.data(), offset));
        const int type = ReadInt32LE(packet.body.data(), offset);
        ReadInt32LE(packet.body.data(), offset);  // state
        ReadInt32LE(packet.body.data(), offset);  // num
        if (type == 1 && id != 0) {
            magicIds.push_back(id);
        }
    }
    if (magicIds.empty()) {
        return;
    }
    RememberStoryLearnedMagicIds(magicIds);
}

void StoryTaskOnTaskFinishedResponse(const GamePacket& packet) {
    // AS3 TaskParse.onReqTaskFinishedBack：subtaskID 取自 params，body 只是可选奖励，
    // 正常完成通知的 body 为空，所以这里不能按 body 判空。
    if (!g_storyTaskRunning.load()) {
        return;
    }
    const uint32_t subtaskId = packet.params;
    if (subtaskId == 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(g_storyTaskState.mutex);
    g_storyTaskState.finishedSubtaskId = subtaskId;
    g_storyTaskState.finishedSubtaskReady = true;
    if (std::find(g_storyTaskState.finishedSubtaskIds.begin(),
                  g_storyTaskState.finishedSubtaskIds.end(),
                  subtaskId) == g_storyTaskState.finishedSubtaskIds.end()) {
        g_storyTaskState.finishedSubtaskIds.push_back(subtaskId);
    }
}

BOOL StartHuangchengWeijiTaskAsync(const std::vector<uint32_t>& subtaskIds) {
    return StartStoryTaskAsync(subtaskIds);
}

VOID StopHuangchengWeijiTask() {
    StopStoryTask();
}

// 把配方表里的主线子任务列表推给 UI，避免前端维护一份硬编码清单。
void PushStoryTaskDefinitionToUi() {
    std::wstring script = L"if(window.setHuangchengWeijiTaskDefs) { window.setHuangchengWeijiTaskDefs([";
    for (uint32_t i = 0; i < StoryTaskRecipe::SUBTASK_COUNT; ++i) {
        const StoryTaskRecipe::Subtask& s = StoryTaskRecipe::SUBTASKS[i];
        std::wstring mapName = GetMapName(static_cast<int>(s.scene));
        if (i > 0) {
            script += L",";
        }
        script += L"{subtaskId:" + std::to_wstring(s.subtaskId) +
                  L",taskId:" + std::to_wstring(s.taskId) +
                  L",sceneId:" + std::to_wstring(s.scene) +
                  L",taskName:'" + UIBridge::EscapeJsonString(Utf8ToWide(s.taskName)) + L"'" +
                  L",name:'" + UIBridge::EscapeJsonString(Utf8ToWide(s.subtaskName)) + L"'" +
                  L",sceneName:'" + UIBridge::EscapeJsonString(mapName) + L"'}";
    }
    script += L"]); }";
    PostScriptToUI(script);
}

// 主线剧情任务区 owner（皇城危机、魔晶战记等）。
// 声明位置必须早于 InitializeDefaultHandlers：进图响应回调要置位 g_storyTaskMapEntered。
std::atomic<bool> g_storyTaskRunning{false};
std::atomic<unsigned long long> g_storyTaskSession{0};
// 剧情战斗（任务节点里的 <battle id="...">）的驱动状态。
// 服务端不会替玩家打完，客户端必须自己走 READY→USER_OP→PLAY_OVER→END_SEND。
std::atomic<bool> g_storyBattleActive{false};
std::atomic<uint32_t> g_storyBattleDialogId{0};
std::atomic<uint32_t> g_storyBattleNpcId{0};
// 点战斗节点的时间：服务端迟迟不开战就别再挂着这套状态，免得接管别人的战斗。
std::atomic<DWORD> g_storyBattleArmTick{0};
// 这一场战斗的开始时间：BATTLE_END 迟迟不来时用它兜底解除接管状态。
std::atomic<DWORD> g_storyBattleActiveTick{0};
std::atomic<bool> g_storyTaskMapEntered{false};
// 置位 g_storyTaskMapEntered 时的会话号：进图回包可能迟到，用它挡掉上一会话的残留。
std::atomic<unsigned long long> g_storyTaskMapEnterSession{0};



static std::vector<uint32_t> NormalizeStorySelection(const std::vector<uint32_t>& subtaskIds) {
    std::vector<uint32_t> normalized;
    for (uint32_t subtaskId : subtaskIds) {
        if (subtaskId == 0) {
            continue;
        }
        if (std::find(normalized.begin(), normalized.end(), subtaskId) == normalized.end()) {
            normalized.push_back(subtaskId);
        }
    }
    std::sort(normalized.begin(), normalized.end());
    return normalized;
}
