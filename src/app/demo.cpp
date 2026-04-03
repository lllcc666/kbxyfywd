#include <windows.h>
#include <dwmapi.h>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <WebView2.h>
#include <MinHook.h>
#include <MemoryModule.h>
#include <atlbase.h>
#include <atlwin.h>
#include <exdisp.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <commctrl.h>
#include <commdlg.h>
#include <winhttp.h>
#include <wininet.h>
#include <thread>

// 包含嵌入的WebView2Loader.dll数据
#include "embedded/webview2loader_data.h"                                 

// 包含嵌入的变速器DLL数据
#include "embedded/speed_x64_data.h"

// 包含嵌入的HTML内容头文件
#include "embedded/ui_html.h"

// 包含WPE Hook头文件
#include "wpe_hook.h"
#include "packet_parser.h"
#include "utils.h"
#include "ui_bridge.h"
#include "web_message_handler.h"
#include "window_messages.h"

// 初始化ATL模块
CComModule _Module;

// WebView2 环境和控制器指针
ICoreWebView2Environment* g_env = nullptr;
ICoreWebView2Controller* g_controller = nullptr;
ICoreWebView2* g_webview = nullptr;

// WebBrowser控件相关变量
CAxWindow g_axWindow;
CComPtr<IWebBrowser2> g_pWebBrowser;
HWND g_hwnd_webbrowser = nullptr;

// 主窗口句柄
HWND g_hWnd = nullptr;

// 加载遮罩窗口句柄
HWND g_hwnd_loading = nullptr;
UINT_PTR g_loading_timer = 0;
int g_loading_angle = 0;

// WebView2Loader.dll的内存加载相关
HMEMORYMODULE g_webView2LoaderModule = nullptr;

// 变速器相关
HMEMORYMODULE g_speedhackModule = nullptr;
typedef void (__stdcall *PFN_INITIALIZESPEEDHACK)(float);
PFN_INITIALIZESPEEDHACK g_pfnInitializeSpeedhack = nullptr;

// 钩子相关的原始地址
void* g_realGetTickCount64 = nullptr;
void* g_realGetTickCount = nullptr;
void* g_realQueryPerformanceCounter = nullptr;

// 版本检查相关常量
constexpr float CURRENT_VERSION = 1.07f;  // 当前版本：1.07
constexpr wchar_t VERSION_CHECK_URL[] = L"https://gitee.com/deepmoutains/kxby-release-detection/raw/master/data.txt";
constexpr wchar_t UPDATE_DOWNLOAD_URL[] = L"https://wwbov.lanzout.com/b03ancytve";

// MinHook库函数指针类型定义
typedef MH_STATUS(WINAPI* PFN_MH_INITIALIZE)(void);
typedef MH_STATUS(WINAPI* PFN_MH_UNINITIALIZE)(void);
typedef MH_STATUS(WINAPI* PFN_MH_CREATEHOOK)(LPVOID pTarget, LPVOID pDetour, LPVOID* ppOriginal);
typedef MH_STATUS(WINAPI* PFN_MH_ENABLEHOOK)(LPVOID pTarget);
typedef MH_STATUS(WINAPI* PFN_MH_DISABLEHOOK)(LPVOID pTarget);

// MinHook库函数指针声明
PFN_MH_INITIALIZE g_pfnMH_Initialize = nullptr;
PFN_MH_UNINITIALIZE g_pfnMH_Uninitialize = nullptr;
PFN_MH_CREATEHOOK g_pfnMH_CreateHook = nullptr;
PFN_MH_ENABLEHOOK g_pfnMH_EnableHook = nullptr;
PFN_MH_DISABLEHOOK g_pfnMH_DisableHook = nullptr;

// 远程DLL下载地址
// 加载变速器DLL
bool LoadSpeedhackFromMemory() {
    if (g_speedhackModule) return true; // 已经加载过了

    g_speedhackModule = MemoryLoadLibrary((const void*)g_SpeedX64Data, g_SpeedX64Size);
    if (!g_speedhackModule) {
        return false;
    }
    
    g_pfnInitializeSpeedhack = (PFN_INITIALIZESPEEDHACK)MemoryGetProcAddress(g_speedhackModule, "InitializeSpeedhack");
    
    // 获取 DLL 中的 hook 相关导出函数地址
    void* pSpeedhackGetTickCount64 = MemoryGetProcAddress(g_speedhackModule, "speedhackversion_GetTickCount64");
    void* pSpeedhackGetTickCount = MemoryGetProcAddress(g_speedhackModule, "speedhackversion_GetTickCount");
    void* pSpeedhackQueryPerformanceCounter = MemoryGetProcAddress(g_speedhackModule, "speedhackversion_QueryPerformanceCounter");

    // 获取 DLL 中的真实地址存储变量地址
    void** ppRealGetTickCount64 = (void**)MemoryGetProcAddress(g_speedhackModule, "realGetTickCount64");
    void** ppRealGetTickCount = (void**)MemoryGetProcAddress(g_speedhackModule, "realGetTickCount");
    void** ppRealQueryPerformanceCounter = (void**)MemoryGetProcAddress(g_speedhackModule, "realQueryPerformanceCounter");

    // 获取系统原始函数地址
    HMODULE hKernel32 = GetModuleHandleW(L"kernel32.dll");
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    HMODULE hWinmm = GetModuleHandleW(L"winmm.dll");

    void* pOrigGetTickCount64 = (void*)GetProcAddress(hKernel32, "GetTickCount64");
    void* pOrigGetTickCount = (void*)GetProcAddress(hKernel32, "GetTickCount");
    void* pOrigQueryPerformanceCounter = (void*)GetProcAddress(hNtdll, "RtlQueryPerformanceCounter");
    void* pOrigTimeGetTime = (void*)GetProcAddress(hWinmm, "timeGetTime");

    // 设置 DLL 内部的回调地址，防止 DLL 内部未正确初始化
    if (ppRealGetTickCount64) *ppRealGetTickCount64 = pOrigGetTickCount64;
    if (ppRealGetTickCount) *ppRealGetTickCount = pOrigGetTickCount;
    if (ppRealQueryPerformanceCounter) *ppRealQueryPerformanceCounter = pOrigQueryPerformanceCounter;

    // 使用 MinHook 进行 HOOK
    if (g_pfnMH_CreateHook && g_pfnMH_EnableHook) {
        if (pSpeedhackGetTickCount64 && pOrigGetTickCount64) {
            g_pfnMH_CreateHook(pOrigGetTickCount64, pSpeedhackGetTickCount64, (LPVOID*)ppRealGetTickCount64);
            g_pfnMH_EnableHook(pOrigGetTickCount64);
        }
        if (pSpeedhackGetTickCount && pOrigGetTickCount) {
            g_pfnMH_CreateHook(pOrigGetTickCount, pSpeedhackGetTickCount, (LPVOID*)ppRealGetTickCount);
            g_pfnMH_EnableHook(pOrigGetTickCount);
        }
        if (pSpeedhackQueryPerformanceCounter && pOrigQueryPerformanceCounter) {
            g_pfnMH_CreateHook(pOrigQueryPerformanceCounter, pSpeedhackQueryPerformanceCounter, (LPVOID*)ppRealQueryPerformanceCounter);
            g_pfnMH_EnableHook(pOrigQueryPerformanceCounter);
        }
        // timeGetTime 也跳转到 GetTickCount 的实现（参考类_变速.xc）
        if (pSpeedhackGetTickCount && pOrigTimeGetTime) {
            g_pfnMH_CreateHook(pOrigTimeGetTime, pSpeedhackGetTickCount, nullptr);
            g_pfnMH_EnableHook(pOrigTimeGetTime);
        }
    }

    return true;
}

