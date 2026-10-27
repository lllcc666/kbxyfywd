#include "web_message_handler.h"

#include <windows.h>
#include <WebView2.h>
#include <shellapi.h>

#include <atomic>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "battle_six.h"
#include "dungeon_jump.h"
#include "activity_bus.h"
#include "activity_minigames.h"
#include "horse_competition.h"
#include "latest_activities.h"
#include "little_game.h"
#include "shuangtai.h"
#include "wpe_hook.h"
#include "spirit_collect.h"
#include "app_host.h"
#include "utils.h"
#include "ui_bridge.h"
BOOL SavePacketListToFile(const std::wstring& filePath);
BOOL LoadPacketListFromFile(const std::wstring& filePath);

namespace {

bool TryParseIntInRangeLocal(const std::wstring& text, int minValue, int maxValue, int defaultValue, int& value) {
    const size_t start = text.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) {
        value = defaultValue;
        return false;
    }

    const size_t endPos = text.find_last_not_of(L" \t\r\n");
    const std::wstring trimmed = text.substr(start, endPos - start + 1);

    wchar_t* end = nullptr;
    long parsed = std::wcstol(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str() || *end != L'\0') {
        value = defaultValue;
        return false;
    }

    if (parsed < minValue) {
        value = minValue;
    } else if (parsed > maxValue) {
        value = maxValue;
    } else {
        value = static_cast<int>(parsed);
    }
    return true;
}

std::wstring UnescapeSerializedWebMessage(std::wstring msg) {
    if (msg.size() < 2 || msg.front() != L'"' || msg.back() != L'"') {
        return msg;
    }

    std::wstring unescaped;
    unescaped.reserve(msg.size());
    for (size_t i = 1; i + 1 < msg.size(); ++i) {
        if (msg[i] == L'\\' && i + 2 < msg.size()) {
            switch (msg[i + 1]) {
                case L'"': unescaped += L'"'; ++i; break;
                case L'\\': unescaped += L'\\'; ++i; break;
                case L'n': unescaped += L'\n'; ++i; break;
                case L'r': unescaped += L'\r'; ++i; break;
                case L't': unescaped += L'\t'; ++i; break;
                default: unescaped += msg[i]; break;
            }
        } else {
            unescaped += msg[i];
        }
    }
    return unescaped;
}

std::wstring GetJsonValue(const std::wstring& msg, const std::wstring& key) {
    const size_t keyPos = msg.find(L"\"" + key + L"\"");
    if (keyPos == std::wstring::npos) {
        return L"";
    }

    const size_t colonPos = msg.find(L":", keyPos);
    if (colonPos == std::wstring::npos) {
        return L"";
    }

    const size_t valueStart = msg.find_first_not_of(L" \t\n\r", colonPos + 1);
    if (valueStart == std::wstring::npos) {
        return L"";
    }

    if (msg[valueStart] == L'"') {
        const size_t valueEnd = msg.find(L"\"", valueStart + 1);
        if (valueEnd != std::wstring::npos) {
            return msg.substr(valueStart + 1, valueEnd - valueStart - 1);
        }
        return L"";
    }

    if (msg[valueStart] == L'[') {
        size_t bracketCount = 1;
        size_t valueEnd = valueStart + 1;
        while (valueEnd < msg.length() && bracketCount > 0) {
            if (msg[valueEnd] == L'[') {
                ++bracketCount;
            } else if (msg[valueEnd] == L']') {
                --bracketCount;
            }
            ++valueEnd;
        }
        if (bracketCount == 0) {
            return msg.substr(valueStart, valueEnd - valueStart);
        }
        return L"";
    }

    const size_t valueEnd = msg.find_first_of(L",}", valueStart);
    if (valueEnd != std::wstring::npos) {
        return msg.substr(valueStart, valueEnd - valueStart);
    }
    return msg.substr(valueStart);
}

std::wstring TrimWhitespace(std::wstring value) {
    const size_t start = value.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) {
        return L"";
    }

    const size_t end = value.find_last_not_of(L" \t\r\n");
    value.erase(end + 1);
    value.erase(0, start);
    return value;
}

std::wstring GetTrimmedJsonValue(const std::wstring& msg, const std::wstring& key) {
    return TrimWhitespace(GetJsonValue(msg, key));
}

bool GetJsonBoolValue(const std::wstring& msg, const std::wstring& key) {
    return GetTrimmedJsonValue(msg, key) == L"true";
}

uint32_t GetJsonUInt32Value(const std::wstring& msg, const std::wstring& key, uint32_t defaultValue = 0) {
    const std::wstring value = GetTrimmedJsonValue(msg, key);
    if (value.empty()) {
        return defaultValue;
    }
    return static_cast<uint32_t>(_wtol(value.c_str()));
}

DWORD GetJsonDWORDValue(const std::wstring& msg, const std::wstring& key, DWORD defaultValue = 0) {
    const std::wstring value = GetTrimmedJsonValue(msg, key);
    if (value.empty()) {
        return defaultValue;
    }
    return static_cast<DWORD>(_wtol(value.c_str()));
}

void SetHelperText(const std::wstring& text) {
    UIBridge::Instance().UpdateHelperText(text);
}

void SetHelperText(const wchar_t* text) {
    SetHelperText(text ? std::wstring(text) : std::wstring());
}

void UpdateDungeonJumpStatus(const std::wstring& text) {
    UIBridge::Instance().ExecuteJS(
        L"if(window.updateDungeonJumpStatus) { window.updateDungeonJumpStatus('" +
        UIBridge::EscapeJsonString(text) + L"'); }");
}

void ClearPacketListView() {
    static const wchar_t* clearScript = LR"(
        (function(){
            var pListItems = document.getElementById('packet-list-items');
            if (pListItems) {
                pListItems.innerHTML = '';
            }
            if (window.updatePacketCount) {
                var count = pListItems ? pListItems.children.length : 0;
                window.updatePacketCount(count);
            }
        })();
    )";
    AppHost::ExecuteScript(clearScript);
}

void ParseIndicesArray(const std::wstring& msg, std::vector<DWORD>& indices);
void ParseUInt32Array(const std::wstring& msg, const std::wstring& key, std::vector<uint32_t>& values);

template <typename Func, typename... Args>
void LaunchDetachedStdThread(Func func, Args... args) {
    std::thread(std::move(func), std::forward<Args>(args)...).detach();
}

