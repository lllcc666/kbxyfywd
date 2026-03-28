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
bool PostScriptToUI(const std::wstring& jsCode);
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

void SetHelperText(const wchar_t* text) {
    PostScriptToUI(L"if(window.updateHelperText) { window.updateHelperText('" + UIBridge::EscapeJsonString(text ? std::wstring(text) : std::wstring()) + L"'); }");
}

void SetHelperText(const std::wstring& text) {
    PostScriptToUI(L"if(window.updateHelperText) { window.updateHelperText('" + UIBridge::EscapeJsonString(text) + L"'); }");
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
            AppHost::BeginWindowDrag();
        } else if (msg.find(L"window-minimize") != std::wstring::npos) {
            AppHost::MinimizeMainWindow();
        } else if (msg.find(L"window-close") != std::wstring::npos) {
            AppHost::CloseMainWindow();
        } else if (msg.find(L"update-dialog-show") != std::wstring::npos) {
            AppHost::ShowBrowserWindow(false);
        } else if (msg.find(L"update-dialog-hide") != std::wstring::npos) {
            AppHost::ShowBrowserWindow(true);
        } else if (msg.find(L"open-url") != std::wstring::npos) {
            std::wstring url = GetJsonValue(msg, L"url");
            if (!url.empty()) ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else if (msg.find(L"refresh-game") != std::wstring::npos) {
            AppHost::NavigateBrowser(L"http://news.4399.com/login/kbxy.html");
        } else if (msg.find(L"refresh-no-login") != std::wstring::npos) {
            AppHost::RefreshBrowser();
        } else if (msg.find(L"mute-game") != std::wstring::npos) {
            AppHost::ToggleProgramVolume();
        } else if (msg.find(L"clear-ie-cache") != std::wstring::npos) {
            AppHost::ClearIECache();
        } else if (msg.find(L"copy-login-key") != std::wstring::npos) {
            CopyLoginKeyToClipboard();
        } else if (msg.find(L"key-login-dialog-show") != std::wstring::npos) {
            AppHost::ShowBrowserWindow(false);
        } else if (msg.find(L"key-login-dialog-hide") != std::wstring::npos) {
            AppHost::ShowBrowserWindow(true);
        } else if (msg.find(L"spirit-confirm-dialog-show") != std::wstring::npos) {
            AppHost::ShowBrowserWindow(false);
        } else if (msg.find(L"spirit-confirm-dialog-hide") != std::wstring::npos) {
            AppHost::ShowBrowserWindow(true);
        } else if (msg.find(L"key-login") != std::wstring::npos) {
            const std::wstring key = GetJsonValue(msg, L"key");
            if (!key.empty()) AppHost::NavigateBrowser(BuildLoginUrl(key));
        } else if (msg.find(L"packet_window_visible") != std::wstring::npos) {
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
                const std::wstring js = L"if(window.updatePacketCount) { window.updatePacketCount(" + std::to_wstring(GetPacketCount()) + L"); }";
                AppHost::SetPacketWindowVisible(false);
                AppHost::ExecuteScript(js);
            }
            AppHost::UpdateBrowserRegion();
        } else if (msg.find(L"delete_selected_packets") != std::wstring::npos) {
            std::vector<DWORD> indices;
            ParseIndicesArray(msg, indices);
            if (!indices.empty()) {
                DeleteSelectedPackets(indices);
            }
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
            SyncPacketsToUI();
        } else if (msg.find(L"clear_packets") != std::wstring::npos) {
            ClearPacketList();
        } else if (msg.find(L"start_intercept") != std::wstring::npos) {
            StartIntercept();
        } else if (msg.find(L"stop_intercept") != std::wstring::npos) {
            StopIntercept();
        } else if (msg.find(L"set_intercept_type") != std::wstring::npos) {
            SetInterceptType(GetJsonBoolValue(msg, L"send"), GetJsonBoolValue(msg, L"recv"));
        } else if (msg.find(L"send_packet") != std::wstring::npos) {
            const std::wstring hexW = GetJsonValue(msg, L"hex");
            if (!hexW.empty()) {
                struct SendThreadData { std::vector<BYTE> data; };
                SendThreadData* pData = new SendThreadData{StringToHex(WideToUtf8(hexW))};
                CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                    SendThreadData* pD = static_cast<SendThreadData*>(lpParam);
                    BOOL result = SendPacket(0, pD->data.data(), static_cast<DWORD>(pD->data.size()));
                    if (!result) {
                        PostScriptToUI(L"if(window.updateHelperText) { window.updateHelperText('封包发送失败：未连接到游戏服务器'); }");
                    }
                    delete pD;
                    return 0;
                }, pData, 0, nullptr);
            } else {
                PostScriptToUI(L"if(window.updateHelperText) { window.updateHelperText('封包数据格式错误'); }");
            }
        } else if (msg.find(L"set_speed") != std::wstring::npos) {
            AppHost::ApplySpeedhack(static_cast<float>(_wtof(GetTrimmedJsonValue(msg, L"speed").c_str())));
        } else if (msg.find(L"toggle_auto_heal") != std::wstring::npos) {
            g_autoHeal = GetJsonBoolValue(msg, L"enabled");
        } else if (msg.find(L"set_block_battle") != std::wstring::npos) {
            g_blockBattle = GetJsonBoolValue(msg, L"enabled");
        } else if (msg.find(L"set_auto_go_home") != std::wstring::npos) {
            g_autoGoHome = GetJsonBoolValue(msg, L"enabled");
        } else if (msg.find(L"query_lingyu") != std::wstring::npos) {
            SendQueryLingyuPacket();
        } else if (msg.find(L"query_monsters") != std::wstring::npos) {
            SendQueryMonsterPacket();
        } else if (msg.find(L"refresh_pack_items") != std::wstring::npos) {
            SendReqPackageDataPacket(0xFFFFFFFF);
        } else if (msg.find(L"buy_item") != std::wstring::npos) {
            const uint32_t itemId = GetJsonUInt32Value(msg, L"itemId");
            const uint32_t count = GetJsonUInt32Value(msg, L"count", 1U);
            if (itemId > 0) {
                SendReqPackageDataPacket(0xFFFFFFFF);
                Sleep(200);
                if (SendBuyGoodsPacket(itemId, count)) {
                    wchar_t buffer[128];
                    swprintf_s(buffer, L"购买道具成功: %s x%u", GetItemName(itemId).c_str(), count);
                    SetHelperText(buffer);
                }
            }
        } else if (msg.find(L"use_item") != std::wstring::npos) {
            const uint32_t itemId = GetJsonUInt32Value(msg, L"itemId");
            if (itemId > 0) {
                if (!g_battleStarted) {
                    PostScriptToUI(L"if(window.updateHelperText) { window.updateHelperText('使用道具需要在战斗中进行'); }");
                } else {
                    SendReqPackageDataPacket(0xFFFFFFFF);
                    Sleep(200);
                    if (SendUseItemInBattlePacket(itemId)) {
                        wchar_t buffer[128];
                        swprintf_s(buffer, L"使用道具: %s", GetItemName(itemId).c_str());
                        PostScriptToUI(L"if(window.updateHelperText) { window.updateHelperText('" + std::wstring(buffer) + L"'); }");
                    } else {
                        PostScriptToUI(L"if(window.updateHelperText) { window.updateHelperText('使用道具失败，背包中无此道具'); }");
                    }
                }
            }
        } else if (msg.find(L"daily_tasks") != std::wstring::npos) {
            SendDailyTasksAsync(GetJsonDWORDValue(msg, L"flags"));
        } else if (msg.find(L"one_key_collect") != std::wstring::npos) {
            const DWORD flags = GetJsonDWORDValue(msg, L"flags");
            if (SendOneKeyCollectPacket(flags)) {
                SetHelperText(L"一键采集已开始，请查看辅助日志");
            } else {
                SetHelperText(L"一键采集启动失败，可能已经在运行或未进入游戏");
            }
        } else if (msg.find(L"buy_dice_18") != std::wstring::npos) {
            if (SendBuyDicePacket()) SetHelperText(L"已购买18个骰子");
        } else if (msg.find(L"buy_dice") != std::wstring::npos) {
            SendBuyDicePacket();
        } else if (msg.find(L"one_key_xuantta") != std::wstring::npos) {
            if (SendOneKeyTowerPacket()) {
                SetHelperText(L"一键玄塔已开始，请查看辅助日志");
            } else {
                SetHelperText(L"一键玄塔启动失败，可能已经在运行或未进入游戏");
            }
        } else if (msg.find(L"query_shuangtai") != std::wstring::npos) {
            QueryShuangTaiMonsters();
        } else if (msg.find(L"start_shuangtai") != std::wstring::npos) {
            if (SendOneKeyShuangTaiPacket(GetJsonBoolValue(msg, L"blockBattle"))) {
                SetHelperText(L"双台谷刷级已开始，请查看辅助日志");
            } else {
                SetHelperText(L"双台谷刷级启动失败，请先点击查询按钮获取妖怪数据");
            }
        } else if (msg.find(L"stop_shuangtai") != std::wstring::npos) {
            StopShuangTai();
            SetHelperText(L"双台谷刷级已停止");
        } else if (msg.find(L"one_key_strawberry") != std::wstring::npos) {
            const bool useSweep = GetJsonBoolValue(msg, L"sweep");
            if (SendOneKeyStrawberryPacket(useSweep)) {
                SetHelperText(useSweep ? L"采摘红莓果已开始（扫荡模式），请查看辅助日志" : L"采摘红莓果已开始，请查看辅助日志");
            } else {
                SetHelperText(L"采摘红莓果启动失败，可能已经在运行或未进入游戏");
            }
        } else if (msg.find(L"battlesix_auto_match") != std::wstring::npos) {
            const std::wstring matchCountStr = GetTrimmedJsonValue(msg, L"matchCount");
            int matchCount = 1;
            TryParseIntInRangeLocal(matchCountStr, 1, 999, 1, matchCount);
            wchar_t startMsg[128];
            swprintf_s(startMsg, L"万妖盛会：开始匹配（共%d次）...", matchCount);
            SetHelperText(startMsg);
            int* pMatchCount = new int(matchCount);
            HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
                int count = *static_cast<int*>(param);
                delete static_cast<int*>(param);
                SendOneKeyBattleSixPacket(count);
                return 0;
            }, pMatchCount, 0, nullptr);
            if (hThread) CloseHandle(hThread);
        } else if (msg.find(L"battlesix_cancel_match") != std::wstring::npos) {
            g_battleSixAuto.SetAutoMatching(false);
            g_battleSixAuto.SetMatchCount(0);
            SetHelperText(SendCancelBattleSixPacket() ? L"万妖盛会：已取消匹配" : L"万妖盛会：取消匹配失败");
        } else if (msg.find(L"battlesix_set_auto_battle") != std::wstring::npos) {
            const std::wstring enabledStr = GetTrimmedJsonValue(msg, L"enabled");
            const bool enabled = (enabledStr == L"true");
            g_battleSixAuto.SetAutoBattle(enabled);
            SetHelperText(L"调试：enabled=[" + enabledStr + L"] parsed=" + std::wstring(enabled ? L"true" : L"false"));
            SetHelperText(enabled ? L"万妖盛会：自动战斗已启用" : L"万妖盛会：自动战斗已禁用");
        } else if (msg.find(L"dungeon_jump_start") != std::wstring::npos) {
            const std::wstring layerStr = GetTrimmedJsonValue(msg, L"targetLayer");
            int targetLayer = 1;
            TryParseIntInRangeLocal(layerStr, 1, 9999, 1, targetLayer);
            PostScriptToUI(L"if(window.updateDungeonJumpStatus) { window.updateDungeonJumpStatus('副本跳层：准备跳转到第" + std::to_wstring(targetLayer) + L"层...'); }");
            int* pTargetLayer = new int(targetLayer);
            HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
                int layer = *static_cast<int*>(param);
                delete static_cast<int*>(param);
                SendOneKeyDungeonJumpPacket(layer);
                return 0;
            }, pTargetLayer, 0, nullptr);
            if (hThread) CloseHandle(hThread);
        } else if (msg.find(L"dungeon_jump_stop") != std::wstring::npos) {
            StopDungeonJump();
            PostScriptToUI(L"if(window.updateDungeonJumpStatus) { window.updateDungeonJumpStatus('副本跳层：已停止'); }");
        } else if (msg.find(L"one_key_act793") != std::wstring::npos) {
            const bool useSweep = GetJsonBoolValue(msg, L"sweep");
            int targetMedals = Act793::TARGET_MEDALS;
            const std::wstring medalsStr = GetTrimmedJsonValue(msg, L"medals");
            if (!medalsStr.empty()) {
                TryParseIntInRangeLocal(medalsStr, 1, 100, Act793::TARGET_MEDALS, targetMedals);
            }
            if (SendOneKeyAct793Packet(useSweep, targetMedals)) {
                SetHelperText(useSweep ? L"磐石御天火已开始（扫荡模式），请查看辅助日志" : L"磐石御天火已开始（游戏模式），请查看辅助日志");
            } else {
                SetHelperText(L"磐石御天火启动失败");
            }
        } else if (msg.find(L"one_key_act791") != std::wstring::npos) {
            const bool useSweep = GetJsonBoolValue(msg, L"sweep");
            int targetScore = Act791::TARGET_SCORE;
            const std::wstring scoreStr = GetTrimmedJsonValue(msg, L"score");
            if (!scoreStr.empty()) {
                TryParseIntInRangeLocal(scoreStr, 1, 250, Act791::TARGET_SCORE, targetScore);
            }
            if (SendOneKeyAct791Packet(useSweep, targetScore)) {
                SetHelperText(useSweep ? L"五行镜破封印已开始（扫荡模式），请查看辅助日志" : L"五行镜破封印已开始（游戏模式），请查看辅助日志");
            } else {
                SetHelperText(L"五行镜破封印启动失败");
            }
        } else if (msg.find(L"one_key_act782") != std::wstring::npos) {
            const bool useSweep = GetJsonBoolValue(msg, L"sweep");
            if (SendOneKeyAct782Packet(useSweep, Act782::TARGET_SCORE)) {
                SetHelperText(useSweep ? L"摘取大力果实已开始（扫荡模式），请查看辅助日志" : L"摘取大力果实已开始（400分模式），请查看辅助日志");
            } else {
                SetHelperText(L"摘取大力果实启动失败");
            }
        } else if (msg.find(L"one_key_sea_battle") != std::wstring::npos) {
            const bool useSweep = GetJsonBoolValue(msg, L"sweep");
            if (SendOneKeySeaBattlePacket(useSweep)) {
                SetHelperText(useSweep ? L"海底激战已开始（扫荡模式），请查看辅助日志" : L"海底激战已开始（默认分数模式），请查看辅助日志");
            } else {
                SetHelperText(L"海底激战启动失败");
            }
        } else if (msg.find(L"one_key_horse_competition") != std::wstring::npos) {
            SetHelperText(L"坐骑大赛已开始（临时坐骑），请等待...");
            SetHorseProgressCallback([](const std::wstring& progress) {
                PostScriptToUI(L"if(window.updateHelperText) { window.updateHelperText('坐骑大赛：" + progress + L"'); }");
            });
            std::thread([]() {
                SendOneKeyHorseCompetitionPacket(true);
            }).detach();
        } else if (msg.find(L"stop_horse_competition") != std::wstring::npos) {
            RequestStopHorseCompetition();
        } else if (msg.find(L"start_heaven_furui") != std::wstring::npos) {
            int maxBoxes = 30;
            const std::wstring maxBoxesStr = GetTrimmedJsonValue(msg, L"maxBoxes");
            if (!maxBoxesStr.empty()) {
                TryParseIntInRangeLocal(maxBoxesStr, 1, 9999, 30, maxBoxes);
            }
            if (SendOneKeyHeavenFuruiPacket(maxBoxes)) {
                SetHelperText(L"福瑞宝箱：开始遍历地图查找宝箱...");
            } else {
                SetHelperText(L"福瑞宝箱启动失败");
            }
        } else if (msg.find(L"stop_heaven_furui") != std::wstring::npos) {
            StopHeavenFurui();
            SetHelperText(L"福瑞宝箱：已停止");
        } else if (msg.find(L"decompose_lingyu") != std::wstring::npos) {
            if (msg.find(L"decompose_lingyu_batch") != std::wstring::npos) {
                const std::wstring jsonArray = GetJsonValue(msg, L"indices");
                if (!jsonArray.empty()) {
                    SendDecomposeLingyuPacket(jsonArray);
                }
            } else {
                const std::wstring jsonArray = GetJsonValue(msg, L"indices_array");
                if (!jsonArray.empty()) {
                    SendDecomposeLingyuPacket(jsonArray);
                }
            }
        } else if (msg.find(L"set_hijack_enabled") != std::wstring::npos) {
            SetHijackEnabled(GetJsonBoolValue(msg, L"enabled"));
        } else if (msg.find(L"add_hijack_rule") != std::wstring::npos) {
            const bool isSend = GetJsonBoolValue(msg, L"isSend");
            const bool isBlock = GetJsonBoolValue(msg, L"isBlock");
            AddHijackRule(isBlock ? HIJACK_BLOCK : HIJACK_REPLACE,
                          isSend,
                          !isSend,
                          WideToUtf8(GetJsonValue(msg, L"pattern")),
                          WideToUtf8(GetJsonValue(msg, L"replace")));
        } else if (msg.find(L"clear_hijack_rules") != std::wstring::npos) {
            ClearHijackRules();
        } else if (msg.find(L"save_packet_list") != std::wstring::npos) {
            const std::wstring filePath = AppHost::ShowSaveFileDialog(L"", L"packets.txt");
            if (!filePath.empty()) {
                SetHelperText(SavePacketListToFile(filePath) ? L"封包列表已保存" : L"保存封包列表失败");
            }
        } else if (msg.find(L"load_packet_list") != std::wstring::npos) {
            const std::wstring filePath = AppHost::ShowOpenFileDialog(L"");
            if (!filePath.empty()) {
                if (LoadPacketListFromFile(filePath)) {
                    SyncPacketsToUI();
                    SetHelperText(L"封包列表已载入");
                } else {
                    SetHelperText(L"载入封包列表失败");
                }
            }
        } else if (msg.find(L"send_all_packets") != std::wstring::npos) {
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
            std::thread([sendCount, sendDelay]() {
                SendAllPackets(sendDelay, sendCount, [](DWORD currentLoop, DWORD packetIndex, const std::string& label) {
                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('正在发送第" +
                        std::to_wstring(currentLoop) + L"次，第" + std::to_wstring(packetIndex) + L"个封包";
                    if (!label.empty()) {
                        script += L"，标签：" + Utf8ToWide(label);
                    }
                    script += L"'); }";
                    PostScriptToUI(script);
                });
                PostScriptToUI(L"if(window.updateHelperText) { window.updateHelperText('封包发送完成'); }");
            }).detach();
        } else if (msg.find(L"stop_send") != std::wstring::npos) {
            StopAutoSend();
            SetHelperText(L"已停止发送封包");
        } else if (msg.find(L"enter_boss_battle") != std::wstring::npos) {
            const uint32_t spiritId = GetJsonUInt32Value(msg, L"bossId");
            if (spiritId > 10000) {
                if (SendBattlePacket(spiritId, 32, 0)) {
                    PostScriptToUI(L"if(window.updateHelperText) { window.updateHelperText('已发送BOSS战斗封包，ID: " + std::to_wstring(spiritId) + L", useId: 32'); }");
                } else {
                    SetHelperText(L"发送战斗封包失败，未连接到游戏服务器");
                }
            } else {
                SetHelperText(L"无效的对象ID，必须大于10000");
            }
        } else if (msg.find(L"spiritCollect") != std::wstring::npos) {
            // 精魄系统消息处理
            const std::wstring action = GetJsonValue(msg, L"action");
            if (action == L"open_ui") {
                // 打开精魄系统 UI
                if (SendSpiritOpenUIPacket()) {
                    SetHelperText(L"精魄系统：正在获取数据...");
                } else {
                    SetHelperText(L"精魄系统：发送请求失败");
                }
            } else if (action == L"getSpirits") {
                // 获取精魄列表
                if (SendSpiritPresuresPacket()) {
                    SetHelperText(L"精魄系统：正在获取精魄列表...");
                } else {
                    SetHelperText(L"精魄系统：获取精魄列表失败");
                }
            } else if (action == L"verifyPlayer") {
                // 验证玩家信息（通过卡布号）
                const std::wstring friendIdStr = GetTrimmedJsonValue(msg, L"friendId");
                if (!friendIdStr.empty()) {
                    uint32_t friendId = static_cast<uint32_t>(_wtol(friendIdStr.c_str()));
                    g_spiritCollectState.selectedFriendId = friendId;
                    if (SendSpiritPlayerInfoPacket(friendId)) {
                        SetHelperText(L"精魄系统：正在验证玩家信息...");
                    } else {
                        SetHelperText(L"精魄系统：验证玩家信息失败");
                    }
                }
            } else if (action == L"sendSpirit") {
                // 发送精魄
                const std::wstring friendIdStr = GetTrimmedJsonValue(msg, L"friendId");
                const std::wstring eggIdStr = GetTrimmedJsonValue(msg, L"eggId");
                if (!friendIdStr.empty() && !eggIdStr.empty()) {
                    uint32_t friendId = static_cast<uint32_t>(_wtol(friendIdStr.c_str()));
                    uint32_t eggId = static_cast<uint32_t>(_wtol(eggIdStr.c_str()));
                    if (SendSpiritGiftPacket(friendId, eggId)) {
                        SetHelperText(L"精魄系统：正在发送精魄...");
                    } else {
                        SetHelperText(L"精魄系统：发送精魄失败");
                    }
                }
            } else if (action == L"history") {
                // 获取历史记录
                const std::wstring typeStr = GetTrimmedJsonValue(msg, L"recordType");
                if (!typeStr.empty()) {
                    int type = _wtoi(typeStr.c_str());
                    if (SendSpiritHistoryPacket(type)) {
                        SetHelperText(L"精魄系统：正在获取历史记录...");
                    } else {
                        SetHelperText(L"精魄系统：获取历史记录失败");
                    }
                }
            }
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