// 游戏静音状态跟踪
std::atomic<bool> g_isGameMuted{false};

// 音频会话状态结构体
struct ProgramAudioSessionState {
    float originalVolume;  // 原始音量 (0.0 - 1.0)
    bool originalMuted;    // 原始静音状态
    bool isMuted;          // 当前静音状态
};

ProgramAudioSessionState g_audioSessionState = { 1.0f, false, false };

// WebView2窗口层级维护相关
HWND g_hwnd_webview = nullptr;
UINT_PTR g_zorder_timer_id = 0;
bool g_is_webview_topmost = false;

// 封包窗口状态
bool g_is_packet_window_visible = false;
RECT g_packet_window_rect = { 0 };

// 自定义消息：在UI线程执行JS

// 外部函数声明（来自 wpe_hook.cpp）
extern BOOL SavePacketListToFile(const std::wstring& filePath);
extern BOOL LoadPacketListFromFile(const std::wstring& filePath);

// 文件选择器辅助函数声明
std::wstring ShowSaveFileDialog(const std::wstring& initialDir = L"",
                                 const std::wstring& defaultFileName = L"",
                                 const std::wstring& filter = L"");
std::wstring ShowOpenFileDialog(const std::wstring& initialDir = L"",
                                 const std::wstring& filter = L"");

// 获取本程序的音频会话控制接口
bool GetProgramAudioSession(IAudioSessionControl** ppSessionControl) {
    HRESULT hr = S_OK;
    CComPtr<IMMDeviceEnumerator> pEnumerator;
    CComPtr<IMMDevice> pDevice;
    CComPtr<IAudioSessionManager2> pSessionManager2;
    CComPtr<IAudioSessionEnumerator> pSessionEnumerator;
    int sessionCount = 0;
    
    // 创建设备枚举器
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                         __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取默认音频输出设备
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取音频会话管理器
    hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
                          NULL, (void**)&pSessionManager2);
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取会话枚举器
    hr = pSessionManager2->GetSessionEnumerator(&pSessionEnumerator);
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取会话数量
    hr = pSessionEnumerator->GetCount(&sessionCount);
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取当前进程ID
    DWORD currentProcessId = GetCurrentProcessId();
    
    // 遍历所有音频会话，查找本程序的会话
    for (int i = 0; i < sessionCount; i++) {
        CComPtr<IAudioSessionControl> pSessionControl;
        CComPtr<IAudioSessionControl2> pSessionControl2;
        DWORD sessionProcessId = 0;
        
        hr = pSessionEnumerator->GetSession(i, &pSessionControl);
        if (FAILED(hr)) {
            continue;
        }
        
        // 尝试获取 IAudioSessionControl2 接口
        hr = pSessionControl->QueryInterface(__uuidof(IAudioSessionControl2), (void**)&pSessionControl2);
        if (FAILED(hr)) {
            continue;
        }
        
        // 获取会话所属的进程ID
        hr = pSessionControl2->GetProcessId(&sessionProcessId);
        if (FAILED(hr)) {
            continue;
        }
        
        // 如果找到本程序的会话
        if (sessionProcessId == currentProcessId) {
            *ppSessionControl = pSessionControl.Detach();
            return true;
        }
    }
    
    // 未找到本程序的音频会话
    return false;
}

// 安全地发送JS脚本到UI线程（统一使用 UI 桥接器）
bool PostScriptToUI(const std::wstring& jsCode) {
    if (jsCode.empty()) return false;

    if (!UIBridge::Instance().IsInitialized()) {
        return false;
    }

    UIBridge::Instance().ExecuteJS(jsCode);
    return true;
}

namespace {

constexpr DWORD IE_CACHE_ENTRY_TYPES =
    NORMAL_CACHE_ENTRY |
    STICKY_CACHE_ENTRY |
    TRACK_OFFLINE_CACHE_ENTRY |
    TRACK_ONLINE_CACHE_ENTRY;

bool CollectIECacheEntryUrls(std::vector<std::wstring>& cacheUrls) {
    DWORD entryInfoSize = 0;
    HANDLE findHandle = FindFirstUrlCacheEntryW(nullptr, nullptr, &entryInfoSize);
    if (!findHandle) {
        const DWORD lastError = GetLastError();
        if (lastError == ERROR_NO_MORE_ITEMS) {
            return true;
        }

        if (lastError != ERROR_INSUFFICIENT_BUFFER) {
            return false;
        }
    }

    std::vector<BYTE> entryBuffer(entryInfoSize);
    auto* entryInfo = reinterpret_cast<INTERNET_CACHE_ENTRY_INFOW*>(entryBuffer.data());
    findHandle = FindFirstUrlCacheEntryW(nullptr, entryInfo, &entryInfoSize);
    if (!findHandle) {
        return GetLastError() == ERROR_NO_MORE_ITEMS;
    }

    while (true) {
        if (entryInfo->lpszSourceUrlName != nullptr &&
            (entryInfo->CacheEntryType & IE_CACHE_ENTRY_TYPES) != 0) {
            cacheUrls.emplace_back(entryInfo->lpszSourceUrlName);
        }

        entryInfoSize = 0;
        if (FindNextUrlCacheEntryW(findHandle, nullptr, &entryInfoSize)) {
            continue;
        }

        DWORD lastError = GetLastError();
        if (lastError == ERROR_NO_MORE_ITEMS) {
            break;
        }

        if (lastError != ERROR_INSUFFICIENT_BUFFER) {
            FindCloseUrlCache(findHandle);
            return false;
        }

        entryBuffer.resize(entryInfoSize);
        entryInfo = reinterpret_cast<INTERNET_CACHE_ENTRY_INFOW*>(entryBuffer.data());
        if (!FindNextUrlCacheEntryW(findHandle, entryInfo, &entryInfoSize)) {
            lastError = GetLastError();
            if (lastError == ERROR_NO_MORE_ITEMS) {
                break;
            }

            FindCloseUrlCache(findHandle);
            return false;
        }
    }

    FindCloseUrlCache(findHandle);
    return true;
}

bool ClearIECacheGroups() {
    GROUPID groupId = 0;
    HANDLE findHandle = FindFirstUrlCacheGroup(0, CACHEGROUP_SEARCH_ALL, nullptr, 0, &groupId, nullptr);
    if (!findHandle) {
        return GetLastError() == ERROR_NO_MORE_ITEMS;
    }

    while (true) {
        DeleteUrlCacheGroup(groupId, CACHEGROUP_FLAG_FLUSHURL_ONDELETE, 0);

        if (!FindNextUrlCacheGroup(findHandle, &groupId, nullptr)) {
            const DWORD lastError = GetLastError();
            FindCloseUrlCache(findHandle);
            return lastError == ERROR_NO_MORE_ITEMS;
        }
    }
}

}  // namespace