void HandleSendRawPacketCommand(const std::wstring& msg) {
    const std::wstring hexW = GetJsonValue(msg, L"hex");
    if (hexW.empty()) {
        SetHelperText(L"封包数据格式错误");
        return;
    }

    auto data = StringToHex(WideToUtf8(hexW));
    LaunchDetachedStdThread([data = std::move(data)]() {
        const BOOL result = SendPacketBytes(data);
        if (!result) {
            SetHelperText(L"封包发送失败：未连接到游戏服务器");
        }
    });
}

void HandlePacketWindowVisibilityChanged(const std::wstring& msg) {
    const bool visible = GetJsonBoolValue(msg, L"visible");
    RECT packetWindowRect{};
    if (visible) {
        packetWindowRect.left = static_cast<LONG>(GetJsonDWORDValue(msg, L"left"));
        packetWindowRect.top = static_cast<LONG>(GetJsonDWORDValue(msg, L"top"));
        packetWindowRect.right = static_cast<LONG>(GetJsonDWORDValue(msg, L"width"));
        packetWindowRect.bottom = static_cast<LONG>(GetJsonDWORDValue(msg, L"height"));
        AppHost::SetPacketWindowState(true, packetWindowRect);
        SyncPacketsToUI();
    } else {
        AppHost::SetPacketWindowVisible(false);
        UIBridge::Instance().UpdatePacketCount(GetPacketCount());
    }
    AppHost::UpdateBrowserRegion();
}

void HandleDeleteSelectedPacketsCommand(const std::wstring& msg) {
    std::vector<DWORD> indices;
    ParseIndicesArray(msg, indices);
    if (!indices.empty()) {
        DeleteSelectedPackets(indices);
    }
    ClearPacketListView();
    SyncPacketsToUI();
}

template <typename ActionFunc>
void HandleActionCommand(ActionFunc action);

void HandleSetSpeedCommand(const std::wstring& msg) {
    const float speed = static_cast<float>(_wtof(GetTrimmedJsonValue(msg, L"speed").c_str()));
    HandleActionCommand([speed]() {
        AppHost::ApplySpeedhack(speed);
    });
}

template <typename BoolTarget>
void HandleSetBoolFlagCommand(const std::wstring& msg, const wchar_t* key, BoolTarget& target) {
    target = GetJsonBoolValue(msg, key);
}

template <typename ActionFunc>
void HandleBoolResultCommand(ActionFunc action, const wchar_t* successText, const wchar_t* failureText = nullptr) {
    if (action()) {
        if (successText) {
            SetHelperText(successText);
        }
    } else if (failureText) {
        SetHelperText(failureText);
    }
}

template <typename ActionFunc, typename SuccessFunc>
void HandleBoolResultCommand(ActionFunc action, SuccessFunc onSuccess, const wchar_t* failureText) {
    if (action()) {
        onSuccess();
    } else if (failureText) {
        SetHelperText(failureText);
    }
}

template <typename ActionFunc>
void HandleActionWithTextCommand(ActionFunc action, const wchar_t* text) {
    action();
    if (text) {
        SetHelperText(text);
    }
}

template <typename ActionFunc>
void HandleActionCommand(ActionFunc action) {
    action();
}

// 道具操作（购买/使用）后刷新背包与界面数量。
// 顺序必须与 AS3 一致：先发操作包，成功后再查背包。
// 反了就会把操作前的旧数量写回界面（历史上就是这么错的：早期实现先查包再 Sleep(200) 发操作包，
// 服务端返回的是操作前的数量，界面被旧值覆盖）。
//
// 这里用的是「发完操作包立刻查」的简化做法，可靠程度取决于服务端何时扣减：
//   购买 —— 可靠。服务端先在 onBuyGoodsBack 里落账（MainModel.as:1115），
//            TCP 同连接按序处理，查包必然排在落账之后。
//   使用 —— 有前提。真客户端等用品结果包回来才补发查包（PackControl.as:442-445），
//            因为 OP_CLIENT_USER_OP 的 back 为 0（MsgDoc.as:279-282）无法直接等；
//            本实现在操作包之后立即查，若服务端把扣减推迟到回合结算，
//            这里就可能仍拿到扣减前的数量。届时需改为挂到回合包之后再查，
//            或补一次延迟重查（同一份全量快照，后到的会覆盖先到的）。
// AS3 依据：
//   购买 FlashControl.as:163 发 OP_CLIENT_BUY_GOODS → MainModel.onBuyGoodsBack(MainModel.as:1115)
//        → mParams==-1 分支刷包(MainModel.as:1042-1056)
//   使用 BattleControl.as:396 发 USER_OP(param=2) → onUsePropsBack(MainModel.as:565-633)
//        → USEPROPSBACK_PACK 事件 → PackControl.as:442-445 补发 REQ_PACKAGE_DATA
template <typename ActionFunc>
bool RunThenRefreshPackageData(ActionFunc action) {
    const bool sent = action();
    if (!sent) {
        return false;
    }
    SendReqPackageDataPacket(0xFFFFFFFF);
    return true;
}

void HandleSetAutoHealEnabledCommand(const std::wstring& msg) {
    HandleSetBoolFlagCommand(msg, L"enabled", g_autoHeal);
}

void HandleSetBlockBattleEnabledCommand(const std::wstring& msg) {
    HandleSetBoolFlagCommand(msg, L"enabled", g_blockBattle);
}

void HandleSetAutoGoHomeEnabledCommand(const std::wstring& msg) {
    HandleSetBoolFlagCommand(msg, L"enabled", g_autoGoHome);
}

void HandleQueryLingyuCommand() {
    HandleActionCommand(SendQueryLingyuPacket);
}

void HandleQueryMonstersCommand() {
    HandleActionCommand(SendQueryMonsterPacket);
}

void HandleRefreshPackItemsCommand() {
    HandleActionCommand([]() {
        SendReqPackageDataPacket(0xFFFFFFFF);
    });
}

// 刷新「最新活动」剩余次数：QueryActivityCountsAsync 内部已异步执行，直接调用
void HandleRefreshActivityCountsCommand() {
    QueryActivityCountsAsync();
}

void HandleBuyDiceCommand() {
    HandleActionCommand(SendBuyDicePacket);
}

