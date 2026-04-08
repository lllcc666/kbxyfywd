#include "web_message_handler.h"

#include <windows.h>
#include <WebView2.h>
#include <shellapi.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "battle_six.h"
#include "dungeon_jump.h"
#include "activity_minigames.h"
#include "horse_competition.h"
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

struct SendPacketThreadData {
    std::vector<BYTE> data;
};

template <typename ThreadData>
HANDLE LaunchDetachedWorkerThread(ThreadData* data, LPTHREAD_START_ROUTINE worker) {
    HANDLE hThread = CreateThread(nullptr, 0, worker, data, 0, nullptr);
    if (hThread) {
        CloseHandle(hThread);
    } else {
        delete data;
    }
    return hThread;
}

template <typename Func, typename... Args>
void LaunchDetachedStdThread(Func func, Args... args) {
    std::thread(func, args...).detach();
}

DWORD WINAPI HandleSendPacketWorker(LPVOID lpParam) {
    SendPacketThreadData* pD = static_cast<SendPacketThreadData*>(lpParam);
    BOOL result = SendPacket(0, pD->data.data(), static_cast<DWORD>(pD->data.size()));
    if (!result) {
        SetHelperText(L"封包发送失败：未连接到游戏服务器");
    }
    delete pD;
    return 0;
}