bool ClearIECache() {
    std::vector<std::wstring> cacheUrls;
    const bool groupsCleared = ClearIECacheGroups();
    const bool entriesCollected = CollectIECacheEntryUrls(cacheUrls);

    if (entriesCollected) {
        for (const auto& cacheUrl : cacheUrls) {
            if (!cacheUrl.empty()) {
                DeleteUrlCacheEntryW(cacheUrl.c_str());
            }
        }
    }

    InternetSetOptionW(nullptr, INTERNET_OPTION_END_BROWSER_SESSION, nullptr, 0);

    const bool success = groupsCleared && entriesCollected;
    std::wstring jsCode = L"if(window.onClearIECacheFinished) { window.onClearIECacheFinished(";
    jsCode += success ? L"true" : L"false";
    jsCode += L"); }";
    PostScriptToUI(jsCode);

    return success;
}

// 使用Windows Session Audio API只静音本程序
bool ToggleProgramVolume() {
    HRESULT hr = S_OK;
    CComPtr<IAudioSessionControl> pSessionControl;
    CComPtr<ISimpleAudioVolume> pSimpleAudioVolume;
    BOOL bMuted = FALSE;
    float currentVolume = 0.0f;
    
    // 获取本程序的音频会话
    if (!GetProgramAudioSession(&pSessionControl)) {
        return false;
    }
    
    // 获取音量控制接口
    hr = pSessionControl->QueryInterface(__uuidof(ISimpleAudioVolume), (void**)&pSimpleAudioVolume);
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取当前音量
    hr = pSimpleAudioVolume->GetMasterVolume(&currentVolume);
    if (FAILED(hr)) {
        return false;
    }
    
    // 获取当前静音状态
    hr = pSimpleAudioVolume->GetMute(&bMuted);
    if (FAILED(hr)) {
        return false;
    }
    
    // 切换静音状态
    if (!g_isGameMuted) {
        // 当前未静音，保存原始状态并静音
        g_audioSessionState.originalVolume = currentVolume;
        g_audioSessionState.originalMuted = (bMuted != FALSE);
        
        // 设置音量为0或静音
        hr = pSimpleAudioVolume->SetMasterVolume(0.0f, NULL);
        if (FAILED(hr)) {
            return false;
        }
        
        g_isGameMuted = true;
    } else {
        // 当前已静音，恢复原始状态
        hr = pSimpleAudioVolume->SetMasterVolume(g_audioSessionState.originalVolume, NULL);
        if (FAILED(hr)) {
            return false;
        }
        
        hr = pSimpleAudioVolume->SetMute(g_audioSessionState.originalMuted, NULL);
        if (FAILED(hr)) {
            return false;
        }
        
        g_isGameMuted = false;
    }
    
    // 更新 JavaScript 端的静音按钮状态
    std::wstring jsCode = L"if(window.updateMuteButtonState) { window.updateMuteButtonState(";
    jsCode += g_isGameMuted ? L"true" : L"false";
    jsCode += L"); }";
    
    PostScriptToUI(jsCode);
    
    return true;
}

// 执行JavaScript脚本的全局函数
HRESULT WINAPI ExecuteScriptInWebView2(const WCHAR* script) {
    if (g_webview) {
        return g_webview->ExecuteScript(script, nullptr);
    }
    return E_FAIL;
}

// 版本检查相关函数
// 前向声明
std::wstring EscapeJsonString(const std::wstring& input);

struct VersionInfo {
    float latestVersion;
    std::wstring announcement;
    std::wstring updateContent;
};

namespace {

std::wstring TrimWhitespace(const std::wstring& value) {
    const size_t start = value.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) {
        return L"";
    }

    const size_t end = value.find_last_not_of(L" \t\r\n");
    return value.substr(start, end - start + 1);
}

bool TryParseIntInRange(const std::wstring& text, int minValue, int maxValue, int defaultValue, int& value) {
    const std::wstring trimmed = TrimWhitespace(text);
    if (trimmed.empty()) {
        value = defaultValue;
        return false;
    }

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

bool TryParseFloatValue(const std::wstring& text, float& value) {
    const std::wstring trimmed = TrimWhitespace(text);
    if (trimmed.empty()) {
        return false;
    }

    wchar_t* end = nullptr;
    float parsed = static_cast<float>(std::wcstod(trimmed.c_str(), &end));
    if (end == trimmed.c_str() || *end != L'\0') {
        return false;
    }

    value = parsed;
    return true;
}

}  // anonymous namespace