void HandleBuyDice18Command() {
    // 买骰子的结果要看服务器回包（operation==6 的 result），不是发包成功就算买到：
    // result: 0=成功 / -1=铜钱不足 / -3=当日已达上限
    HandleBoolResultCommand(SendBuyDicePacket, L"购买请求已发送", L"购买请求发送失败");
}

void HandleClearPacketListCommand() {
    HandleActionCommand(ClearPacketList);
}

void HandleStartInterceptCommand() {
    HandleActionCommand(StartIntercept);
}

void HandleStopInterceptCommand() {
    HandleActionCommand(StopIntercept);
}

void HandleSetInterceptTypeCommand(const std::wstring& msg) {
    const bool send = GetJsonBoolValue(msg, L"send");
    const bool recv = GetJsonBoolValue(msg, L"recv");
    HandleActionCommand([send, recv]() {
        SetInterceptType(send, recv);
    });
}

void HandleStartDailyTasksCommand(const std::wstring& msg) {
    const DWORD flags = GetJsonDWORDValue(msg, L"flags");
    HandleActionCommand([flags]() {
        StartDailyTasksAsync(flags);
    });
}

// 障碍赛跑（Act810）：走新活动总线 1185429，扫荡路径不吃跑酷玩法。
void HandleStartRunCompetitionCommand(const std::wstring& msg) {
    const int sweepTimes = static_cast<int>(GetJsonDWORDValue(msg, L"times"));
    HandleActionCommand([sweepTimes]() {
        StartRunCompetitionAsync(sweepTimes);
    });
}

// 金平府祈愿（activityId 688）：start → finish 往返。
void HandleStartJinpingfuWishCommand(const std::wstring& msg) {
    const int rounds = static_cast<int>(GetJsonDWORDValue(msg, L"times"));
    const int holdMs = static_cast<int>(GetJsonDWORDValue(msg, L"hold_ms"));
    HandleActionCommand([rounds, holdMs]() {
        StartJinpingfuWishAsync(rounds, holdMs);
    });
}

// 知识小天地（gameType 10003）：小游戏总线开局挂机再结算。
void HandleStartKnowledgeGardenCommand(const std::wstring& msg) {
    const int rounds = static_cast<int>(GetJsonDWORDValue(msg, L"times"));
    const int holdMs = static_cast<int>(GetJsonDWORDValue(msg, L"hold_ms"));
    HandleActionCommand([rounds, holdMs]() {
        StartKnowledgeGardenAsync(rounds, holdMs);
    });
}

// 五子棋大作战（activityId 22003 / 场景 1010 / NPC 18004）。
void HandleStartGomokuCommand(const std::wstring& msg) {
    const int rounds = static_cast<int>(GetJsonDWORDValue(msg, L"times"));
    HandleActionCommand([rounds]() {
        StartGomokuAsync(rounds);
    });
}

void HandleStopTaskZoneCommand() {
    HandleActionWithTextCommand(StopEightTrigramsTask, L"任务区已停止");
}

void HandleStartTaskZoneCommand() {
    HandleActionCommand(StartEightTrigramsTaskAsync);
}

void HandleQueryHuangchengWeijiTaskProgressCommand(const std::wstring& msg) {
    const DWORD taskId = GetJsonDWORDValue(msg, L"task_id", 2001000);
    // 必须异步：这个 handler 跑在 WebView 消息线程上，而进度查询要等
    // USER_TASK_LIST_BACK 回包；在该线程上阻塞会一起卡住回包处理，3 秒必然超时。
    // 结果文案由工作线程自己回显，这里不预设成败。
    if (!QueryTaskZoneUserTaskListProgressAsync(taskId)) {
        SetHelperText(L"主线任务：读取进度失败");
    } else {
        SetHelperText(L"主线任务：正在读取进度...");
    }
}

void HandleStartHuangchengWeijiTaskCommand(const std::wstring& msg) {
    std::vector<uint32_t> subtaskIds;
    ParseUInt32Array(msg, L"subtask_ids", subtaskIds);
    if (subtaskIds.empty()) {
        std::vector<DWORD> legacyIndices;
        ParseIndicesArray(msg, legacyIndices);
        for (DWORD index : legacyIndices) {
            subtaskIds.push_back(static_cast<uint32_t>(index));
        }
    }

    HandleBoolResultCommand(
        [subtaskIds]() {
            return StartHuangchengWeijiTaskAsync(subtaskIds);
        },
        L"皇城危机：任务区列表已载入",
        L"皇城危机：请先勾选任务或先进入游戏");
}

void HandleStopHuangchengWeijiTaskCommand() {
    HandleActionWithTextCommand(StopHuangchengWeijiTask, L"皇城危机：已停止");
}

void HandleQueryShuangTaiMonstersCommand() {
    HandleActionCommand(QueryShuangTaiMonsters);
}

void HandleStartShuangTaiCommand(const std::wstring& msg) {
    const bool blockBattle = GetJsonBoolValue(msg, L"blockBattle");
    HandleBoolResultCommand(
        [blockBattle]() {
            return StartOneKeyShuangTaiPacket(blockBattle);
        },
        L"双台谷刷级已开始，请查看辅助日志",
        L"双台谷刷级启动失败，请先点击查询按钮获取妖怪数据");
}

void HandleStopShuangTaiCommand() {
    HandleActionWithTextCommand(StopShuangTai, L"双台谷刷级已停止");
}

void HandleCancelBattlesixMatchCommand() {
    HandleBoolResultCommand(
        []() {
            g_battleSixAuto.SetAutoMatching(false);
            g_battleSixAuto.SetMatchCount(0);
            return CancelBattleSixMatch();
        },
        L"万妖盛会：已取消匹配",
        L"万妖盛会：取消匹配失败");
}

void HandleStopHorseCompetitionCommand() {
    HandleActionCommand(RequestStopHorseCompetition);
}

void HandleStartHeavenFuruiCommand(const std::wstring& msg) {
    int maxBoxes = 30;
    const std::wstring maxBoxesStr = GetTrimmedJsonValue(msg, L"maxBoxes");
    if (!maxBoxesStr.empty()) {
        TryParseIntInRangeLocal(maxBoxesStr, 1, 9999, 30, maxBoxes);
    }
    HandleBoolResultCommand(
        [maxBoxes]() {
            return StartOneKeyHeavenFuruiPacket(maxBoxes);
        },
        L"福瑞宝箱：开始遍历地图查找宝箱...",
        L"福瑞宝箱启动失败");
}