void HandleSendPacketCommand(const std::wstring& msg) {
    const std::wstring hexW = GetJsonValue(msg, L"hex");
    if (hexW.empty()) {
        SetHelperText(L"封包数据格式错误");
        return;
    }

    SendPacketThreadData* pData = new SendPacketThreadData{StringToHex(WideToUtf8(hexW))};
    LaunchDetachedWorkerThread(pData, HandleSendPacketWorker);
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

template <typename ActionFunc>
bool RefreshPackageDataAndRun(ActionFunc action) {
    SendReqPackageDataPacket(0xFFFFFFFF);
    Sleep(200);
    return action();
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

void HandleSendLingyuQueryCommand() {
    HandleActionCommand(SendQueryLingyuPacket);
}

void HandleSendMonsterQueryCommand() {
    HandleActionCommand(SendQueryMonsterPacket);
}

void HandleSendPackItemsRequestCommand() {
    HandleActionCommand([]() {
        SendReqPackageDataPacket(0xFFFFFFFF);
    });
}

void HandleSendBuyDiceCommand() {
    HandleActionCommand(SendBuyDicePacket);
}

void HandleBuyDice18Command() {
    HandleBoolResultCommand(SendBuyDicePacket, L"已购买18个骰子");
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

void HandleSendDailyTasksCommand(const std::wstring& msg) {
    const DWORD flags = GetJsonDWORDValue(msg, L"flags");
    HandleActionCommand([flags]() {
        SendDailyTasksAsync(flags);
    });
}

void HandleStopTaskZoneCommand() {
    HandleActionWithTextCommand(StopEightTrigramsTask, L"任务区已停止");
}

void HandleStartTaskZoneCommand() {
    HandleActionCommand(SendEightTrigramsTaskAsync);
}

void HandleSendShuangTaiQueryCommand() {
    HandleActionCommand(QueryShuangTaiMonsters);
}

void HandleStartShuangTaiCommand(const std::wstring& msg) {
    const bool blockBattle = GetJsonBoolValue(msg, L"blockBattle");
    HandleBoolResultCommand(
        [blockBattle]() {
            return SendOneKeyShuangTaiPacket(blockBattle);
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
            return SendCancelBattleSixPacket();
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
            return SendOneKeyHeavenFuruiPacket(maxBoxes);
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
            L"封包列表已载入",
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
            return SendOneKeyCollectPacket(flags);
        },
        L"一键采集已开始，请查看辅助日志",
        L"一键采集启动失败，可能已经在运行或未进入游戏");
}

void HandleStartOneKeyXuanttaCommand() {
    HandleBoolResultCommand(
        SendOneKeyTowerPacket,
        L"一键玄塔已开始，请查看辅助日志",
        L"一键玄塔启动失败，可能已经在运行或未进入游戏");
}

DWORD WINAPI HandleBattlesixAutoMatchWorker(LPVOID param);

void HandleStartBattlesixAutoMatchCommand(const std::wstring& msg) {
    const std::wstring matchCountStr = GetTrimmedJsonValue(msg, L"matchCount");
    int matchCount = 1;
    TryParseIntInRangeLocal(matchCountStr, 1, 999, 1, matchCount);
    wchar_t startMsg[128];
    swprintf_s(startMsg, L"万妖盛会：开始匹配（共%d次）...", matchCount);
    SetHelperText(startMsg);
    int* pMatchCount = new int(matchCount);
    LaunchDetachedWorkerThread(pMatchCount, HandleBattlesixAutoMatchWorker);
}

void HandleSetBattlesixAutoBattleCommand(const std::wstring& msg) {
    const std::wstring enabledStr = GetTrimmedJsonValue(msg, L"enabled");
    const bool enabled = (enabledStr == L"true");
    g_battleSixAuto.SetAutoBattle(enabled);
    SetHelperText(L"调试：enabled=[" + enabledStr + L"] parsed=" + std::wstring(enabled ? L"true" : L"false"));
    SetHelperText(enabled ? L"万妖盛会：自动战斗已启用" : L"万妖盛会：自动战斗已禁用");
}

DWORD WINAPI HandleBattlesixAutoMatchWorker(LPVOID param) {
    int* matchCount = static_cast<int*>(param);
    int count = *matchCount;
    delete matchCount;
    SendOneKeyBattleSixPacket(count);
    return 0;
}

DWORD WINAPI HandleDungeonJumpWorker(LPVOID param);

void HandleDungeonJumpStartCommand(const std::wstring& msg) {
    const std::wstring layerStr = GetTrimmedJsonValue(msg, L"targetLayer");
    int targetLayer = 1;
    TryParseIntInRangeLocal(layerStr, 1, 9999, 1, targetLayer);
    UpdateDungeonJumpStatus(std::wstring(L"副本跳层：准备跳转到第") + std::to_wstring(targetLayer) + L"层...");
    int* pTargetLayer = new int(targetLayer);
    LaunchDetachedWorkerThread(pTargetLayer, HandleDungeonJumpWorker);
}

void HandleDungeonJumpStopCommand() {
    StopDungeonJump();
    UpdateDungeonJumpStatus(L"副本跳层：已停止");
}

DWORD WINAPI HandleDungeonJumpWorker(LPVOID param) {
    int* targetLayer = static_cast<int*>(param);
    int layer = *targetLayer;
    delete targetLayer;
    SendOneKeyDungeonJumpPacket(layer);
    return 0;
}

void HandleHorseCompetitionProgressCommand(const std::wstring& progress) {
    SetHelperText(std::wstring(L"坐骑大赛：") + progress);
}

void HandleOneKeyHorseCompetitionWorker() {
    SendOneKeyHorseCompetitionPacket(true);
}

void HandleStartOneKeyHorseCompetitionCommand() {
    SetHelperText(L"坐骑大赛已开始（临时坐骑），请等待...");
    SetHorseProgressCallback(HandleHorseCompetitionProgressCommand);
    LaunchDetachedStdThread(HandleOneKeyHorseCompetitionWorker);
}

void HandleBuyItemCommand(const std::wstring& msg) {
    const uint32_t itemId = GetJsonUInt32Value(msg, L"itemId");
    const uint32_t count = GetJsonUInt32Value(msg, L"count", 1U);
    if (itemId > 0) {
        HandleBoolResultCommand(
            [itemId, count]() {
                return RefreshPackageDataAndRun([itemId, count]() {
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
                    return RefreshPackageDataAndRun([itemId]() {
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

void HandleSendAllPacketsProgressCommand(DWORD currentLoop, DWORD packetIndex, const std::string& label) {
    std::wstring message = L"正在发送第" + std::to_wstring(currentLoop) + L"次，第" + std::to_wstring(packetIndex) + L"个封包";
    if (!label.empty()) {
        message += L"，标签：" + Utf8ToWide(label);
    }
    SetHelperText(message);
}

void HandleSendAllPacketsWorker(DWORD sendCount, DWORD sendDelay) {
    SendAllPackets(sendDelay, sendCount, HandleSendAllPacketsProgressCommand);
    SetHelperText(L"封包发送完成");
}

template <typename SendFunc>
BOOL InvokeOneKeyActSend(SendFunc sendFunc, bool useSweep, int targetValue) {
    return sendFunc(useSweep, targetValue);
}

inline BOOL InvokeOneKeyActSend(BOOL (*sendFunc)(bool), bool useSweep, int) {
    return sendFunc(useSweep);
}

void HandleSendAllPacketsCommand(const std::wstring& msg) {
    DWORD sendCount = 1;
    DWORD sendDelay = 300;
    const std::wstring sendCountStr = GetJsonValue(msg, L"sendCount");
    if (!sendCountStr.empty()) {
        sendCount = static_cast<DWORD>(_wtoi(sendCountStr.c_str()));
        if (sendCount < 1) sendCount = 1;
    }
    const std::wstring sendDelayStr = GetJsonValue(msg, L"sendDelay");
    if (!sendDelayStr.empty()) {
        sendDelay = static_cast<DWORD>(_wtoi(sendDelayStr.c_str()));
    }
    LaunchDetachedStdThread(HandleSendAllPacketsWorker, sendCount, sendDelay);
}

template <typename SendFunc>
void HandleSendOneKeyActCommand(const std::wstring& msg,
                                const wchar_t* paramKey,
                                int defaultValue,
                                int minValue,
                                int maxValue,
                                const wchar_t* sweepText,
                                const wchar_t* gameText,
                                const wchar_t* failureText,
                                SendFunc sendFunc) {
    const bool useSweep = GetJsonBoolValue(msg, L"sweep");
    int targetValue = defaultValue;
    if (paramKey && *paramKey) {
        const std::wstring valueStr = GetTrimmedJsonValue(msg, paramKey);
        if (!valueStr.empty()) {
            TryParseIntInRangeLocal(valueStr, minValue, maxValue, defaultValue, targetValue);
        }
    }
    if (InvokeOneKeyActSend(sendFunc, useSweep, targetValue)) {
        SetHelperText(useSweep ? sweepText : gameText);
    } else {
        SetHelperText(failureText);
    }
}

void HandleOneKeyAct793Command(const std::wstring& msg) {
    HandleSendOneKeyActCommand(
        msg,
        L"medals",
        Act793::TARGET_MEDALS,
        1,
        100,
        L"磐石御天火已开始（扫荡模式），请查看辅助日志",
        L"磐石御天火已开始（游戏模式），请查看辅助日志",
        L"磐石御天火启动失败",
        SendOneKeyAct793Packet);
}

void HandleOneKeyAct791Command(const std::wstring& msg) {
    HandleSendOneKeyActCommand(
        msg,
        L"score",
        Act791::TARGET_SCORE,
        1,
        250,
        L"五行镜破封印已开始（扫荡模式），请查看辅助日志",
        L"五行镜破封印已开始（游戏模式），请查看辅助日志",
        L"五行镜破封印启动失败",
        SendOneKeyAct791Packet);
}

void HandleOneKeyAct782Command(const std::wstring& msg) {
    HandleSendOneKeyActCommand(
        msg,
        nullptr,
        Act782::TARGET_SCORE,
        0,
        0,
        L"摘取大力果实已开始（扫荡模式），请查看辅助日志",
        L"摘取大力果实已开始（400分模式），请查看辅助日志",
        L"摘取大力果实启动失败",
        SendOneKeyAct782Packet);
}

void HandleOneKeyAct803Command(const std::wstring& msg) {
    HandleSendOneKeyActCommand(
        msg,
        nullptr,
        Act803::MAX_NUM,
        0,
        0,
        L"清明赏河景已开始（扫荡模式）",
        L"清明赏河景已开始（游戏模式）",
        L"清明赏河景启动失败",
        SendOneKeyAct803Packet);
}

void HandleOneKeyAct624Command(const std::wstring& msg) {
    HandleSendOneKeyActCommand(
        msg,
        nullptr,
        0,
        0,
        0,
        L"采蘑菇的好伙伴已开始（扫荡模式）",
        L"采蘑菇的好伙伴已开始（三轮模式）",
        L"采蘑菇的好伙伴启动失败",
        SendOneKeyAct624Packet);
}

void HandleStartOneKeySeaBattleCommand(const std::wstring& msg) {
    HandleSendOneKeyActCommand(
        msg,
        nullptr,
        0,
        0,
        0,
        L"海底激战已开始（扫荡模式），请查看辅助日志",
        L"海底激战已开始（默认分数模式），请查看辅助日志",
        L"海底激战启动失败",
        SendOneKeySeaBattlePacket);
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
    if (g_loginKey.empty() || !OpenClipboard(nullptr)) {
        return;
    }

    EmptyClipboard();
    const size_t len = (g_loginKey.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (hMem) {
        void* locked = GlobalLock(hMem);
        if (locked) {
            memcpy(locked, g_loginKey.c_str(), len);
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

void ParseIndicesArray(const std::wstring& msg, std::vector<DWORD>& indices) {
    const size_t keyPos = msg.find(L"\"indices\"");
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
            indices.push_back(static_cast<DWORD>(value));
        }
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
            HandleSendPacketCommand(msg);
        } else if (msg.find(L"set_speed") != std::wstring::npos) {
            HandleSetSpeedCommand(msg);
        } else if (msg.find(L"toggle_auto_heal") != std::wstring::npos) {
            HandleSetAutoHealEnabledCommand(msg);
        } else if (msg.find(L"set_block_battle") != std::wstring::npos) {
            HandleSetBlockBattleEnabledCommand(msg);
        } else if (msg.find(L"set_auto_go_home") != std::wstring::npos) {
            HandleSetAutoGoHomeEnabledCommand(msg);
        } else if (msg.find(L"query_lingyu") != std::wstring::npos) {
            HandleSendLingyuQueryCommand();
        } else if (msg.find(L"query_monsters") != std::wstring::npos) {
            HandleSendMonsterQueryCommand();
        } else if (msg.find(L"refresh_pack_items") != std::wstring::npos) {
            HandleSendPackItemsRequestCommand();
        } else if (msg.find(L"buy_item") != std::wstring::npos) {
            HandleBuyItemCommand(msg);
        } else if (msg.find(L"use_item") != std::wstring::npos) {
            HandleUseItemCommand(msg);
        } else if (msg.find(L"daily_tasks") != std::wstring::npos) {
            HandleSendDailyTasksCommand(msg);
        } else if (msg.find(L"stop_task_zone") != std::wstring::npos) {
            HandleStopTaskZoneCommand();
        } else if (msg.find(L"task_zone") != std::wstring::npos) {
            HandleStartTaskZoneCommand();
        } else if (msg.find(L"one_key_collect") != std::wstring::npos) {
            HandleStartOneKeyCollectCommand(msg);
        } else if (msg.find(L"buy_dice_18") != std::wstring::npos) {
            HandleBuyDice18Command();
        } else if (msg.find(L"buy_dice") != std::wstring::npos) {
            HandleSendBuyDiceCommand();
        } else if (msg.find(L"one_key_xuantta") != std::wstring::npos) {
            HandleStartOneKeyXuanttaCommand();
        } else if (msg.find(L"query_shuangtai") != std::wstring::npos) {
            HandleSendShuangTaiQueryCommand();
        } else if (msg.find(L"start_shuangtai") != std::wstring::npos) {
            HandleStartShuangTaiCommand(msg);
        } else if (msg.find(L"stop_shuangtai") != std::wstring::npos) {
            HandleStopShuangTaiCommand();
        } else if (msg.find(L"battlesix_auto_match") != std::wstring::npos) {
            HandleStartBattlesixAutoMatchCommand(msg);
        } else if (msg.find(L"battlesix_cancel_match") != std::wstring::npos) {
            HandleCancelBattlesixMatchCommand();
        } else if (msg.find(L"battlesix_set_auto_battle") != std::wstring::npos) {
            HandleSetBattlesixAutoBattleCommand(msg);
        } else if (msg.find(L"dungeon_jump_start") != std::wstring::npos) {
            HandleDungeonJumpStartCommand(msg);
        } else if (msg.find(L"dungeon_jump_stop") != std::wstring::npos) {
            HandleDungeonJumpStopCommand();
        } else if (msg.find(L"one_key_act793") != std::wstring::npos) {
            HandleOneKeyAct793Command(msg);
        } else if (msg.find(L"one_key_act791") != std::wstring::npos) {
            HandleOneKeyAct791Command(msg);
        } else if (msg.find(L"one_key_act782") != std::wstring::npos) {
            HandleOneKeyAct782Command(msg);
        } else if (msg.find(L"one_key_act803") != std::wstring::npos) {
            HandleOneKeyAct803Command(msg);
        } else if (msg.find(L"one_key_act624") != std::wstring::npos) {
            HandleOneKeyAct624Command(msg);
        } else if (msg.find(L"one_key_sea_battle") != std::wstring::npos) {
            HandleStartOneKeySeaBattleCommand(msg);
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
            HandleSendAllPacketsCommand(msg);
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