// 从文本内容中解析版本信息（Gitee data.txt 格式）
VersionInfo ParseVersionInfo(const std::wstring& content) {
    VersionInfo info = {CURRENT_VERSION, L"", L""};

    std::wstring currentSection;
    size_t lineStart = 0;

    while (lineStart <= content.length()) {
        size_t lineEnd = content.find(L'\n', lineStart);
        std::wstring line;
        if (lineEnd == std::wstring::npos) {
            line = content.substr(lineStart);
            lineStart = content.length() + 1;
        } else {
            line = content.substr(lineStart, lineEnd - lineStart);
            lineStart = lineEnd + 1;
        }

        // 去除行尾的\r（Windows换行符问题）
        if (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        
        if (line.find(L"最新版本") == 0) {
            // 解析版本号
            size_t pos = line.find(L"：");
            if (pos == std::wstring::npos) pos = line.find(L":");
            if (pos != std::wstring::npos) {
                std::wstring versionStr = line.substr(pos + 1);
                // 去除空白
                versionStr = TrimWhitespace(versionStr);
                if (!TryParseFloatValue(versionStr, info.latestVersion)) {
                    info.latestVersion = CURRENT_VERSION;
                }
            }
            currentSection = L"";
        } else if (line.find(L"版本公告") == 0) {
            currentSection = L"announcement";
            // 检查冒号后是否有内容
            size_t pos = line.find(L"：");
            if (pos == std::wstring::npos) pos = line.find(L":");
            if (pos != std::wstring::npos && pos + 1 < line.length()) {
                info.announcement = line.substr(pos + 1);
                info.announcement = TrimWhitespace(info.announcement);
            }
        } else if (line.find(L"更新内容") == 0) {
            currentSection = L"updateContent";
        } else if (currentSection == L"announcement") {
            // 版本公告的下一行内容
            if (!line.empty()) {
                if (info.announcement.empty()) {
                    info.announcement = line;
                }
            }
            currentSection = L"";
        } else if (currentSection == L"updateContent") {
            // 更新内容的多行内容
            if (line == L"---") {
                currentSection = L"";
            } else if (!line.empty()) {
                if (!info.updateContent.empty()) {
                    info.updateContent += L"\n";
                }
                info.updateContent += line;
            }
        }
    }

    return info;
}

// 异步检查版本更新
void CheckForUpdatesAsync() {
    std::thread([]() {
        HINTERNET hSession = nullptr;
        HINTERNET hConnect = nullptr;
        HINTERNET hRequest = nullptr;
        do {
            // 初始化 WinHTTP
            hSession = WinHttpOpen(
                L"KBWebUI/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );
            
            if (!hSession) break;
            
            // 设置超时时间（10秒）
            WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 10000);
            
            // 解析 URL
            URL_COMPONENTSW urlComp = {0};
            urlComp.dwStructSize = sizeof(URL_COMPONENTSW);
            urlComp.dwSchemeLength = (DWORD)-1;
            urlComp.dwHostNameLength = (DWORD)-1;
            urlComp.dwUrlPathLength = (DWORD)-1;
            
            if (!WinHttpCrackUrl(VERSION_CHECK_URL, 0, 0, &urlComp)) {
                break;
            }

            std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
            std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
            
            hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
            if (!hConnect) break;
            
            // 创建请求（支持 HTTPS）
            DWORD flags = 0;
            if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
                flags = WINHTTP_FLAG_SECURE;
            }
            
            hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(),
                nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

            if (!hRequest) break;

            // 设置安全选项，忽略 SSL 证书错误
            if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
                DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
                WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
            }

            // 发送请求
            if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                break;
            }
            
            // 接收响应
            if (!WinHttpReceiveResponse(hRequest, nullptr)) {
                break;
            }

            // 读取响应数据
            std::wstring response;
            DWORD bytesRead = 0;
            BYTE buffer[4096];
            
            while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                int len = MultiByteToWideChar(CP_UTF8, 0, (char*)buffer, bytesRead, nullptr, 0);
                if (len > 0) {
                    std::wstring temp(len, 0);
                    MultiByteToWideChar(CP_UTF8, 0, (char*)buffer, bytesRead, &temp[0], len);
                    response += temp;
                }
            }

            // 解析版本信息
            VersionInfo versionInfo = ParseVersionInfo(response);

            // 检查是否需要更新（使用容差避免浮点数精度问题）
            constexpr float VERSION_EPSILON = 0.001f;
            if (versionInfo.latestVersion > CURRENT_VERSION + VERSION_EPSILON) {
                // 需要更新，构造 JavaScript 代码显示更新对话框
                std::wstring script =
                    L"if(window.showUpdateDialog) { window.showUpdateDialog({"
                    L"version: " + std::to_wstring(versionInfo.latestVersion) +
                    L",announcement: \"" + UIBridge::EscapeJsonString(versionInfo.announcement) +
                    L"\",updateContent: \"" + UIBridge::EscapeJsonString(versionInfo.updateContent) +
                    L"\",downloadUrl: \"" + std::wstring(UPDATE_DOWNLOAD_URL) +
                    L"\"}); }";
                PostScriptToUI(script);
            }
            
        } while (false);

        // 清理资源
        if (hRequest) WinHttpCloseHandle(hRequest);
        if (hConnect) WinHttpCloseHandle(hConnect);
        if (hSession) WinHttpCloseHandle(hSession);
    }).detach();
}

// 回调函数：查找WebView2渲染窗口
BOOL CALLBACK EnumWebView2ChildWindowsProc(HWND hwnd, LPARAM lParam)
{
    TCHAR className[256];
    if (GetClassName(hwnd, className, sizeof(className)) > 0)
    {
        // 查找WebView2渲染窗口的类名
        if (_tcsstr(className, _T("Chrome_RenderWidgetHostHWND")) != NULL ||
            _tcsstr(className, _T("Chrome_WidgetWin_1")) != NULL)
        {
            HWND* render_hwnd = (HWND*)lParam;
            *render_hwnd = hwnd;
            return FALSE; // 找到目标窗口，停止枚举
        }
    }
    return TRUE; // 继续枚举
}

// 获取WebView2渲染窗口句柄
HWND GetWebView2RenderWindow()
{
    if (!g_hwnd_webview)
    {
        if (g_controller)
        {
            g_controller->get_ParentWindow(&g_hwnd_webview);
        }
    }
    
    if (g_hwnd_webview)
    {
        // 枚举WebView2父窗口的子窗口，查找渲染窗口
        HWND render_hwnd = NULL;
        EnumChildWindows(g_hwnd_webview, EnumWebView2ChildWindowsProc, (LPARAM)&render_hwnd);
        
        if (render_hwnd)
        {
            return render_hwnd;
        }
    }
    
    // 如果找不到渲染窗口，返回原始窗口句柄
    return g_hwnd_webview;
}

// 全局或类成员变量，记录渲染窗口句柄
HWND g_hWebViewRenderWnd = NULL;

// 查找 WebView2 渲染窗口的递归函数
BOOL CALLBACK FindRenderWindow(HWND hwnd, LPARAM lParam) {
    TCHAR className[256];
    GetClassName(hwnd, className, 256);
    if (_tcscmp(className, _T("Chrome_RenderWidgetHostHWND")) == 0) {
        if (lParam != 0) {
            // 通过参数传递窗口句柄
            *(HWND*)lParam = hwnd;
        }
        else {
            // 兼容旧代码，使用全局变量
            g_hWebViewRenderWnd = hwnd;
        }
        return FALSE; // 找到了，停止搜索
    }
    return TRUE;
}

// 使用SetWindowRgn调整IE窗口区域，创建'孔洞'显示WebView2弹窗
// 辅助函数：递归给所有子窗口设置区域
void SetWindowRgnRecursive(HWND hWnd, HRGN hRgn, BOOL bRedraw)
{
    if (!hWnd) return;
    
    // 注意：SetWindowRgn 会接管 hRgn 的所有权，所以我们不能在循环中重复使用同一个句柄
    // 我们需要为每个窗口创建一个拷贝
    
    // 先给当前窗口设置
    HRGN hRgnCopy = NULL;
    if (hRgn) {
        hRgnCopy = CreateRectRgn(0, 0, 0, 0);
        if (hRgnCopy) {
            CombineRgn(hRgnCopy, hRgn, NULL, RGN_COPY);
        }
    }
    SetWindowRgn(hWnd, hRgnCopy, bRedraw);

    // 遍历子窗口
    HWND hChild = GetWindow(hWnd, GW_CHILD);
    while (hChild) {
        SetWindowRgnRecursive(hChild, hRgn, bRedraw);
        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }
}