void HandleStopHeavenFuruiCommand() {
    HandleActionWithTextCommand(StopHeavenFurui, L"福瑞宝箱：已停止");
}

void HandleSetHijackEnabledCommand(const std::wstring& msg) {
    const bool enabled = GetJsonBoolValue(msg, L"enabled");
    HandleActionCommand([enabled]() {
        SetHijackEnabled(enabled);
    });
}

void HandleClearHijackRulesCommand() {
    HandleActionCommand(ClearHijackRules);
}

void HandleSavePacketListCommand() {
    const std::wstring filePath = AppHost::ShowSaveFileDialog(L"", L"packets.txt");
    if (!filePath.empty()) {
        HandleBoolResultCommand(
            [filePath]() {
                return SavePacketListToFile(filePath);
            },
            L"封包列表已保存",
            L"保存封包列表失败");
    }
}

void HandleLoadPacketListCommand() {
    const std::wstring filePath = AppHost::ShowOpenFileDialog(L"");
    if (!filePath.empty()) {
        HandleBoolResultCommand(
            [filePath]() {
                if (LoadPacketListFromFile(filePath)) {
                    SyncPacketsToUI();
                    return true;
                }
                return false;
            },
            L"封包列表已载入（仅发送包）",
            L"载入封包列表失败");
    }
}

void HandleStopSendCommand() {
    HandleActionWithTextCommand(StopAutoSend, L"已停止发送封包");
}

void HandleEnterBossBattleCommand(const std::wstring& msg) {
    const uint32_t spiritId = GetJsonUInt32Value(msg, L"bossId");
    if (spiritId > 10000) {
        HandleBoolResultCommand(
            [spiritId]() {
                return SendBattlePacket(spiritId, 32, 0);
            },
            [spiritId]() {
                SetHelperText(L"已发送BOSS战斗封包，ID: " + std::to_wstring(spiritId) + L", useId: 32");
            },
            L"发送战斗封包失败，未连接到游戏服务器");
    } else {
        SetHelperText(L"无效的对象ID，必须大于10000");
    }
}

void HandleStartOneKeyCollectCommand(const std::wstring& msg) {
    const DWORD flags = GetJsonDWORDValue(msg, L"flags");
    HandleBoolResultCommand(
        [flags]() {
            return StartOneKeyCollectPacket(flags);
        },
        L"一键采集已开始，请查看辅助日志",
        L"一键采集启动失败，可能已经在运行或未进入游戏");
}

void HandleStartOneKeyXuanttaCommand() {
    HandleBoolResultCommand(
        StartOneKeyTowerPacket,
        L"一键玄塔已开始，请查看辅助日志",
        L"一键玄塔启动失败，可能已经在运行或未进入游戏");
}

void HandleStartBattleSixAutoMatchCommand(const std::wstring& msg) {
    const std::wstring matchCountStr = GetTrimmedJsonValue(msg, L"matchCount");
    int matchCount = 1;
    TryParseIntInRangeLocal(matchCountStr, 1, 999, 1, matchCount);
    wchar_t startMsg[128];
    swprintf_s(startMsg, L"万妖盛会：开始匹配（共%d次）...", matchCount);
    SetHelperText(startMsg);
    LaunchDetachedStdThread([matchCount]() {
        StartOneKeyBattleSixPacket(matchCount);
    });
}

void HandleSetBattleSixAutoBattleEnabledCommand(const std::wstring& msg) {
    const std::wstring enabledStr = GetTrimmedJsonValue(msg, L"enabled");
    const bool enabled = (enabledStr == L"true");
    g_battleSixAuto.SetAutoBattle(enabled);
    SetHelperText(L"调试：enabled=[" + enabledStr + L"] parsed=" + std::wstring(enabled ? L"true" : L"false"));
    SetHelperText(enabled ? L"万妖盛会：自动战斗已启用" : L"万妖盛会：自动战斗已禁用");
}

void HandleStartDungeonJumpCommand(const std::wstring& msg) {
    const std::wstring layerStr = GetTrimmedJsonValue(msg, L"targetLayer");
    int targetLayer = 1;
    TryParseIntInRangeLocal(layerStr, 1, 9999, 1, targetLayer);
    UpdateDungeonJumpStatus(std::wstring(L"副本跳层：准备跳转到第") + std::to_wstring(targetLayer) + L"层...");
    LaunchDetachedStdThread([targetLayer]() {
        StartOneKeyDungeonJumpPacket(targetLayer);
    });
}

void HandleStopDungeonJumpCommand() {
    StopDungeonJump();
    UpdateDungeonJumpStatus(L"副本跳层：已停止");
}

void HandleHorseCompetitionProgressCommand(const std::wstring& progress) {
    SetHelperText(std::wstring(L"坐骑大赛：") + progress);

    // 面板上的「状态: 就绪」此前是死的：C++ 只往左下角 helper 写文字，
    // #horse-competition-status 从来没人更新。把同一句进度也推到面板上，
    // 顺手标记 running/waiting 用于配色（见 ui.html 的 updateHorseCompetitionStatus）。
    std::wstring escaped;
    escaped.reserve(progress.size() + 8);
    for (const wchar_t ch : progress) {
        if (ch == L'\'' || ch == L'\\') {
            escaped.push_back(L'\\');
        }
        if (ch == L'\n' || ch == L'\r') {
            escaped.push_back(L' ');
            continue;
        }
        escaped.push_back(ch);
    }
    const bool running = progress.find(L"失败") == std::wstring::npos &&
                         progress.find(L"未") == std::wstring::npos &&
                         progress.find(L"停止") == std::wstring::npos &&
                         progress.find(L"完成") == std::wstring::npos;
    PostScriptToUI(L"if(window.updateHorseCompetitionStatus) { window.updateHorseCompetitionStatus('" +
                   escaped + L"', '" + (running ? L"running" : L"idle") + L"'); }");
}

void HandleOneKeyHorseCompetitionWorker() {
    // 启动是否成功只有这里知道（已在运行、状态异常都会返回 FALSE）。
    // 之前不看返回值，界面会先报「已开始」，实际上什么都没跑。
    if (!StartOneKeyHorseCompetitionPacket()) {
        SetHelperText(L"坐骑大赛启动失败（可能已在运行中）");
    }
}

