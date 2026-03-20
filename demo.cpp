#include <windows.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <iomanip>
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
#include <thread>
#include <regex>

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

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ComCtl32.lib")
#pragma comment(lib, "mmdevapi.lib")
#pragma comment(lib, "winhttp.lib")

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
constexpr float CURRENT_VERSION = 1.04f;  // 当前版本：1.04
constexpr wchar_t VERSION_CHECK_URL[] = L"https://gitee.com/deepmoutains/kxby-release-detection/raw/master/data.txt";
constexpr wchar_t UPDATE_DOWNLOAD_URL[] = L"https://wwbov.lanzout.com/b03ancytve";

// 内存加载文件助手函数 (Release 模式下不再需要)
#ifdef _DEBUG
std::vector<BYTE> ReadFileToBuffer(const std::wstring& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<BYTE> buffer(size);
    if (file.read((char*)buffer.data(), size)) return buffer;
    return {};
}
#endif

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
#define WM_EXECUTE_JS (WM_USER + 101)

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

// 安全地发送JS脚本到UI线程（自动管理内存）
bool PostScriptToUI(const std::wstring& jsCode) {
    if (jsCode.empty() || !g_hWnd) return false;
    
    wchar_t* pScript = new(std::nothrow) wchar_t[jsCode.length() + 1];
    if (!pScript) return false;
    
    wcscpy_s(pScript, jsCode.length() + 1, jsCode.c_str());
    
    if (!PostMessage(g_hWnd, WM_EXECUTE_JS, 0, (LPARAM)pScript)) {
        delete[] pScript;
        return false;
    }
    return true;
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

// 从文本内容中解析版本信息（Gitee data.txt 格式）
VersionInfo ParseVersionInfo(const std::wstring& content) {
    VersionInfo info = {CURRENT_VERSION, L"", L""};

    // 按行解析，更可靠
    std::wistringstream stream(content);
    std::wstring line;
    std::wstring currentSection;
    
    while (std::getline(stream, line)) {
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
                versionStr = std::regex_replace(versionStr, std::wregex(LR"(^\s+|\s+$)"), L"");
                try {
                    info.latestVersion = std::stof(versionStr);
                } catch (...) {
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
                info.announcement = std::regex_replace(info.announcement, std::wregex(LR"(^\s+|\s+$)"), L"");
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

// JSON 字符串转义辅助函数
std::wstring EscapeJsonString(const std::wstring& input) {
    std::wstring result;
    for (wchar_t c : input) {
        switch (c) {
            case L'"': result += L"\\\""; break;
            case L'\\': result += L"\\\\"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

// 异步检查版本更新
void CheckForUpdatesAsync() {
    std::thread([]() {
        HINTERNET hSession = nullptr;
        HINTERNET hConnect = nullptr;
        HINTERNET hRequest = nullptr;
        
        try {
            // 初始化 WinHTTP
            hSession = WinHttpOpen(
                L"KBWebUI/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0
            );
            
            if (!hSession) throw std::runtime_error("WinHttpOpen failed");
            
            // 设置超时时间（10秒）
            WinHttpSetTimeouts(hSession, 10000, 10000, 10000, 10000);
            
            // 解析 URL
            URL_COMPONENTS urlComp = {0};
            urlComp.dwStructSize = sizeof(URL_COMPONENTS);
            urlComp.dwSchemeLength = (DWORD)-1;
            urlComp.dwHostNameLength = (DWORD)-1;
            urlComp.dwUrlPathLength = (DWORD)-1;
            
            if (!WinHttpCrackUrl(VERSION_CHECK_URL, 0, 0, &urlComp)) {
                throw std::runtime_error("WinHttpCrackUrl failed");
            }

            std::wstring hostName(urlComp.lpszHostName, urlComp.dwHostNameLength);
            std::wstring urlPath(urlComp.lpszUrlPath, urlComp.dwUrlPathLength);
            
            hConnect = WinHttpConnect(hSession, hostName.c_str(), urlComp.nPort, 0);
            if (!hConnect) throw std::runtime_error("WinHttpConnect failed");
            
            // 创建请求（支持 HTTPS）
            DWORD flags = 0;
            if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
                flags = WINHTTP_FLAG_SECURE;
            }
            
            hRequest = WinHttpOpenRequest(hConnect, L"GET", urlPath.c_str(),
                nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);

            if (!hRequest) throw std::runtime_error("WinHttpOpenRequest failed");

            // 设置安全选项，忽略 SSL 证书错误
            if (urlComp.nScheme == INTERNET_SCHEME_HTTPS) {
                DWORD securityFlags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                                     SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                                     SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
                WinHttpSetOption(hRequest, WINHTTP_OPTION_SECURITY_FLAGS, &securityFlags, sizeof(securityFlags));
            }

            // 发送请求
            if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                throw std::runtime_error("WinHttpSendRequest failed");
            }
            
            // 接收响应
            if (!WinHttpReceiveResponse(hRequest, nullptr)) {
                throw std::runtime_error("WinHttpReceiveResponse failed");
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
                std::wstringstream jsScript;
                jsScript << L"if(window.showUpdateDialog) { window.showUpdateDialog({";
                jsScript << L"version: " << versionInfo.latestVersion << L",";
                jsScript << L"announcement: \"" << EscapeJsonString(versionInfo.announcement) << L"\",";
                jsScript << L"updateContent: \"" << EscapeJsonString(versionInfo.updateContent) << L"\",";
                jsScript << L"downloadUrl: \"" << UPDATE_DOWNLOAD_URL << L"\"";
                jsScript << L"}); }";

                std::wstring script = jsScript.str();
                PostScriptToUI(script);
            }
            
        } catch (const std::exception&) {
            // 版本检查失败，静默处理
        }
        
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
    case WM_EXECUTE_JS:
        if (lParam) {
            wchar_t* script = (wchar_t*)lParam;
            if (script) {
                ExecuteScriptInWebView2(script);
                delete[] script;
            }
        }
        break;
    case WM_DECOMPOSE_COMPLETE:
        // 在UI线程中安全地调用分解完成的JavaScript回调
        ExecuteScriptInWebView2(L"if(window.onDecomposeComplete) window.onDecomposeComplete();");
        break;
    case WM_DECOMPOSE_HEX_PACKET:
        // 清理十六进制封包数据内存
        {
            HexPacketData* hexData = (HexPacketData*)wParam;
            if (hexData) {
                delete[] hexData->data;
                delete hexData;
            }
        }
        break;
    case WM_DAILY_TASK_COMPLETE:
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
                    // 设置WebView2消息事件处理
                    class MessageHandler : public ICoreWebView2WebMessageReceivedEventHandler
                    {
                    private:
                        ULONG m_refCount;
                    public:
                        MessageHandler() : m_refCount(1) {}
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
                            if (riid == IID_IUnknown || riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
                                *ppvObject = this;
                                AddRef();
                                return S_OK;
                            }
                            return E_NOINTERFACE;
                        }
                        
                                                    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override
                                                {
                                                    if (!args) return E_POINTER;
                                                    
                                                    PWSTR message = nullptr;
                                                    HRESULT hr = args->get_WebMessageAsJson(&message);
                                                    if (FAILED(hr) || !message) return hr;
                                                    
                                                    std::wstring msg(message);
                                                    CoTaskMemFree(message);
                                                    
                                                    // 处理双重序列化：如果消息以引号开头，说明是字符串被再次序列化
                                                    if (msg.size() >= 2 && msg[0] == L'"' && msg[msg.size()-1] == L'"') {
                                                        // 移除外层引号并反转义
                                                        std::wstring unescaped;
                                                        for (size_t i = 1; i < msg.size() - 1; i++) {
                                                            if (msg[i] == L'\\' && i + 1 < msg.size() - 1) {
                                                                // 处理转义字符
                                                                switch (msg[i+1]) {
                                                                    case L'"': unescaped += L'"'; i++; break;
                                                                    case L'\\': unescaped += L'\\'; i++; break;
                                                                    case L'n': unescaped += L'\n'; i++; break;
                                                                    case L'r': unescaped += L'\r'; i++; break;
                                                                    case L't': unescaped += L'\t'; i++; break;
                                                                    default: unescaped += msg[i]; break;
                                                                }
                                                            } else {
                                                                unescaped += msg[i];
                                                            }
                                                        }
                                                        msg = unescaped;
                                                    }
                                                    
                                                    // 辅助函数：简单的 JSON 值提取（支持字符串和数组）
                                                    auto get_json_value = [&](const std::wstring& key) -> std::wstring {
                                                        size_t key_pos = msg.find(L"\"" + key + L"\"");
                                                        if (key_pos == std::wstring::npos) return L"";
                                                        size_t colon_pos = msg.find(L":", key_pos);
                                                        if (colon_pos == std::wstring::npos) return L"";
                                                        size_t val_start = msg.find_first_not_of(L" \t\n\r", colon_pos + 1);
                                                        if (val_start == std::wstring::npos) return L"";
                                                        
                                                        if (msg[val_start] == L'\"') {
                                                            // 字符串类型
                                                            size_t val_end = msg.find(L"\"", val_start + 1);
                                                            if (val_end != std::wstring::npos) return msg.substr(val_start + 1, val_end - val_start - 1);
                                                        } else if (msg[val_start] == L'[') {
                                                            // 数组类型：找到匹配的 ]
                                                            size_t bracket_count = 1;
                                                            size_t val_end = val_start + 1;
                                                            while (val_end < msg.length() && bracket_count > 0) {
                                                                if (msg[val_end] == L'[') bracket_count++;
                                                                else if (msg[val_end] == L']') bracket_count--;
                                                                val_end++;
                                                            }
                                                            if (bracket_count == 0) {
                                                                return msg.substr(val_start, val_end - val_start);
                                                            }
                                                        } else {
                                                            // 其他类型（数字、布尔等）
                                                            size_t val_end = msg.find_first_of(L",}", val_start);
                                                            if (val_end != std::wstring::npos) return msg.substr(val_start, val_end - val_start);
                                                        }
                                                        return L"";
                                                    };

                            // 处理不同类型的消息
                            if (msg.find(L"window-drag") != std::wstring::npos) {
                                // 窗口拖拽：发送 WM_NCLBUTTONDOWN 消息模拟标题栏点击
                                ReleaseCapture();
                                SendMessage(g_hWnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                            } else if (msg.find(L"window-minimize") != std::wstring::npos) {
                                // 最小化窗口
                                ShowWindow(g_hWnd, SW_MINIMIZE);
                            } else if (msg.find(L"window-close") != std::wstring::npos) {
                                // 关闭窗口
                                PostMessage(g_hWnd, WM_CLOSE, 0, 0);
                            } else if (msg.find(L"update-dialog-show") != std::wstring::npos) {
                                // 显示更新对话框时隐藏 IE 浏览框
                                if (g_hwnd_webbrowser) {
                                    ShowWindow(g_hwnd_webbrowser, SW_HIDE);
                                }
                            } else if (msg.find(L"update-dialog-hide") != std::wstring::npos) {
                                // 关闭更新对话框时恢复显示 IE 浏览框
                                if (g_hwnd_webbrowser) {
                                    ShowWindow(g_hwnd_webbrowser, SW_SHOW);
                                }
                            } else if (msg.find(L"open-url") != std::wstring::npos) {
                                // 用系统默认浏览器打开链接
                                std::wstring url = get_json_value(L"url");
                                if (!url.empty()) {
                                    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                                }
                            } else if (msg.find(L"refresh-game") != std::wstring::npos) {
                                if (g_pWebBrowser) {
                                    VARIANT vEmpty; VariantInit(&vEmpty);
                                    BSTR bstrURL = SysAllocString(L"http://news.4399.com/login/kbxy.html");
                                    g_pWebBrowser->Navigate(bstrURL, &vEmpty, &vEmpty, &vEmpty, &vEmpty);
                                    SysFreeString(bstrURL);
                                }
                            } else if (msg.find(L"refresh-no-login") != std::wstring::npos) {
                                if (g_pWebBrowser) g_pWebBrowser->Refresh();
                            } else if (msg.find(L"mute-game") != std::wstring::npos) {
                                ToggleProgramVolume();
                            } else if (msg.find(L"copy-login-key") != std::wstring::npos) {
                                // 复制登录 Key 到剪贴板
                                if (!g_loginKey.empty()) {
                                    if (OpenClipboard(nullptr)) {
                                        EmptyClipboard();
                                        size_t len = (g_loginKey.length() + 1) * sizeof(wchar_t);
                                        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
                                        if (hMem) {
                                            memcpy(GlobalLock(hMem), g_loginKey.c_str(), len);
                                            GlobalUnlock(hMem);
                                            SetClipboardData(CF_UNICODETEXT, hMem);
                                        }
                                        CloseClipboard();
                                    }
                                }
                            } else if (msg.find(L"key-login-dialog-show") != std::wstring::npos) {
                                // 显示 Key 登录对话框时隐藏 IE 浏览框
                                ShowWindow(g_hwnd_webbrowser, SW_HIDE);
                            } else if (msg.find(L"key-login-dialog-hide") != std::wstring::npos) {
                                // 隐藏 Key 登录对话框时恢复 IE 浏览框
                                ShowWindow(g_hwnd_webbrowser, SW_SHOW);
                            } else if (msg.find(L"key-login") != std::wstring::npos) {
                                // Key 登录 - 构造 URL 并导航
                                std::wstring key = get_json_value(L"key");
                                if (!key.empty() && g_pWebBrowser) {
                                    std::wstring url = BuildLoginUrl(key);
                                    if (!url.empty()) {
                                        VARIANT vEmpty; VariantInit(&vEmpty);
                                        BSTR bstrURL = SysAllocString(url.c_str());
                                        g_pWebBrowser->Navigate(bstrURL, &vEmpty, &vEmpty, &vEmpty, &vEmpty);
                                        SysFreeString(bstrURL);
                                    }
                                }
                            } else if (msg.find(L"packet_window_visible") != std::wstring::npos) {
                                std::wstring visible_val = get_json_value(L"visible");
                                bool visible = (visible_val == L"true");
                                g_is_packet_window_visible = visible;
                                
                                if (visible) {
                                    g_packet_window_rect.left = _wtoi(get_json_value(L"left").c_str());
                                    g_packet_window_rect.top = _wtoi(get_json_value(L"top").c_str());
                                    g_packet_window_rect.right = _wtoi(get_json_value(L"width").c_str());
                                    g_packet_window_rect.bottom = _wtoi(get_json_value(L"height").c_str());
                                    SyncPacketsToUI();
                                } else {
                                    // 窗口关闭时，只更新计数，不清空UI
                                    DWORD packetCount = GetPacketCount();
                                    std::wstring jsUpdateCount = L"if(window.updatePacketCount) { window.updatePacketCount(" + std::to_wstring(packetCount) + L"); }";
                                    ExecuteScriptInWebView2(jsUpdateCount.c_str());
                                }
                                UpdateIEEWindowRegion();
                            } else if (msg.find(L"delete_selected_packets") != std::wstring::npos) {
                                std::vector<DWORD> indices;
                                size_t key_pos = msg.find(L"\"indices\"");
                                if (key_pos != std::wstring::npos) {
                                    size_t colon_pos = msg.find(L":", key_pos);
                                    size_t lbrack = msg.find(L"[", colon_pos);
                                    size_t rbrack = msg.find(L"]", lbrack);
                                    if (lbrack != std::wstring::npos && rbrack != std::wstring::npos) {
                                        std::wstring arr = msg.substr(lbrack + 1, rbrack - lbrack - 1);
                                        std::wistringstream ss(arr);
                                        while (ss.good()) {
                                            std::wstring num;
                                            if (!std::getline(ss, num, L',')) break;
                                            // trim spaces
                                            size_t start = num.find_first_not_of(L" \t\r\n");
                                            size_t end = num.find_last_not_of(L" \t\r\n");
                                            if (start == std::wstring::npos) continue;
                                            std::wstring trimmed = num.substr(start, end - start + 1);
                                            int val = _wtoi(trimmed.c_str());
                                            if (val >= 0) indices.push_back(static_cast<DWORD>(val));
                                        }
                                    }
                                }
                                if (!indices.empty()) {
                                    DeleteSelectedPackets(indices);
                                }
                                // 清空UI列表项容器，保留列表头
                                const wchar_t* clearScript = LR"(
                                    (function(){
                                        var pListItems = document.getElementById('packet-list-items');
                                        if (pListItems) { 
                                            pListItems.innerHTML = ''; 
                                        }
                                        // 更新计数
                                        if (window.updatePacketCount) { 
                                            var count = pListItems ? pListItems.children.length : 0;
                                            window.updatePacketCount(count);
                                        }
                                    })();
                                )";
                                ExecuteScriptInWebView2(clearScript);
                                // 重新同步当前列表到UI
                                SyncPacketsToUI();
                            } else if (msg.find(L"clear_packets") != std::wstring::npos) {
                                ClearPacketList();
                            } else if (msg.find(L"start_intercept") != std::wstring::npos) {
                                StartIntercept();
                            } else if (msg.find(L"stop_intercept") != std::wstring::npos) {
                                StopIntercept();
                            } else if (msg.find(L"set_intercept_type") != std::wstring::npos) {
                                bool send = (get_json_value(L"send") == L"true");
                                bool recv = (get_json_value(L"recv") == L"true");
                                SetInterceptType(send, recv);
                            } else if (msg.find(L"send_packet") != std::wstring::npos) {
                                std::wstring hexW = get_json_value(L"hex");
                                if (!hexW.empty()) {
                                    std::string hexA = WideToUtf8(hexW);
                                    std::vector<BYTE> data = StringToHex(hexA);
                                    // 在后台线程发送，避免阻塞 UI 线程
                                    struct SendThreadData {
                                        std::vector<BYTE> data;
                                    };
                                    SendThreadData* pData = new SendThreadData{ std::move(data) };
                                    CreateThread(nullptr, 0, [](LPVOID lpParam) -> DWORD {
                                        SendThreadData* pD = (SendThreadData*)lpParam;
                                        BOOL result = SendPacket(0, pD->data.data(), (DWORD)pD->data.size());
                                        if (!result) {
                                            // 发送失败，通知UI
                                            std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('封包发送失败：未连接到游戏服务器'); }";
                                            PostScriptToUI(script);
                                        }
                                        delete pD;
                                        return 0;
                                    }, pData, 0, nullptr);
                                } else {
                                    // 数据为空，通知UI
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('封包数据格式错误'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"set_speed") != std::wstring::npos) {
                                std::wstring speedStr = get_json_value(L"speed");
                                float speed = (float)_wtof(speedStr.c_str());
                                if (LoadSpeedhackFromMemory()) {
                                    if (g_pfnInitializeSpeedhack) {
                                        g_pfnInitializeSpeedhack(speed);
                                    }
                                }
                            } else if (msg.find(L"toggle_auto_heal") != std::wstring::npos) {
                                // 自动回血
                                std::wstring enabledStr = get_json_value(L"enabled");
                                g_autoHeal = (enabledStr == L"true");
                            } else if (msg.find(L"set_block_battle") != std::wstring::npos) {
                                // 设置屏蔽战斗
                                std::wstring enabledStr = get_json_value(L"enabled");
                                g_blockBattle = (enabledStr == L"true");
                            } else if (msg.find(L"set_auto_go_home") != std::wstring::npos) {
                                // 设置自动回家
                                std::wstring enabledStr = get_json_value(L"enabled");
                                g_autoGoHome = (enabledStr == L"true");
                            } else if (msg.find(L"query_lingyu") != std::wstring::npos) {
                                // 查询灵玉 - 发送2个封包（间隔300ms）
                                SendQueryLingyuPacket();
                            } else if (msg.find(L"query_monsters") != std::wstring::npos) {
                                // 查询妖怪背包列表
                                SendQueryMonsterPacket();
                            } else if (msg.find(L"refresh_pack_items") != std::wstring::npos) {
                                // 刷新背包物品
                                SendReqPackageDataPacket(0xFFFFFFFF);
                            } else if (msg.find(L"buy_item") != std::wstring::npos) {
                                // 购买道具
                                std::wstring itemIdStr = get_json_value(L"itemId");
                                std::wstring countStr = get_json_value(L"count");
                                uint32_t itemId = itemIdStr.empty() ? 0 : (uint32_t)_wtol(itemIdStr.c_str());
                                uint32_t count = countStr.empty() ? 1 : (uint32_t)_wtol(countStr.c_str());
                                
                                if (itemId > 0) {
                                    // 先刷新背包，确保背包数据是最新的
                                    SendReqPackageDataPacket(0xFFFFFFFF);
                                    Sleep(200);  // 等待背包数据更新
                                    
                                    if (SendBuyGoodsPacket(itemId, count)) {
                                        std::wstring itemName = GetItemName(itemId);
                                        wchar_t msg[128];
                                        swprintf_s(msg, L"购买道具成功: %s x%u", itemName.c_str(), count);
                                        std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('" + std::wstring(msg) + L"'); }";
                                        PostScriptToUI(script);
                                    }
                                }
                            } else if (msg.find(L"use_item") != std::wstring::npos) {
                                // 使用道具（战斗中）
                                std::wstring itemIdStr = get_json_value(L"itemId");
                                std::wstring countStr = get_json_value(L"count");
                                uint32_t itemId = itemIdStr.empty() ? 0 : (uint32_t)_wtol(itemIdStr.c_str());
                                uint32_t count = countStr.empty() ? 1 : (uint32_t)_wtol(countStr.c_str());
                                
                                if (itemId > 0) {
                                    // 检查是否在战斗中
                                    if (!g_battleStarted) {
                                        std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('使用道具需要在战斗中进行'); }";
                                        PostScriptToUI(script);
                                    } else {
                                        // 先刷新背包，确保背包数据是最新的
                                        SendReqPackageDataPacket(0xFFFFFFFF);
                                        Sleep(200);  // 等待背包数据更新
                                        
                                        // 战斗中使用道具
                                        if (SendUseItemInBattlePacket(itemId)) {
                                            std::wstring itemName = GetItemName(itemId);
                                            wchar_t msg[128];
                                            swprintf_s(msg, L"使用道具: %s", itemName.c_str());
                                            std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('" + std::wstring(msg) + L"'); }";
                                            PostScriptToUI(script);
                                        } else {
                                            std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('使用道具失败，背包中无此道具'); }";
                                            PostScriptToUI(script);
                                        }
                                    }
                                }
                            } else if (msg.find(L"daily_tasks") != std::wstring::npos) {
                                // 一键完成日常活动
                                std::wstring flagsStr = get_json_value(L"flags");
                                DWORD flags = 0;
                                if (!flagsStr.empty()) {
                                    flags = (DWORD)_wtol(flagsStr.c_str());
                                }
                                SendDailyTasksAsync(flags);
                            } else if (msg.find(L"one_key_collect") != std::wstring::npos) {
                                // 一键采集
                                std::wstring flagsStr = get_json_value(L"flags");
                                DWORD flags = 0;
                                if (!flagsStr.empty()) {
                                    flags = (DWORD)_wtol(flagsStr.c_str());
                                }
                                if (SendOneKeyCollectPacket(flags)) {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('一键采集已开始，请查看辅助日志'); }";
                                    PostScriptToUI(script);
                                } else {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('一键采集启动失败，可能已经在运行或未进入游戏'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"buy_dice_18") != std::wstring::npos) {
                                // 购买18个骰子
                                if (SendBuyDicePacket()) {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('已购买18个骰子'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"buy_dice") != std::wstring::npos) {
                                // 购买骰子
                                SendBuyDicePacket();
                            } else if (msg.find(L"one_key_xuantta") != std::wstring::npos) {
                                // 一键玄塔（直接使用全局的 g_blockBattle 变量）
                                // 注意：不要在这里重新设置 g_blockBattle，应该使用用户在复选框中设置的值

                                // 执行一键玄塔
                                if (SendOneKeyTowerPacket()) {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('一键玄塔已开始，请查看辅助日志'); }";
                                    PostScriptToUI(script);
                                } else {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('一键玄塔启动失败，可能已经在运行或未进入游戏'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"query_shuangtai") != std::wstring::npos) {
                                // 双台谷刷级 - 查询妖怪数据
                                QueryShuangTaiMonsters();
                            } else if (msg.find(L"start_shuangtai") != std::wstring::npos) {
                                // 双台谷刷级 - 启动
                                std::wstring blockBattleStr = get_json_value(L"blockBattle");
                                while (!blockBattleStr.empty() && (blockBattleStr.front() == L' ' || blockBattleStr.front() == L'\t')) blockBattleStr.erase(0, 1);
                                while (!blockBattleStr.empty() && (blockBattleStr.back() == L' ' || blockBattleStr.back() == L'\t')) blockBattleStr.pop_back();
                                bool blockBattle = (blockBattleStr == L"true");
                                
                                if (SendOneKeyShuangTaiPacket(blockBattle)) {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('双台谷刷级已开始，请查看辅助日志'); }";
                                    PostScriptToUI(script);
                                } else {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('双台谷刷级启动失败，请先点击查询按钮获取妖怪数据'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"stop_shuangtai") != std::wstring::npos) {
                                // 双台谷刷级 - 停止
                                StopShuangTai();
                                std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('双台谷刷级已停止'); }";
                                PostScriptToUI(script);
                            } else if (msg.find(L"one_key_strawberry") != std::wstring::npos) {
                                // 一键采摘红莓果
                                std::wstring sweepStr = get_json_value(L"sweep");
                                while (!sweepStr.empty() && (sweepStr.front() == L' ' || sweepStr.front() == L'\t')) sweepStr.erase(0, 1);
                                while (!sweepStr.empty() && (sweepStr.back() == L' ' || sweepStr.back() == L'\t')) sweepStr.pop_back();
                                bool useSweep = (sweepStr == L"true");
                                if (SendOneKeyStrawberryPacket(useSweep)) {
                                    std::wstring script = useSweep 
                                        ? L"if(window.updateHelperText) { window.updateHelperText('采摘红莓果已开始（扫荡模式），请查看辅助日志'); }"
                                        : L"if(window.updateHelperText) { window.updateHelperText('采摘红莓果已开始，请查看辅助日志'); }";
                                    PostScriptToUI(script);
                                } else {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('采摘红莓果启动失败，可能已经在运行或未进入游戏'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"battlesix_auto_match") != std::wstring::npos) {
                                // 万妖盛会 - 自动匹配（异步执行，避免卡顿）
                                // 读取匹配次数
                                std::wstring matchCountStr = get_json_value(L"matchCount");
                                while (!matchCountStr.empty() && (matchCountStr.front() == L' ' || matchCountStr.front() == L'\t')) matchCountStr.erase(0, 1);
                                while (!matchCountStr.empty() && (matchCountStr.back() == L' ' || matchCountStr.back() == L'\t')) matchCountStr.pop_back();
                                int matchCount = 1;
                                try {
                                    matchCount = std::stoi(matchCountStr);
                                    if (matchCount < 1) matchCount = 1;
                                    if (matchCount > 999) matchCount = 999;
                                } catch (...) {
                                    matchCount = 1;
                                }
                                
                                wchar_t startMsg[128];
                                swprintf_s(startMsg, L"万妖盛会：开始匹配（共%d次）...", matchCount);
                                std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('" + std::wstring(startMsg) + L"'); }";
                                PostScriptToUI(script);
                                
                                // 异步执行匹配（通过堆分配传递参数）
                                int* pMatchCount = new int(matchCount);
                                HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
                                    int count = *(int*)param;
                                    delete (int*)param;
                                    SendOneKeyBattleSixPacket(count);
                                    return 0;
                                }, pMatchCount, 0, nullptr);
                                if (hThread) CloseHandle(hThread);
                            } else if (msg.find(L"battlesix_cancel_match") != std::wstring::npos) {
                                // 万妖盛会 - 取消匹配
                                g_battleSixAuto.SetAutoMatching(false);
                                g_battleSixAuto.SetMatchCount(0);
                                if (SendCancelBattleSixPacket()) {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('万妖盛会：已取消匹配'); }";
                                    PostScriptToUI(script);
                                } else {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('万妖盛会：取消匹配失败'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"battlesix_set_auto_battle") != std::wstring::npos) {
                                // 万妖盛会 - 设置自动战斗
                                std::wstring enabledStr = get_json_value(L"enabled");
                                // 去除可能的空格
                                while (!enabledStr.empty() && (enabledStr.front() == L' ' || enabledStr.front() == L'\t')) enabledStr.erase(0, 1);
                                while (!enabledStr.empty() && (enabledStr.back() == L' ' || enabledStr.back() == L'\t')) enabledStr.pop_back();
                                bool enabled = (enabledStr == L"true");
                                g_battleSixAuto.SetAutoBattle(enabled);
                                
                                // 调试输出：显示实际接收到的消息
                                std::wstring debugScript = L"if(window.updateHelperText) { window.updateHelperText('调试：enabled=[" + enabledStr + L"] parsed=" + (enabled ? L"true" : L"false") + L"'); }";
                                PostScriptToUI(debugScript);
                                
                                std::wstring script = enabled 
                                    ? L"if(window.updateHelperText) { window.updateHelperText('万妖盛会：自动战斗已启用'); }"
                                    : L"if(window.updateHelperText) { window.updateHelperText('万妖盛会：自动战斗已禁用'); }";
                                PostScriptToUI(script);
                            } else if (msg.find(L"dungeon_jump_start") != std::wstring::npos) {
                                // 副本跳层 - 开始
                                std::wstring layerStr = get_json_value(L"targetLayer");
                                while (!layerStr.empty() && (layerStr.front() == L' ' || layerStr.front() == L'\t')) layerStr.erase(0, 1);
                                while (!layerStr.empty() && (layerStr.back() == L' ' || layerStr.back() == L'\t')) layerStr.pop_back();
                                int targetLayer = 1;
                                try {
                                    targetLayer = std::stoi(layerStr);
                                    if (targetLayer < 1) targetLayer = 1;
                                    if (targetLayer > 9999) targetLayer = 9999;
                                } catch (...) {
                                    targetLayer = 1;
                                }
                                
                                // 更新UI状态
                                std::wstring startScript = L"if(window.updateDungeonJumpStatus) { window.updateDungeonJumpStatus('副本跳层：准备跳转到第" + std::to_wstring(targetLayer) + L"层...'); }";
                                PostScriptToUI(startScript);
                                
                                // 异步执行副本跳层
                                int* pTargetLayer = new int(targetLayer);
                                HANDLE hThread = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
                                    int layer = *(int*)param;
                                    delete (int*)param;
                                    SendOneKeyDungeonJumpPacket(layer);
                                    return 0;
                                }, pTargetLayer, 0, nullptr);
                                if (hThread) CloseHandle(hThread);
                            } else if (msg.find(L"dungeon_jump_stop") != std::wstring::npos) {
                                // 副本跳层 - 停止
                                StopDungeonJump();
                                std::wstring script = L"if(window.updateDungeonJumpStatus) { window.updateDungeonJumpStatus('副本跳层：已停止'); }";
                                PostScriptToUI(script);
                            } else if (msg.find(L"one_key_act778") != std::wstring::npos) {
                                // 一键驱傩聚福寿
                                std::wstring sweepStr = get_json_value(L"sweep");
                                while (!sweepStr.empty() && (sweepStr.front() == L' ' || sweepStr.front() == L'\t')) sweepStr.erase(0, 1);
                                while (!sweepStr.empty() && (sweepStr.back() == L' ' || sweepStr.back() == L'\t')) sweepStr.pop_back();
                                bool useSweep = (sweepStr == L"true");
                                if (SendOneKeyAct778Packet(useSweep)) {
                                    std::wstring script = useSweep 
                                        ? L"if(window.updateHelperText) { window.updateHelperText('驱傩聚福寿已开始（扫荡模式），请查看辅助日志'); }"
                                        : L"if(window.updateHelperText) { window.updateHelperText('驱傩聚福寿已开始（最高分模式），请查看辅助日志'); }";
                                    PostScriptToUI(script);
                                } else {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('驱傩聚福寿启动失败'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"one_key_act793") != std::wstring::npos) {
                                // 一键磐石御天火
                                std::wstring sweepStr = get_json_value(L"sweep");
                                while (!sweepStr.empty() && (sweepStr.front() == L' ' || sweepStr.front() == L'\t')) sweepStr.erase(0, 1);
                                while (!sweepStr.empty() && (sweepStr.back() == L' ' || sweepStr.back() == L'\t')) sweepStr.pop_back();
                                bool useSweep = (sweepStr == L"true");
                                
                                // 获取目标勋章数量参数
                                std::wstring medalsStr = get_json_value(L"medals");
                                int targetMedals = Act793::TARGET_MEDALS;
                                if (!medalsStr.empty()) {
                                    try {
                                        targetMedals = std::stoi(medalsStr);
                                        if (targetMedals < 1) targetMedals = Act793::TARGET_MEDALS;
                                        if (targetMedals > 100) targetMedals = 100;
                                    } catch (...) {
                                        targetMedals = Act793::TARGET_MEDALS;
                                    }
                                }
                                
                                if (SendOneKeyAct793Packet(useSweep, targetMedals)) {
                                    std::wstring script = useSweep 
                                        ? L"if(window.updateHelperText) { window.updateHelperText('磐石御天火已开始（扫荡模式），请查看辅助日志'); }"
                                        : L"if(window.updateHelperText) { window.updateHelperText('磐石御天火已开始（游戏模式），请查看辅助日志'); }";
                                    PostScriptToUI(script);
                                } else {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('磐石御天火启动失败'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"one_key_act791") != std::wstring::npos) {
                                // 一键五行镜破封印
                                std::wstring sweepStr = get_json_value(L"sweep");
                                while (!sweepStr.empty() && (sweepStr.front() == L' ' || sweepStr.front() == L'\t')) sweepStr.erase(0, 1);
                                while (!sweepStr.empty() && (sweepStr.back() == L' ' || sweepStr.back() == L'\t')) sweepStr.pop_back();
                                bool useSweep = (sweepStr == L"true");
                                
                                // 获取目标分数参数
                                std::wstring scoreStr = get_json_value(L"score");
                                int targetScore = Act791::TARGET_SCORE;
                                if (!scoreStr.empty()) {
                                    try {
                                        targetScore = std::stoi(scoreStr);
                                        if (targetScore < 1) targetScore = Act791::TARGET_SCORE;
                                        if (targetScore > 250) targetScore = 250;  // 最大分数限制
                                    } catch (...) {
                                        targetScore = Act791::TARGET_SCORE;
                                    }
                                }
                                
                                if (SendOneKeyAct791Packet(useSweep, targetScore)) {
                                    std::wstring script = useSweep 
                                        ? L"if(window.updateHelperText) { window.updateHelperText('五行镜破封印已开始（扫荡模式），请查看辅助日志'); }"
                                        : L"if(window.updateHelperText) { window.updateHelperText('五行镜破封印已开始（游戏模式），请查看辅助日志'); }";
                                    PostScriptToUI(script);
                                } else {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('五行镜破封印启动失败'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"start_heaven_furui") != std::wstring::npos) {
                                // 开始福瑞宝箱
                                std::wstring maxBoxesStr = get_json_value(L"maxBoxes");
                                int maxBoxes = 30;
                                if (!maxBoxesStr.empty()) {
                                    try {
                                        maxBoxes = std::stoi(maxBoxesStr);
                                    } catch (...) {
                                        maxBoxes = 30;
                                    }
                                }
                                if (SendOneKeyHeavenFuruiPacket(maxBoxes)) {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('福瑞宝箱：开始遍历地图查找宝箱...'); }";
                                    PostScriptToUI(script);
                                } else {
                                    std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('福瑞宝箱启动失败'); }";
                                    PostScriptToUI(script);
                                }
                            } else if (msg.find(L"stop_heaven_furui") != std::wstring::npos) {
                                // 停止福瑞宝箱
                                StopHeavenFurui();
                                std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('福瑞宝箱：已停止'); }";
                                PostScriptToUI(script);
                            } else if (msg.find(L"decompose_lingyu") != std::wstring::npos) {
                                // 检查是否为批量分解
                                if (msg.find(L"decompose_lingyu_batch") != std::wstring::npos) {
                                    // 批量分解：indices 是数组
                                    std::wstring jsonArray = get_json_value(L"indices");

                                    if (!jsonArray.empty()) {
                                        // 调用批量分解函数 - 这是异步的
                                        SendDecomposeLingyuPacket(jsonArray);
                                    }
                                } else {
                                    // 单个分解（向后兼容）
                                    std::wstring jsonArray = get_json_value(L"indices_array");

                                    if (!jsonArray.empty()) {
                                        // 直接调用，传递 JSON 数组字符串 - 这是异步的
                                        SendDecomposeLingyuPacket(jsonArray);
                                    }
                                }
                            } else if (msg.find(L"set_hijack_enabled") != std::wstring::npos) {
                                // 设置劫持功能启用/禁用
                                std::wstring enabledStr = get_json_value(L"enabled");
                                bool enabled = (enabledStr == L"true");
                                SetHijackEnabled(enabled);
                            } else if (msg.find(L"add_hijack_rule") != std::wstring::npos) {
                                // 添加劫持规则
                                std::wstring isSendStr = get_json_value(L"isSend");
                                std::wstring isBlockStr = get_json_value(L"isBlock");
                                std::wstring patternW = get_json_value(L"pattern");
                                std::wstring replaceW = get_json_value(L"replace");
                                
                                bool isSend = (isSendStr == L"true");
                                bool isBlock = (isBlockStr == L"true");
                                HijackType type = isBlock ? HIJACK_BLOCK : HIJACK_REPLACE;
                                
                                std::string pattern = WideToUtf8(patternW);
                                std::string replace = WideToUtf8(replaceW);
                                
                                AddHijackRule(type, isSend, !isSend, pattern, replace);
                            } else if (msg.find(L"clear_hijack_rules") != std::wstring::npos) {
                                // 清空劫持规则
                                ClearHijackRules();
                            } else if (msg.find(L"save_packet_list") != std::wstring::npos) {
                                // 保存封包列表 - 打开文件选择器
                                std::wstring filePathW = ShowSaveFileDialog(L"", L"packets.txt");
                                if (!filePathW.empty()) {
                                    if (SavePacketListToFile(filePathW)) {
                                        std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('封包列表已保存到：" + filePathW + L"'); }";
                                        PostScriptToUI(script);
                                    } else {
                                        std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('保存封包列表失败'); }";
                                        PostScriptToUI(script);
                                    }
                                }
                            } else if (msg.find(L"load_packet_list") != std::wstring::npos) {
                                // 载入封包列表 - 打开文件选择器
                                std::wstring filePathW = ShowOpenFileDialog(L"");
                                if (!filePathW.empty()) {
                                    if (LoadPacketListFromFile(filePathW)) {
                                        SyncPacketsToUI();
                                        std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('封包列表已从 " + filePathW + L" 载入'); }";
                                        PostScriptToUI(script);
                                    } else {
                                        std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('载入封包列表失败'); }";
                                        PostScriptToUI(script);
                                    }
                                }
                            } else if (msg.find(L"send_all_packets") != std::wstring::npos) {
                                // 自动发送所有封包
                                DWORD sendCount = 1;
                                DWORD sendDelay = 300;
                                
                                // 解析发送次数
                                std::wstring sendCountStr = get_json_value(L"sendCount");
                                if (!sendCountStr.empty()) {
                                    sendCount = (DWORD)_wtoi(sendCountStr.c_str());
                                    if (sendCount < 1) sendCount = 1;
                                }
                                
                                // 解析发送延迟
                                std::wstring sendDelayStr = get_json_value(L"sendDelay");
                                if (!sendDelayStr.empty()) {
                                    sendDelay = (DWORD)_wtoi(sendDelayStr.c_str());
                                }
                                
                                // 创建线程发送封包，避免阻塞UI
                                // 使用临时对象 + detach，避免 new/delete 的资源管理问题
                                std::thread([sendCount, sendDelay]() {
                                    SendAllPackets(sendDelay, sendCount, 
                                        [](DWORD currentLoop, DWORD packetIndex, const std::string& label) {
                                            // 在主线程更新辅助提示
                                            std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('正在发送第" 
                                                + std::to_wstring(currentLoop) + L"次，第" 
                                                + std::to_wstring(packetIndex) + L"个封包";
                                            
                                            if (!label.empty()) {
                                                script += L"，标签：" + Utf8ToWide(label);
                                            }
                                            
                                            script += L"'); }";
                                            
                                    PostScriptToUI(script);
                                        });
                                    
                                    // 发送完成，更新辅助提示
                                    std::wstring completeScript = L"if(window.updateHelperText) { window.updateHelperText('封包发送完成'); }";
                                    PostScriptToUI(completeScript);
                                }).detach();
                            } else if (msg.find(L"stop_send") != std::wstring::npos) {
                                // 停止发送封包
                                StopAutoSend();
                                
                                // 更新辅助提示
                                std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('已停止发送封包'); }";
                                PostScriptToUI(script);
                            } else if (msg.find(L"enter_boss_battle") != std::wstring::npos) {
                                // 进入BOSS战斗
                                // UI发送的是bossId字段
                                std::wstring spiritIdStr = get_json_value(L"bossId");
                                if (!spiritIdStr.empty()) {
                                    uint32_t spiritId = (uint32_t)_wtol(spiritIdStr.c_str());
                                    if (spiritId > 10000) {
                                        // 发送战斗封包（BOSS 挑战：useId=32，extraParam=0）
                                        uint32_t useId = 32;
                                        uint8_t extraParam = 0;
                                        if (SendBattlePacket(spiritId, useId, extraParam)) {
                                            std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('已发送BOSS战斗封包，ID: " + std::to_wstring(spiritId) + L", useId: " + std::to_wstring(useId) + L"'); }";
                                            PostScriptToUI(script);
                                        } else {
                                            std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('发送战斗封包失败，未连接游戏服务器'); }";
                                            PostScriptToUI(script);
                                        }
                                    } else {
                                        std::wstring script = L"if(window.updateHelperText) { window.updateHelperText('无效的对象ID，必须大于10000'); }";
                                        PostScriptToUI(script);
                                    }
                                }
                            }

                            return S_OK;
                        }
                    };

                    g_webview->add_WebMessageReceived(new MessageHandler(), nullptr);
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
        L"卡布西游浮影微端 V1.04",
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