void UpdateIEEWindowRegion()
{
    if (!g_hwnd_webbrowser) {
        return;
    }
    
    // 使用动态解析的坐标进行打孔
    int hole_x = g_packet_window_rect.left;
    int hole_y = g_packet_window_rect.top - 32;  // 减去标题栏高度，因为封包窗口在IE控件下方
    int hole_w = g_packet_window_rect.right; // 这里在JS中传的是width
    int hole_h = g_packet_window_rect.bottom; // 这里在JS中传的是height

    if (!g_is_packet_window_visible) {
        // 恢复 IE 浏览框为完整矩形
        SetWindowRgnRecursive(g_hwnd_webbrowser, NULL, TRUE);
    } else {
        // 1. 获取 IE 窗口的客户区大小
        RECT ieClientRect;
        GetClientRect(g_hwnd_webbrowser, &ieClientRect);
        int ieWidth = ieClientRect.right;
        int ieHeight = ieClientRect.bottom;
        
        // 2. 创建一个覆盖整个 IE 窗口客户区的原始区域
        HRGN hFullRgn = CreateRectRgn(0, 0, ieWidth, ieHeight);
        
        // 3. 创建孔洞区域
        HRGN hHoleRgn = CreateRectRgn(hole_x, hole_y, hole_x + hole_w, hole_y + hole_h);
        
        // 4. 从完整区域中"减去"孔洞区域
        CombineRgn(hFullRgn, hFullRgn, hHoleRgn, RGN_DIFF);
        
        // 5. 应用这个带洞的区域到 IE 窗口及其所有子窗口
        SetWindowRgnRecursive(g_hwnd_webbrowser, hFullRgn, TRUE);
        
        // 强制刷新窗口
        RedrawWindow(g_hwnd_webbrowser, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
        
        DeleteObject(hHoleRgn);
        DeleteObject(hFullRgn); // 因为 SetWindowRgnRecursive 使用的是拷贝，所以这里需要释放原件
    }
}

// 简化的EnsureWebView2OnTop函数，只保留基本功能
void EnsureWebView2OnTop()
{
    if (g_controller) {
        HWND hWebView = nullptr;
        g_controller->get_ParentWindow(&hWebView);
        if (hWebView) {
            EnumChildWindows(hWebView, FindRenderWindow, (LPARAM)&g_hWebViewRenderWnd);
        }
    }
    
    if (g_hWebViewRenderWnd) {
        SetWindowPos(g_hWebViewRenderWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// WebView2 API 函数指针类型定义
typedef HRESULT(WINAPI* PFN_CREATECOREWEBVIEW2ENVIRONMENTWITHOPTIONS)(
    PCWSTR browserExecutableFolder,
    PCWSTR userDataFolder,
    ICoreWebView2EnvironmentOptions* environmentOptions,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* environmentCreatedHandler
);

// WebView2 API 函数指针
PFN_CREATECOREWEBVIEW2ENVIRONMENTWITHOPTIONS g_pfnCreateCoreWebView2EnvironmentWithOptions = nullptr;

// 从内存加载WebView2Loader.dll
bool LoadWebView2LoaderFromMemory()
{
    // 从嵌入的二进制数据加载WebView2Loader.dll
    g_webView2LoaderModule = MemoryLoadLibrary((const void*)g_WebView2LoaderData, g_WebView2LoaderSize);
    if (!g_webView2LoaderModule)
    {
        MessageBoxW(nullptr, L"Failed to load WebView2Loader.dll from memory", L"Error", MB_OK);
        return false;
    }
    
    // 获取函数指针
    g_pfnCreateCoreWebView2EnvironmentWithOptions = 
        (PFN_CREATECOREWEBVIEW2ENVIRONMENTWITHOPTIONS)MemoryGetProcAddress(g_webView2LoaderModule, "CreateCoreWebView2EnvironmentWithOptions");
    
    if (!g_pfnCreateCoreWebView2EnvironmentWithOptions)
    {
        MessageBoxW(nullptr, L"Failed to get CreateCoreWebView2EnvironmentWithOptions function", L"Error", MB_OK);
        MemoryFreeLibrary(g_webView2LoaderModule);
        g_webView2LoaderModule = nullptr;
        return false;
    }
    
    return true;
}

// 无边框模式标志
bool g_isBorderless = true;

// 加载遮罩窗口过程
LRESULT CALLBACK LoadingWindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_TIMER:
        if (wParam == 1)
        {
            // 更新旋转角度（更快）
            g_loading_angle = (g_loading_angle + 10) % 360;
            // 使用 FALSE 避免背景擦除闪烁
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        return 0;
        
    case WM_ERASEBKGND:
        // 返回 TRUE 防止背景擦除，避免闪烁
        return 1;
        
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            
            RECT rc;
            GetClientRect(hWnd, &rc);
            int width = rc.right - rc.left;
            int height = rc.bottom - rc.top;
            
            // 双缓冲：创建内存DC
            HDC hdcMem = CreateCompatibleDC(hdc);
            HBITMAP hBmpMem = CreateCompatibleBitmap(hdc, width, height);
            HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmpMem);
            
            // 绘制背景
            HBRUSH hBrush = CreateSolidBrush(RGB(248, 249, 250));
            FillRect(hdcMem, &rc, hBrush);
            DeleteObject(hBrush);
            
            // 绘制旋转的加载圆环
            int cx = width / 2;
            int cy = height / 2;
            int radius = 25;
            int dotRadius = 4;
            
            // 绘制12个点（更流畅）
            for (int i = 0; i < 12; i++)
            {
                int angle = g_loading_angle + i * 30;
                double rad = angle * 3.14159265 / 180.0;
                int x = cx + (int)(radius * cos(rad));
                int y = cy + (int)(radius * sin(rad));
                
                // 透明度渐变
                int alpha = 255 - i * 20;
                if (alpha < 30) alpha = 30;
                
                // 绘制圆点
                HBRUSH hDotBrush = CreateSolidBrush(RGB(0, 120, 212));
                
                // 使用 AlphaBlend 绘制带透明度的圆点
                HDC hdcDot = CreateCompatibleDC(hdcMem);
                HBITMAP hBmpDot = CreateCompatibleBitmap(hdcMem, dotRadius * 2, dotRadius * 2);
                HBITMAP hOldBmpDot = (HBITMAP)SelectObject(hdcDot, hBmpDot);
                
                RECT dotRc = {0, 0, dotRadius * 2, dotRadius * 2};
                FillRect(hdcDot, &dotRc, hDotBrush);
                
                BLENDFUNCTION bf = { AC_SRC_OVER, 0, (BYTE)alpha, 0 };
                AlphaBlend(hdcMem, x - dotRadius, y - dotRadius, dotRadius * 2, dotRadius * 2,
                          hdcDot, 0, 0, dotRadius * 2, dotRadius * 2, bf);
                
                SelectObject(hdcDot, hOldBmpDot);
                DeleteObject(hBmpDot);
                DeleteDC(hdcDot);
                DeleteObject(hDotBrush);
            }
            
            // 绘制加载文字
            SetBkMode(hdcMem, TRANSPARENT);
            SetTextColor(hdcMem, RGB(73, 73, 73));
            HFONT hFont = CreateFontW(16, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
            HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);
            
            const wchar_t* text = L"正在加载界面...";
            SIZE textSize;
            GetTextExtentPoint32W(hdcMem, text, (int)wcslen(text), &textSize);
            TextOutW(hdcMem, cx - textSize.cx / 2, cy + 45, text, (int)wcslen(text));
            
            SelectObject(hdcMem, hOldFont);
            DeleteObject(hFont);
            
            // 将内存DC的内容复制到屏幕
            BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);
            
            // 清理
            SelectObject(hdcMem, hOldBmp);
            DeleteObject(hBmpMem);
            DeleteDC(hdcMem);
            
            EndPaint(hWnd, &ps);
        }
        return 0;
        
    case WM_DESTROY:
        if (g_loading_timer)
        {
            KillTimer(hWnd, g_loading_timer);
            g_loading_timer = 0;
        }
        // 不要调用 PostQuitMessage，否则会关闭整个应用
        return 0;
    }
    
    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

// 创建加载遮罩窗口
void CreateLoadingOverlay()
{
    if (!g_hWnd) return;
    
    // 获取主窗口位置和大小
    RECT rcMain;
    GetWindowRect(g_hWnd, &rcMain);
    int width = rcMain.right - rcMain.left;
    int height = rcMain.bottom - rcMain.top;
    
    // 注册遮罩窗口类
    const wchar_t LOADING_CLASS[] = L"LoadingOverlayClass";
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc = LoadingWindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    wc.lpszClassName = LOADING_CLASS;
    RegisterClassExW(&wc);
    
    // 创建遮罩窗口（覆盖在主窗口上）
    g_hwnd_loading = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        LOADING_CLASS,
        L"",
        WS_POPUP,
        rcMain.left, rcMain.top, width, height,
        nullptr,
        nullptr,
        GetModuleHandleW(nullptr),
        nullptr
    );
    
    if (g_hwnd_loading)
    {
        // 设置透明度
        SetLayeredWindowAttributes(g_hwnd_loading, 0, 255, LWA_ALPHA);
        
        // 设置定时器更新动画
        g_loading_timer = SetTimer(g_hwnd_loading, 1, 16, nullptr);
        
        // 显示遮罩窗口
        ShowWindow(g_hwnd_loading, SW_SHOW);
        UpdateWindow(g_hwnd_loading);
    }
}