void HandleStartOneKeyHorseCompetitionCommand() {
    SetHelperText(L"正在启动坐骑大赛（临时坐骑）...");
    SetHorseProgressCallback(HandleHorseCompetitionProgressCommand);
    LaunchDetachedStdThread(HandleOneKeyHorseCompetitionWorker);
}

void HandleBuyItemCommand(const std::wstring& msg) {
    const uint32_t itemId = GetJsonUInt32Value(msg, L"itemId");
    const uint32_t count = GetJsonUInt32Value(msg, L"count", 1U);
    if (itemId > 0) {
        HandleBoolResultCommand(
            [itemId, count]() {
                return RunThenRefreshPackageData([itemId, count]() {
                    return SendBuyGoodsPacket(itemId, count);
                });
            },
            [itemId, count]() {
                wchar_t buffer[128];
                swprintf_s(buffer, L"购买道具成功: %s x%u", GetItemName(itemId).c_str(), count);
                SetHelperText(buffer);
            },
            nullptr);
    }
}

void HandleUseItemCommand(const std::wstring& msg) {
    const uint32_t itemId = GetJsonUInt32Value(msg, L"itemId");
    if (itemId > 0) {
        if (!g_battleStarted) {
            SetHelperText(L"使用道具需要在战斗中进行");
        } else {
            HandleBoolResultCommand(
                [itemId]() {
                    return RunThenRefreshPackageData([itemId]() {
                        return SendUseItemInBattlePacket(itemId);
                    });
                },
                [itemId]() {
                    wchar_t buffer[128];
                    swprintf_s(buffer, L"使用道具: %s", GetItemName(itemId).c_str());
                    SetHelperText(buffer);
                },
                L"使用道具失败，背包中无此道具");
        }
    }
}

void HandleStartSendAllPacketsProgressCommand(DWORD currentLoop, DWORD packetIndex, const std::string& label) {
    std::wstring message = L"正在发送第" + std::to_wstring(currentLoop) + L"次，第" + std::to_wstring(packetIndex) + L"个封包";
    if (!label.empty()) {
        message += L"，标签：" + Utf8ToWide(label);
    }
    SetHelperText(message);
}

void HandleStartSendAllPacketsWorker(DWORD sendCount, DWORD sendDelay) {
    SendAllPackets(sendDelay, sendCount, HandleStartSendAllPacketsProgressCommand);
    SetHelperText(L"封包发送完成");
}

void HandleStartSendAllPacketsCommand(const std::wstring& msg) {
    DWORD sendCount = 1;
    DWORD sendDelay = 300;
    const std::wstring sendCountStr = GetJsonValue(msg, L"sendCount");
    if (!sendCountStr.empty()) {
        const long parsedCount = _wtol(sendCountStr.c_str());
        // 上限兜底：界面填错（或填 -1 被转成 DWORD 上限）时别真的发几十亿个包。
        sendCount = parsedCount < 1 ? 1 : (parsedCount > 10000 ? 10000 : static_cast<DWORD>(parsedCount));
    }
    const std::wstring sendDelayStr = GetJsonValue(msg, L"sendDelay");
    if (!sendDelayStr.empty()) {
        const long parsedDelay = _wtol(sendDelayStr.c_str());
        // 间隔同样要夹住：负数转成 DWORD 会变成几十亿毫秒，等于整条流程卡死。
        sendDelay = parsedDelay < 0 ? 0 : (parsedDelay > 60000 ? 60000 : static_cast<DWORD>(parsedDelay));
    }
    LaunchDetachedStdThread(HandleStartSendAllPacketsWorker, sendCount, sendDelay);
}

// 最新活动（周活动小游戏）：统一按活动 ID 启动主流程。
// 扫荡与否由 C++ 侧按历史最好成绩自动判定，前端无需传参。
// 带 activity_ids 数组时整批串行执行（一个跑完次数再跑下一个）；
// 只有单个 ID 时退化为单活动执行。
void HandleStartLatestActivityCommand(const std::wstring& msg, uint32_t activityId) {
    std::vector<uint32_t> activityIds;
    ParseUInt32Array(msg, L"activity_ids", activityIds);
    if (activityIds.empty()) {
        activityIds.push_back(activityId);
    }
    StartOneKeyLatestActivitiesPacket(activityIds);
}

void HandleStopLatestActivitiesCommand() {
    StopLatestActivities();
}

void HandleDecomposeLingyuIndicesCommand(const std::wstring& msg, const wchar_t* indicesKey) {
    const std::wstring jsonArray = GetJsonValue(msg, indicesKey);
    if (!jsonArray.empty()) {
        SendDecomposeLingyuPacket(jsonArray);
    }
}

void HandleDecomposeLingyuBatchCommand(const std::wstring& msg);
void HandleDecomposeLingyuArrayCommand(const std::wstring& msg);
template <typename SendFunc>
void HandleSpiritCollectSendResultCommand(SendFunc sendFunc, const wchar_t* successText, const wchar_t* failureText);

void HandleDecomposeLingyuCommand(const std::wstring& msg) {
    if (msg.find(L"decompose_lingyu_batch") != std::wstring::npos) {
        HandleDecomposeLingyuBatchCommand(msg);
    } else {
        HandleDecomposeLingyuArrayCommand(msg);
    }
}

void HandleSpiritCollectOpenUiCommand() {
    HandleSpiritCollectSendResultCommand(
        SendSpiritOpenUIPacket,
        L"精魄系统：正在获取数据...",
        L"精魄系统：发送请求失败");
}

void HandleSpiritCollectGetSpiritsCommand() {
    HandleSpiritCollectSendResultCommand(
        SendSpiritPresuresPacket,
        L"精魄系统：正在获取精魄列表...",
        L"精魄系统：获取精魄列表失败");
}

void HandleSpiritCollectVerifyPlayerCommand(const std::wstring& msg) {
    const std::wstring friendIdStr = GetTrimmedJsonValue(msg, L"friendId");
    if (!friendIdStr.empty()) {
        uint32_t friendId = static_cast<uint32_t>(_wtol(friendIdStr.c_str()));
        g_spiritCollectState.selectedFriendId = friendId;
        HandleSpiritCollectSendResultCommand(
            [friendId]() {
                return SendSpiritPlayerInfoPacket(friendId);
            },
            L"精魄系统：正在验证玩家信息...",
            L"精魄系统：验证玩家信息失败");
    }
}

void HandleSpiritCollectSendSpiritCommand(const std::wstring& msg) {
    const std::wstring friendIdStr = GetTrimmedJsonValue(msg, L"friendId");
    const std::wstring eggIdStr = GetTrimmedJsonValue(msg, L"eggId");
    if (!friendIdStr.empty() && !eggIdStr.empty()) {
        uint32_t friendId = static_cast<uint32_t>(_wtol(friendIdStr.c_str()));
        uint32_t eggId = static_cast<uint32_t>(_wtol(eggIdStr.c_str()));
        HandleSpiritCollectSendResultCommand(
            [friendId, eggId]() {
                return SendSpiritGiftPacket(friendId, eggId);
            },
            L"精魄系统：正在发送精魄...",
            L"精魄系统：发送精魄失败");
    }
}

void HandleSpiritCollectHistoryCommand(const std::wstring& msg) {
    const std::wstring typeStr = GetTrimmedJsonValue(msg, L"recordType");
    if (!typeStr.empty()) {
        int type = _wtoi(typeStr.c_str());
        HandleSpiritCollectSendResultCommand(
            [type]() {
                return SendSpiritHistoryPacket(type);
            },
            L"精魄系统：正在获取历史记录...",
            L"精魄系统：获取历史记录失败");
    }
}

void HandleDecomposeLingyuBatchCommand(const std::wstring& msg) {
    HandleDecomposeLingyuIndicesCommand(msg, L"indices");
}

void HandleDecomposeLingyuArrayCommand(const std::wstring& msg) {
    HandleDecomposeLingyuIndicesCommand(msg, L"indices_array");
}

template <typename SendFunc>
void HandleSpiritCollectSendResultCommand(SendFunc sendFunc, const wchar_t* successText, const wchar_t* failureText) {
    if (sendFunc()) {
        SetHelperText(successText);
    } else {
        SetHelperText(failureText);
    }
}

void HandleSpiritCollectCommand(const std::wstring& msg) {
    const std::wstring action = GetJsonValue(msg, L"action");
    if (action == L"open_ui") {
        HandleSpiritCollectOpenUiCommand();
    } else if (action == L"getSpirits") {
        HandleSpiritCollectGetSpiritsCommand();
    } else if (action == L"verifyPlayer") {
        HandleSpiritCollectVerifyPlayerCommand(msg);
    } else if (action == L"sendSpirit") {
        HandleSpiritCollectSendSpiritCommand(msg);
    } else if (action == L"history") {
        HandleSpiritCollectHistoryCommand(msg);
    }
}