// 销毁加载遮罩窗口
void DestroyLoadingOverlay()
{
    if (g_hwnd_loading)
    {
        DestroyWindow(g_hwnd_loading);
        g_hwnd_loading = nullptr;
    }
}

// Win32 窗口过程函数
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_NCCALCSIZE:
        // 隐藏窗口边框但保留阴影
        // 返回 0 告诉系统不要绘制非客户区
        if (g_isBorderless && wParam) {
            return 0;
        }
        break;
    case WM_CLOSE:
        // 清理资源
        if (g_zorder_timer_id)
        {
            KillTimer(hWnd, g_zorder_timer_id);
            g_zorder_timer_id = 0;
        }
        DestroyWindow(hWnd);
        break;
    case WM_DESTROY:
        // 停止拦截并卸载Hook
        StopIntercept();
        UninitializeHooks();
        CleanupWpeHook();
        
        // 恢复 IE 浏览框区域
        if (g_hwnd_webbrowser) {
            SetWindowRgnRecursive(g_hwnd_webbrowser, NULL, TRUE);
        }
        
        // 释放 WebView2 资源
        if (g_controller) {
            g_controller->Close();
            g_controller->Release();
            g_controller = nullptr;
        }
        if (g_webview) {
            g_webview->Release();
            g_webview = nullptr;
        }
        if (g_env) {
            g_env->Release();
            g_env = nullptr;
        }
        
        // 释放内存加载的 DLL
        if (g_webView2LoaderModule) {
            MemoryFreeLibrary(g_webView2LoaderModule);
            g_webView2LoaderModule = nullptr;
        }
        
        PostQuitMessage(0);
        break;
    case WM_SIZE:
        if (g_controller != nullptr)
        {
            RECT bounds;
            GetClientRect(hWnd, &bounds);
            
            // WebView2覆盖整个主窗口，确保HTML元素可以显示在WebBrowser上方
            g_controller->put_Bounds(bounds);
        }
        break;
    case WM_TIMER:
        if (wParam == g_zorder_timer_id)
        {
            // 删除所有与WebView2层级设置相关的代码
            // 确保IE浏览框显示在WebView2上方
        }
        break;
    case AppMessage::kExecuteJs:
        if (lParam) {
            wchar_t* script = (wchar_t*)lParam;
            if (script) {
                ExecuteScriptInWebView2(script);
                delete[] script;
            }
        }
        break;
    case AppMessage::kDecomposeComplete:
        // 在UI线程中安全地调用分解完成的JavaScript回调
        ExecuteScriptInWebView2(L"if(window.onDecomposeComplete) window.onDecomposeComplete();");
        break;
    case AppMessage::kDecomposeHexPacket:
        // 清理十六进制封包数据内存
        {
            HexPacketData* hexData = (HexPacketData*)wParam;
            if (hexData) {
                delete[] hexData->data;
                delete hexData;
            }
        }
        break;
    case AppMessage::kDailyTaskComplete:
        // 日常活动完成通知
        {
            int completedCount = (int)wParam;
            int totalCount = (int)lParam;
            std::wstring script = L"if(window.onDailyTaskComplete) window.onDailyTaskComplete(" + 
                                  std::to_wstring(completedCount) + L", " + std::to_wstring(totalCount) + L");";
            ExecuteScriptInWebView2(script.c_str());
        }
        break;
    case WM_NCHITTEST:
        {
            // 获取鼠标位置（屏幕坐标）
            POINT pt;
            pt.x = GET_X_LPARAM(lParam);
            pt.y = GET_Y_LPARAM(lParam);
            
            // 转换为客户区坐标
            POINT clientPt = pt;
            ScreenToClient(hWnd, &clientPt);
            
            // 标题栏区域：顶部 32 像素
            const int TITLE_BAR_HEIGHT = 32;
            
            // 检查是否在标题栏区域（排除控制按钮区域）
            if (clientPt.y < TITLE_BAR_HEIGHT && clientPt.y >= 0)
            {
                // 检查是否在控制按钮区域（右侧）
                RECT clientRect;
                GetClientRect(hWnd, &clientRect);
                int buttonAreaStart = clientRect.right - 120;  // 控制按钮区域（最小化、关闭）
                
                if (clientPt.x < buttonAreaStart)
                {
                    return HTCAPTION;  // 返回标题栏，允许拖拽
                }
                else
                {
                    // 控制按钮区域，返回客户区让 WebView2 处理
                    return HTCLIENT;
                }
            }
            
            // 其他区域使用默认处理
            return DefWindowProcW(hWnd, uMsg, wParam, lParam);
        }
        break;
    default:
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