void CopyLoginKeyToClipboard() {
    const std::wstring loginKey = GetLoginKey();
    if (loginKey.empty() || !OpenClipboard(nullptr)) {
        return;
    }

    EmptyClipboard();
    const size_t len = (loginKey.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (hMem) {
        void* locked = GlobalLock(hMem);
        if (locked) {
            memcpy(locked, loginKey.c_str(), len);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        } else {
            GlobalFree(hMem);
        }
    }
    CloseClipboard();
}

void HandleWindowDragCommand() {
    HandleActionCommand(AppHost::BeginWindowDrag);
}

void HandleWindowMinimizeCommand() {
    HandleActionCommand(AppHost::MinimizeMainWindow);
}

void HandleWindowCloseCommand() {
    HandleActionCommand(AppHost::CloseMainWindow);
}

void HandleSetBrowserWindowVisibleCommand(bool visible) {
    HandleActionCommand([visible]() {
        AppHost::ShowBrowserWindow(visible);
    });
}

void HandleHideBrowserWindowCommand() {
    HandleSetBrowserWindowVisibleCommand(false);
}

void HandleShowBrowserWindowCommand() {
    HandleSetBrowserWindowVisibleCommand(true);
}

void HandleOpenUrlCommand(const std::wstring& msg) {
    std::wstring url = GetJsonValue(msg, L"url");
    if (!url.empty()) {
        HandleActionCommand([url]() {
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        });
    }
}

void HandleRefreshGameCommand() {
    HandleActionCommand([]() {
        AppHost::NavigateBrowser(L"http://news.4399.com/login/kbxy.html");
    });
}

void HandleRefreshNoLoginCommand() {
    HandleActionCommand(AppHost::RefreshBrowser);
}

void HandleMuteGameCommand() {
    HandleActionCommand(AppHost::ToggleProgramVolume);
}

void HandleClearIeCacheCommand() {
    HandleActionCommand(AppHost::ClearIECache);
}

void HandleCopyLoginKeyCommand() {
    HandleActionCommand(CopyLoginKeyToClipboard);
}

void HandleKeyLoginCommand(const std::wstring& msg) {
    const std::wstring key = GetJsonValue(msg, L"key");
    if (!key.empty()) {
        HandleActionCommand([key]() {
            AppHost::NavigateBrowser(BuildLoginUrl(key));
        });
    }
}

void HandleAddHijackRuleCommand(const std::wstring& msg) {
    const bool isSend = GetJsonBoolValue(msg, L"isSend");
    const bool isBlock = GetJsonBoolValue(msg, L"isBlock");
    AddHijackRule(isBlock ? HIJACK_BLOCK : HIJACK_REPLACE,
                  isSend,
                  !isSend,
                  WideToUtf8(GetJsonValue(msg, L"pattern")),
                  WideToUtf8(GetJsonValue(msg, L"replace")));
}

void ParseUInt32Array(const std::wstring& msg, const std::wstring& key, std::vector<uint32_t>& values) {
    const size_t keyPos = msg.find(L"\"" + key + L"\"");
    if (keyPos == std::wstring::npos) {
        return;
    }

    const size_t colonPos = msg.find(L":", keyPos);
    const size_t leftBracket = msg.find(L"[", colonPos);
    const size_t rightBracket = msg.find(L"]", leftBracket);
    if (leftBracket == std::wstring::npos || rightBracket == std::wstring::npos) {
        return;
    }

    const std::wstring arr = msg.substr(leftBracket + 1, rightBracket - leftBracket - 1);
    size_t itemStart = 0;
    while (itemStart <= arr.length()) {
        const size_t itemEnd = arr.find(L',', itemStart);
        std::wstring num = (itemEnd == std::wstring::npos)
            ? arr.substr(itemStart)
            : arr.substr(itemStart, itemEnd - itemStart);
        itemStart = (itemEnd == std::wstring::npos) ? (arr.length() + 1) : (itemEnd + 1);

        const size_t start = num.find_first_not_of(L" \t\r\n");
        if (start == std::wstring::npos) {
            continue;
        }
        const size_t end = num.find_last_not_of(L" \t\r\n");
        const int value = _wtoi(num.substr(start, end - start + 1).c_str());
        if (value >= 0) {
            values.push_back(static_cast<uint32_t>(value));
        }
    }
}

void ParseIndicesArray(const std::wstring& msg, std::vector<DWORD>& indices) {
    std::vector<uint32_t> values;
    ParseUInt32Array(msg, L"indices", values);
    for (uint32_t value : values) {
        indices.push_back(static_cast<DWORD>(value));
    }
}

class WebMessageHandler : public ICoreWebView2WebMessageReceivedEventHandler {
public:
    WebMessageHandler() : m_refCount(1) {}

    ULONG STDMETHODCALLTYPE AddRef() override {
        return InterlockedIncrement(&m_refCount);
    }

    ULONG STDMETHODCALLTYPE Release() override {
        ULONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) {
            delete this;
        }
        return count;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        (void)sender;
        if (!args) {
            return E_POINTER;
        }

        PWSTR message = nullptr;
        const HRESULT hr = args->get_WebMessageAsJson(&message);
        if (FAILED(hr) || !message) {
            return hr;
        }

        std::wstring msg = UnescapeSerializedWebMessage(std::wstring(message));
        CoTaskMemFree(message);

        if (msg.find(L"window-drag") != std::wstring::npos) {
            HandleWindowDragCommand();
        } else if (msg.find(L"window-minimize") != std::wstring::npos) {
            HandleWindowMinimizeCommand();
        } else if (msg.find(L"window-close") != std::wstring::npos) {
            HandleWindowCloseCommand();
        } else if (msg.find(L"update-dialog-show") != std::wstring::npos) {
            HandleHideBrowserWindowCommand();
        } else if (msg.find(L"update-dialog-hide") != std::wstring::npos) {
            HandleShowBrowserWindowCommand();
        } else if (msg.find(L"open-url") != std::wstring::npos) {
            HandleOpenUrlCommand(msg);
        } else if (msg.find(L"refresh-game") != std::wstring::npos) {
            HandleRefreshGameCommand();
        } else if (msg.find(L"refresh-no-login") != std::wstring::npos) {
            HandleRefreshNoLoginCommand();
        } else if (msg.find(L"mute-game") != std::wstring::npos) {
            HandleMuteGameCommand();
        } else if (msg.find(L"clear-ie-cache") != std::wstring::npos) {
            HandleClearIeCacheCommand();
        } else if (msg.find(L"copy-login-key") != std::wstring::npos) {
            HandleCopyLoginKeyCommand();
        } else if (msg.find(L"key-login-dialog-show") != std::wstring::npos) {
            HandleHideBrowserWindowCommand();
        } else if (msg.find(L"key-login-dialog-hide") != std::wstring::npos) {
            HandleShowBrowserWindowCommand();
        } else if (msg.find(L"spirit-confirm-dialog-show") != std::wstring::npos) {
            HandleHideBrowserWindowCommand();
        } else if (msg.find(L"spirit-confirm-dialog-hide") != std::wstring::npos) {
            HandleShowBrowserWindowCommand();
        } else if (msg.find(L"key-login") != std::wstring::npos) {
            HandleKeyLoginCommand(msg);
        } else if (msg.find(L"packet_window_visible") != std::wstring::npos) {
            HandlePacketWindowVisibilityChanged(msg);
        } else if (msg.find(L"delete_selected_packets") != std::wstring::npos) {
            HandleDeleteSelectedPacketsCommand(msg);
        } else if (msg.find(L"clear_packets") != std::wstring::npos) {
            HandleClearPacketListCommand();
        } else if (msg.find(L"start_intercept") != std::wstring::npos) {
            HandleStartInterceptCommand();
        } else if (msg.find(L"stop_intercept") != std::wstring::npos) {
            HandleStopInterceptCommand();
        } else if (msg.find(L"set_intercept_type") != std::wstring::npos) {
            HandleSetInterceptTypeCommand(msg);
        } else if (msg.find(L"send_packet") != std::wstring::npos) {
            HandleSendRawPacketCommand(msg);
        } else if (msg.find(L"set_speed") != std::wstring::npos) {
            HandleSetSpeedCommand(msg);
        } else if (msg.find(L"toggle_auto_heal") != std::wstring::npos) {
            HandleSetAutoHealEnabledCommand(msg);
        } else if (msg.find(L"set_block_battle") != std::wstring::npos) {
            HandleSetBlockBattleEnabledCommand(msg);
        } else if (msg.find(L"set_auto_go_home") != std::wstring::npos) {
            HandleSetAutoGoHomeEnabledCommand(msg);
        } else if (msg.find(L"query_lingyu") != std::wstring::npos) {
            HandleQueryLingyuCommand();
        } else if (msg.find(L"query_monsters") != std::wstring::npos) {
            HandleQueryMonstersCommand();
        } else if (msg.find(L"refresh_pack_items") != std::wstring::npos) {
            HandleRefreshPackItemsCommand();
        } else if (msg.find(L"buy_item") != std::wstring::npos) {
            HandleBuyItemCommand(msg);
        } else if (msg.find(L"use_item") != std::wstring::npos) {
            HandleUseItemCommand(msg);
        } else if (msg.find(L"daily_tasks") != std::wstring::npos) {
            HandleStartDailyTasksCommand(msg);
        } else if (msg.find(L"daily_run_competition") != std::wstring::npos) {
            HandleStartRunCompetitionCommand(msg);
        } else if (msg.find(L"daily_jinpingfu_wish") != std::wstring::npos) {
            HandleStartJinpingfuWishCommand(msg);
        } else if (msg.find(L"daily_knowledge_garden") != std::wstring::npos) {
            HandleStartKnowledgeGardenCommand(msg);
        } else if (msg.find(L"daily_gomoku") != std::wstring::npos) {
            HandleStartGomokuCommand(msg);
        } else if (msg.find(L"query_huangcheng_weiji_task_progress") != std::wstring::npos) {
            HandleQueryHuangchengWeijiTaskProgressCommand(msg);
        } else if (msg.find(L"start_huangcheng_weiji_task") != std::wstring::npos) {
            HandleStartHuangchengWeijiTaskCommand(msg);
        } else if (msg.find(L"stop_huangcheng_weiji_task") != std::wstring::npos) {
            HandleStopHuangchengWeijiTaskCommand();
        } else if (msg.find(L"stop_task_zone") != std::wstring::npos) {
            HandleStopTaskZoneCommand();
        } else if (msg.find(L"task_zone") != std::wstring::npos) {
            HandleStartTaskZoneCommand();
        } else if (msg.find(L"one_key_collect") != std::wstring::npos) {
            HandleStartOneKeyCollectCommand(msg);
        } else if (msg.find(L"buy_dice_18") != std::wstring::npos) {
            HandleBuyDice18Command();
        } else if (msg.find(L"buy_dice") != std::wstring::npos) {
            HandleBuyDiceCommand();
        } else if (msg.find(L"one_key_xuantta") != std::wstring::npos) {
            HandleStartOneKeyXuanttaCommand();
        } else if (msg.find(L"query_shuangtai") != std::wstring::npos) {
            HandleQueryShuangTaiMonstersCommand();
        } else if (msg.find(L"start_shuangtai") != std::wstring::npos) {
            HandleStartShuangTaiCommand(msg);
        } else if (msg.find(L"stop_shuangtai") != std::wstring::npos) {
            HandleStopShuangTaiCommand();
        } else if (msg.find(L"battlesix_auto_match") != std::wstring::npos) {
            HandleStartBattleSixAutoMatchCommand(msg);
        } else if (msg.find(L"battlesix_cancel_match") != std::wstring::npos) {
            HandleCancelBattlesixMatchCommand();
        } else if (msg.find(L"battlesix_set_auto_battle") != std::wstring::npos) {
            HandleSetBattleSixAutoBattleEnabledCommand(msg);
        } else if (msg.find(L"dungeon_jump_start") != std::wstring::npos) {
            HandleStartDungeonJumpCommand(msg);
        } else if (msg.find(L"dungeon_jump_stop") != std::wstring::npos) {
            HandleStopDungeonJumpCommand();
        } else if (msg.find(L"one_key_latest_batch") != std::wstring::npos) {
            // 批量通道：activity_ids 数组，C++ 侧单线程顺序执行到次数耗尽。
            HandleStartLatestActivityCommand(msg, 0);
        } else if (msg.find(L"stop_latest_activities") != std::wstring::npos) {
            HandleStopLatestActivitiesCommand();
        } else if (msg.find(L"one_key_latest647") != std::wstring::npos) {
            HandleStartLatestActivityCommand(msg, LatestAct::ACT_MAZE);
        } else if (msg.find(L"one_key_latest869") != std::wstring::npos) {
            HandleStartLatestActivityCommand(msg, LatestAct::ACT_TOUCH_STAR);
        } else if (msg.find(L"one_key_latest749") != std::wstring::npos) {
            HandleStartLatestActivityCommand(msg, LatestAct::ACT_EXPLORE);
        } else if (msg.find(L"one_key_latest873") != std::wstring::npos) {
            HandleStartLatestActivityCommand(msg, LatestAct::ACT_WORD_GAME);
        } else if (msg.find(L"one_key_latest742") != std::wstring::npos) {
            HandleStartLatestActivityCommand(msg, LatestAct::ACT_HULU);
        } else if (msg.find(L"one_key_latest721") != std::wstring::npos) {
            HandleStartLatestActivityCommand(msg, LatestAct::ACT_STAR_PRINCESS);
        } else if (msg.find(L"one_key_latest877") != std::wstring::npos) {
            HandleStartLatestActivityCommand(msg, LatestAct::ACT_FIND_CORNEL);
        } else if (msg.find(L"one_key_latest730") != std::wstring::npos) {
            HandleStartLatestActivityCommand(msg, LatestAct::ACT_FIGHT_EVILS);
        } else if (msg.find(L"one_key_latest880") != std::wstring::npos) {
            HandleStartLatestActivityCommand(msg, LatestAct::ACT_TREASURE);
        } else if (msg.find(L"refresh_activity_counts") != std::wstring::npos) {
            HandleRefreshActivityCountsCommand();
        } else if (msg.find(L"one_key_horse_competition") != std::wstring::npos) {
            HandleStartOneKeyHorseCompetitionCommand();
        } else if (msg.find(L"stop_horse_competition") != std::wstring::npos) {
            HandleStopHorseCompetitionCommand();
        } else if (msg.find(L"start_heaven_furui") != std::wstring::npos) {
            HandleStartHeavenFuruiCommand(msg);
        } else if (msg.find(L"stop_heaven_furui") != std::wstring::npos) {
            HandleStopHeavenFuruiCommand();
        } else if (msg.find(L"decompose_lingyu") != std::wstring::npos) {
            HandleDecomposeLingyuCommand(msg);
        } else if (msg.find(L"set_hijack_enabled") != std::wstring::npos) {
            HandleSetHijackEnabledCommand(msg);
        } else if (msg.find(L"add_hijack_rule") != std::wstring::npos) {
            HandleAddHijackRuleCommand(msg);
        } else if (msg.find(L"clear_hijack_rules") != std::wstring::npos) {
            HandleClearHijackRulesCommand();
        } else if (msg.find(L"save_packet_list") != std::wstring::npos) {
            HandleSavePacketListCommand();
        } else if (msg.find(L"load_packet_list") != std::wstring::npos) {
            HandleLoadPacketListCommand();
        } else if (msg.find(L"send_all_packets") != std::wstring::npos) {
            HandleStartSendAllPacketsCommand(msg);
        } else if (msg.find(L"stop_send") != std::wstring::npos) {
            HandleStopSendCommand();
        } else if (msg.find(L"enter_boss_battle") != std::wstring::npos) {
            HandleEnterBossBattleCommand(msg);
        } else if (msg.find(L"spiritCollect") != std::wstring::npos) {
            HandleSpiritCollectCommand(msg);
        }

        return S_OK;
    }

private:
    ULONG m_refCount;
};

}  // namespace

ICoreWebView2WebMessageReceivedEventHandler* CreateWebMessageHandler() {
    return new WebMessageHandler();
}