// WebView2 环境创建完成回调类
class CreateEnvironmentCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler
{
private:
    ULONG m_refCount;
public:
    CreateEnvironmentCompletedHandler() : m_refCount(1) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
    ULONG STDMETHODCALLTYPE Release() override { 
        ULONG count = InterlockedDecrement(&m_refCount);
        if (count == 0) {
            delete this;
        }
        return count;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override
    {
        if (FAILED(result))
        {
            MessageBoxW(nullptr, L"Failed to create WebView2 environment", L"Error", MB_OK);
            return result;
        }

        g_env = env;
        env->AddRef();

        // WebView2 控制器创建完成回调类
        class CreateControllerCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler
        {
        private:
            ULONG m_refCount;
        public:
            CreateControllerCompletedHandler() : m_refCount(1) {}
            ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
            ULONG STDMETHODCALLTYPE Release() override { 
                ULONG count = InterlockedDecrement(&m_refCount);
                if (count == 0) {
                    delete this;
                }
                return count;
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
                if (!ppvObject) return E_POINTER;
                if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
                    *ppvObject = this;
                    AddRef();
                    return S_OK;
                }
                return E_NOINTERFACE;
            }
            
            HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override
            {
                if (FAILED(result))
                {
                    MessageBoxW(nullptr, L"Failed to create WebView2 controller", L"Error", MB_OK);
                    return result;
                }

                g_controller = controller;
                controller->AddRef();

                // 获取 WebView2 界面
                ICoreWebView2* webview = nullptr;
                g_controller->get_CoreWebView2(&webview);
                g_webview = webview;
                webview->AddRef();

                // 将WebView2边界扩展到整个主窗口，确保HTML元素可以显示在WebBrowser上方
                RECT bounds;
                GetClientRect(g_hWnd, &bounds);
                
                // 恢复WebView2覆盖整个主窗口，确保HTML元素可以显示在WebBrowser上方
                g_controller->put_Bounds(bounds);
                
                // 获取WebView2窗口句柄并保存
                g_controller->get_ParentWindow(&g_hwnd_webview);
                
                // 设置WebView2渲染窗口的基本功能
                if (g_hwnd_webview)
                {
                    // 已移除冗余的透明度和子类化设置
                }
                
                // 尝试使用ICoreWebView2CompositionController接口
                // 这个接口允许更精细地控制WebView2的渲染区域和透明度
                ICoreWebView2CompositionController* composition_controller = nullptr;
                HRESULT hr = g_webview->QueryInterface(IID_PPV_ARGS(&composition_controller));
                if (SUCCEEDED(hr) && composition_controller)
                {
                    // 如果获取到接口，可以在这里添加相关代码
                    // 例如，设置渲染区域或透明度
                    // 但由于当前版本可能不支持，我们先不添加具体实现
                    // 后续可以根据实际需求和支持的版本添加代码
                    
                    // 释放接口
                    composition_controller->Release();
                }
                
                // 确保WebView2控件显示在所有窗口最前面
                EnsureWebView2OnTop();

                // 注册消息处理逻辑
                {
                    g_webview->add_WebMessageReceived(CreateWebMessageHandler(), nullptr);
                }

                // 注册NavigationCompleted事件处理器，用于在页面加载完成后发送BOSS列表
                {
                    class NavigationCompletedHandler : public ICoreWebView2NavigationCompletedEventHandler
                    {
                    private:
                        ULONG m_refCount;
                    public:
                        NavigationCompletedHandler() : m_refCount(1) {}
                        ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_refCount); }
                        ULONG STDMETHODCALLTYPE Release() override { 
                            ULONG count = InterlockedDecrement(&m_refCount);
                            if (count == 0) {
                                delete this;
                            }
                            return count;
                        }
                        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
                            if (!ppvObject) return E_POINTER;
                            if (riid == IID_IUnknown || riid == IID_ICoreWebView2NavigationCompletedEventHandler) {
                                *ppvObject = this;
                                AddRef();
                                return S_OK;
                            }
                            return E_NOINTERFACE;
                        }
                        
                        HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) override
                        {
                            // 页面加载完成后，发送BOSS列表到UI
                            PacketParser::SendBossListToUI();
                            return S_OK;
                        }
                    };
                    
                    g_webview->add_NavigationCompleted(new NavigationCompletedHandler(), nullptr);
                }

                // 如果WebView2没有成功设置为最前，启动计时器定期检查
                if (!g_is_webview_topmost)
                {
                    if (!g_zorder_timer_id)
                    {
                        // 设置计时器，每500毫秒检查一次窗口层级
                        g_zorder_timer_id = SetTimer(g_hWnd, 1, 500, nullptr);
                    }
                }

                // 使用嵌入的HTML内容
                std::wstring html_content = Utf8ToWide(g_html_content);
                g_webview->NavigateToString(html_content.c_str());

                // 启动异步版本检查
                CheckForUpdatesAsync();

                // 销毁加载遮罩窗口
                DestroyLoadingOverlay();

                return S_OK;
            }
        };
        
        // 创建 WebView2 控制器
        CreateControllerCompletedHandler* create_controller_handler = new CreateControllerCompletedHandler();
        g_env->CreateCoreWebView2Controller(g_hWnd, create_controller_handler);

        return S_OK;
    }
};

// 主函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    // 注册窗口类 (使用 Unicode 版本)
    const wchar_t CLASS_NAME[] = L"WebView2DemoWindowClass";

    // 从资源文件加载图标
    HICON hAppIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(1));

    WNDCLASSEXW wc = {
        sizeof(WNDCLASSEXW),
        CS_HREDRAW | CS_VREDRAW,
        WindowProc,
        0,
        0,
        hInstance,
        hAppIcon,  // 大图标
        LoadCursor(nullptr, IDC_ARROW),
        nullptr,
        nullptr,
        CLASS_NAME,
        hAppIcon   // 小图标
    };

    RegisterClassExW(&wc);

    // 创建主窗口 (使用 Unicode 版本) - 无边框样式带阴影
    // 使用 WS_THICKFRAME 配合 DwmExtendFrameIntoClientArea 实现系统阴影
    g_hWnd = CreateWindowExW(
        0,  // 普通窗口样式
        CLASS_NAME,
        L"卡布西游浮影微端 V1.07",
        WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_MINIMIZEBOX | WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 976, 813,  // 窗口宽度976，高度813
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (g_hWnd == nullptr)
    {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK);
        return 1;
    }

    // 启用窗口阴影 - DwmExtendFrameIntoClientArea 方法
    // 这是 Win10+ 兼容的标准方法
    MARGINS margins = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(g_hWnd, &margins);
    
    // 强制重绘窗口框架
    SetWindowPos(g_hWnd, nullptr, 0, 0, 0, 0,
        SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | 
        SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE);

    // 初始化WPE Hook模块
    InitializeWpeHook();
    
    // 初始化 UIBridge
    UIBridge::Instance().Initialize(g_hWnd);
    
    // 初始化 COM
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr))
    {
        MessageBoxW(nullptr, L"Failed to initialize COM", L"Error", MB_OK);
        return 1;
    }
    
    // 初始化ATL模块
    _Module.Init(nullptr, GetModuleHandleW(nullptr));

    // 窗口居中显示（先设置位置，不显示窗口）
    RECT rc;
    GetWindowRect(g_hWnd, &rc);
    int wndWidth = rc.right - rc.left;
    int wndHeight = rc.bottom - rc.top;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(g_hWnd, nullptr, 
        (screenWidth - wndWidth) / 2, 
        (screenHeight - wndHeight) / 2, 
        0, 0, SWP_NOSIZE | SWP_NOZORDER);
    
    // 显示窗口
    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);
    
    // 创建加载遮罩窗口
    CreateLoadingOverlay();
    
    // 立即初始化Hook（不等待页面加载）
    // 这样即使还没打开WPE标签页，也能调用send
    InitializeHooks();

    // 获取窗口客户区大小
    RECT parentRect;
    GetClientRect(g_hWnd, &parentRect);
    
    // 标题栏高度
    const int TITLE_BAR_HEIGHT = 32;
    
    // 计算WebBrowser控件的位置：固定高度 562px，顶部为标题栏留出空间
    RECT browserRect;
    browserRect.left = parentRect.left;
    browserRect.right = parentRect.right;
    browserRect.top = parentRect.top + TITLE_BAR_HEIGHT;  // 为标题栏留出空间
    browserRect.bottom = browserRect.top + 562;  // 固定高度 562px
    
    // 确保窗口尺寸正确
    
    // 先尝试初始化WebView2，再创建IE控件，这样WebView2会默认显示在IE控件上方
    if (LoadWebView2LoaderFromMemory())
    {
        // 创建 WebView2 环境 - 使用临时目录作为用户数据文件夹
        wchar_t temp_path[MAX_PATH];
        if (GetTempPathW(MAX_PATH, temp_path) != 0)
        {
            // 在临时目录中创建一个唯一的子目录
            wchar_t user_data_folder[MAX_PATH];
            if (GetTempFileNameW(temp_path, L"WV2", 0, user_data_folder) != 0)
            {
                // 删除临时文件，创建目录
                DeleteFileW(user_data_folder);
                CreateDirectoryW(user_data_folder, nullptr);
                
                CreateEnvironmentCompletedHandler* create_env_handler = new CreateEnvironmentCompletedHandler();
                hr = g_pfnCreateCoreWebView2EnvironmentWithOptions(nullptr, user_data_folder, nullptr, create_env_handler);
            }
        }
    }
    
    // 延迟创建IE控件，确保WebView2有足够时间初始化
    Sleep(100);
    
    // 使用ATL的CAxWindow创建WebBrowser控件容器
    g_axWindow.Create(g_hWnd, browserRect, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_EX_CLIENTEDGE);
    
    g_hwnd_webbrowser = g_axWindow.m_hWnd;
    
    // 创建WebBrowser控件实例
    if (g_hwnd_webbrowser)
    {
        HRESULT hr = g_axWindow.CreateControl(L"Shell.Explorer.2");
        if (SUCCEEDED(hr))
        {
            // 获取IWebBrowser2接口
            hr = g_axWindow.QueryControl(&g_pWebBrowser);
            if (SUCCEEDED(hr) && g_pWebBrowser)
            {
                // 设置WebBrowser控件为静默状态，不显示脚本错误和安全警告
                g_pWebBrowser->put_Silent(VARIANT_TRUE);
                
                // 导航到指定URL
                VARIANT vEmpty;
                VariantInit(&vEmpty);
                BSTR bstrURL = SysAllocString(L"http://news.4399.com/login/kbxy.html");
                g_pWebBrowser->Navigate(bstrURL, &vEmpty, &vEmpty, &vEmpty, &vEmpty);
                SysFreeString(bstrURL);
            }
        }
        
        // 将WebBrowser控件的可见性设为真，恢复正常显示
        ShowWindow(g_hwnd_webbrowser, SW_SHOW);
        
        // 将WebBrowser控件置于顶层，确保它显示在WebView2上方
        SetWindowPos(g_hwnd_webbrowser, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
    

    
    // 删除所有与WebView2层级设置相关的代码
    // 确保IE浏览框显示在WebView2上方
    
    // 消息循环
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 释放资源 (已经在 WM_DESTROY 中处理过了，这里是最后的兜底)
    if (g_hwnd_webbrowser != nullptr)
    {
        g_axWindow.DestroyWindow();
        g_hwnd_webbrowser = nullptr;
    }
    
    // 如果 WM_DESTROY 没被调用（理论上不会），这里再清理一次
    if (g_webview) { g_webview->Release(); g_webview = nullptr; }
    if (g_controller) { g_controller->Release(); g_controller = nullptr; }
    if (g_env) { g_env->Release(); g_env = nullptr; }
    if (g_webView2LoaderModule) { MemoryFreeLibrary(g_webView2LoaderModule); g_webView2LoaderModule = nullptr; }



    // 反初始化ATL模块
    _Module.Term();
    
    CoUninitialize();

    return (int)msg.wParam;
}

// ============================================================================
// 文件选择器辅助函数
// ============================================================================

/**
 * @brief 打开保存文件对话框
 * @param initialDir 初始目录
 * @param defaultFileName 默认文件名
 * @param filter 文件过滤器
 * @return 用户选择的文件路径，如果取消则返回空字符串
 */
std::wstring ShowSaveFileDialog(const std::wstring& initialDir,
                                 const std::wstring& defaultFileName,
                                 const std::wstring& filter)
{
    OPENFILENAMEW ofn = {0};
    wchar_t szFile[MAX_PATH] = {0};
    
    if (!defaultFileName.empty()) {
        wcscpy_s(szFile, MAX_PATH, defaultFileName.c_str());
    }
    
    // 构建默认过滤器
    static const wchar_t defaultFilter[] = L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter.empty() ? defaultFilter : filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = initialDir.empty() ? NULL : initialDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_EXPLORER;
    ofn.lpstrDefExt = L"txt";
    
    if (GetSaveFileNameW(&ofn)) {
        return std::wstring(szFile);
    }
    
    return L"";
}

/**
 * @brief 打开打开文件对话框
 * @param initialDir 初始目录
 * @param filter 文件过滤器
 * @return 用户选择的文件路径，如果取消则返回空字符串
 */
std::wstring ShowOpenFileDialog(const std::wstring& initialDir,
                                 const std::wstring& filter)
{
    OPENFILENAMEW ofn = {0};
    wchar_t szFile[MAX_PATH] = {0};
    
    // 构建默认过滤器
    static const wchar_t defaultFilter[] = L"文本文件 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0";
    
    ofn.lStructSize = sizeof(OPENFILENAMEW);
    ofn.hwndOwner = g_hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = filter.empty() ? defaultFilter : filter.c_str();
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = initialDir.empty() ? NULL : initialDir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
    
    if (GetOpenFileNameW(&ofn)) {
        return std::wstring(szFile);
    }
    
    return L"";
}
