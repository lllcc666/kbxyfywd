# 卡布西游浮影微端

[![Version](https://img.shields.io/badge/version-1.18-blue.svg)](https://github.com/lllcc666/kbxyfywd)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey.svg)](https://github.com/lllcc666/kbxyfywd)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

基于 WebView2 的 Windows 游戏辅助桌面程序：Win32 C++17 外壳 + 嵌入式 WebView2 界面 + MinHook 封包拦截 + 各活动自动化模块。产物是**单文件 exe**，不依赖外置 zlib / 内存模块 DLL。

## 项目特点

- 界面用 WebView2 承载，前端源码是 `resources/ui.html`，构建时嵌入成 `embedded/ui_html.h`
- 游戏页面用 ATL `CAxWindow` + `IWebBrowser2` 承载
- 封包拦截（MinHook）、协议解析、活动自动化、C++ ↔ JS 数据桥接
- zlib 1.3.1 以源码形式编入（`third_party/zlib`），不再链接 `zlib1.dll`
- 自动化流程统一走受管线程：可取消、退出时可控回收

## 技术栈

| 组件 | 说明 |
|------|------|
| 语言 | C++17（`/utf-8`，MSVC `/W4`；未开 `/WX`，告警不阻断构建） |
| 桌面框架 | Win32 API |
| UI | WebView2 + HTML/CSS/JavaScript |
| 游戏承载 | ATL / WebBrowser |
| Hook | MinHook |
| 压缩/加载 | zlib 1.3.1（源码静态编入）/ MemoryModule |
| 构建 | CMake 3.16+，Visual Studio 2022 生成器 |
| 依赖管理 | vcpkg（仅 MemoryModule 与 WebView2Loader） |

## 环境要求

- Windows 10/11 x64
- Visual Studio 2022（MSVC v143）
- CMake 3.16 及以上
- Python 3，且可通过 `python3` 命令调用（构建期生成 `embedded/*.h`）
- vcpkg（提供 `MemoryModule.lib` 与 `WebView2Loader.dll`）

`CMakeLists.txt` 里 `VCPKG_ROOT` 默认硬编码为：

```text
d:/AItrace/CE/.trae/vcpkg-master
```

本地路径不同时先改这一处。

## 构建方式

```powershell
cmake -S . -B build_new -G "Visual Studio 17 2022" -A x64
cmake --build build_new --config Release --target WebView2Demo
```

产物：`build_new/bin/Release/WebView2Demo.exe`

Release 是体积优先配置：`/O1` + `/GL` + `/LTCG` + `/GF` + `/Gy` + `/Gw`、函数级链接、COMDAT 折叠、引用优化；运行时库为 `/MD`（目标机需 VC++ 运行库）。成品可再自行加壳（如 UPX）。

## 目录结构

```text
kbwebui/
├─ src/
│  ├─ app/                  # 程序入口、主窗口、WebView2 初始化
│  ├─ core/                 # 桥接、工具、消息分派、受管线程
│  ├─ hook/                 # Hook 内核 + 各活动自动化模块（一活动一文件）
│  └─ protocol/             # 封包解析与构造
├─ include/
│  ├─ activities/           # 各活动对外接口声明
│  ├─ core/                 # core 层头文件（含 bounded_reader / managed_thread）
│  ├─ hook/                 # hook 层公开头文件
│  ├─ internal/             # 内部状态结构、响应分发、模块间共享状态
│  └─ protocol/             # 协议类型、Opcode 常量
├─ resources/               # ui.html、app.rc、app.ico
├─ embedded/                # 嵌入资源头文件
├─ third_party/zlib/        # zlib 1.3.1 源码（静态编入）
├─ scripts/                 # Python 生成/抓取脚本
├─ CMakeLists.txt
└─ README.md
```

## 代码结构

### `src/app/`

- `demo.cpp` — 程序入口、窗口生命周期、WebView2 初始化、退出顺序（先停受管线程再拆钩子）
- `app_host.cpp` — 窗口宿主与消息处理

### `src/core/`

- `web_message_handler.cpp` — 前端消息 → 原生逻辑的唯一入口（动作分派）
- `ui_bridge.cpp` — C++ → JS 桥接（`ExecuteScript`、进度横幅、列表同步）
- `managed_thread.cpp` — 受管线程：统一登记、可取消睡眠、退出时 join / 回收
- `utils.cpp` — 字符串、数值、十六进制等基础工具

> HTTP 资源拦截没有独立模块：IE 缓存清理与 WebView2 资源处理都在 `src/app/demo.cpp` 里。

### `src/protocol/`

- `packet_parser.cpp` — 封包解析、Opcode/Params 识别、zlib 解压、data.zip 解析、战斗数据
- `battle_session.cpp` — 战斗会话唯一状态源（Begin/Replace/End/Snapshot + sessionId）
- `packet_builder.cpp` — 封包构造

### `src/hook/`（一活动一模块）

| 文件 | 职责 |
|------|------|
| `wpe_hook.cpp` | Hook 生命周期、收发封包、响应分发（`ResponseDispatcher`）、活动封包与次数查询入口 |
| `wpe_hook_helpers.cpp` | 登录 / 会话等辅助 Hook |
| `response_waiter.cpp` | 同步等待回包（opcode + params 精确匹配、可取消） |
| `hook_shared.cpp` | 模块间共享原语：发包、通知、奖励表构建 |
| `horse_competition.cpp` | 自动坐骑大赛 |
| `latest_activities.cpp` | 最新活动通用流程（查询 / 开始 / 扫荡 / 领取） |
| `activity_minigames.cpp` | 各期小游戏自动化实现 |
| `activity_bus.cpp` | 活动通用启动器（障碍赛跑 / 金平府祈愿 / 五子棋 / 知识小天地） |
| `little_game.cpp` | 小游戏开始 / 结束发包（两种 body 形态） |
| `activity_counts.cpp` | 活动剩余次数查询与回填 |
| `story_task.cpp` | 主线任务（含战斗收尾、配方表驱动） |
| `eight_trigrams.cpp` | 任务区 / 八卦灵盘 |
| `battle_six.cpp` | 战斗六人本（战斗流程阶段机 + 看门狗） |
| `battle.cpp` | 通用战斗发包（CLICK_NPC / BATTLE_READY / BATTLE_PLAY_OVER / BATTLE_END_SEND） |
| `battle_activity_hooks.cpp` | 协议层 → 活动层的战斗事件窄接口 |
| `shuangtai.cpp` | 双台自动战斗 |
| `spirit_collect.cpp` | 精灵采集 |
| `dungeon_jump.cpp` / `dungeon_deepdig.cpp` | 跳图 / 地宫挖宝 |
| `tower.cpp` | 玄塔 |
| `trial.cpp` | 试炼 |
| `dance.cpp` | 跳舞小游戏 |
| `daily_activity.cpp` | 日常 / 周常 / 家族 / 回血 |
| `collect.cpp` | 一键采集 |
| `bag_items.cpp` | 灵玉、背包查询与分解、道具使用 |
| `md5_check.cpp` | 图片 MD5 校验路径 |
| `packet_store.cpp` | 封包列表存取、自动发包的数据层 |
| `hijack.cpp` | 封包劫持规则 |

### `include/internal/`

- `wpe_hook_internal.h` — `ResponseDispatcher`、`ResponseWaiter`、跨模块共享全局与口径
- `hook_shared.h` — 发包 / 通知 / 奖励表等共享原语声明
- `activity_states_internal.h` — `ActivityStateManager` 与各活动状态结构
- `task_zone_internal.h` / `story_task_internal.h` / `shuangtai_internal.h` — 模块内部共享状态

## 资源与生成文件

### 源资源

- `resources/ui.html`、`resources/app.rc`、`resources/app.ico`

### 构建生成（勿手改）

- `embedded/ui_html.h`（来自 `resources/ui.html`）
- `embedded/webview2loader_data.h`（来自 vcpkg 的 `WebView2Loader.dll`）

### 仓库内手工维护的嵌入头

- `embedded/minhook_data.h`、`embedded/speed_x64_data.h`、`embedded/minizip_helper.h`

## Python 脚本

| 脚本 | 用途 |
|------|------|
| `embed_html.py` | `ui.html` → 嵌入头文件 |
| `embed_dll.py` | DLL → 嵌入头文件 |
| `download_swf.py` | 下载活动 SWF 到 `swf_cache/` |
| `fetch_activities.py` / `fetch_taskprop.py` | 抓取活动配置 / 任务属性 |
| `gen_story_recipe.py` | 由 `data/taskinfo.xml` 生成主线节点配方（输出 `include/activities/story_task_recipe.h`） |

## 协议说明

基础封包格式：

```text
Magic(2) + Length(2) + Opcode(4) + Params(4) + Body
```

- 普通包 Magic：`0x5344`
- 压缩包 Magic：`0x5343`
- 整数值小端

活动回包按 `(opcode, params)` 精确注册到 `ResponseDispatcher`：先查精确表，再退 opcode 级表。需要同步等结果的流程用 `ResponseWaiter`（200ms 分片、可被停止打断）。

## 开发约定

### 新增一个活动

1. `include/protocol/packet_protocol.h` 补 Opcode / 参数常量，不在业务代码里写魔数
2. `include/activities/<活动>.h` 加对外接口，`include/internal/activity_states_internal.h` 加状态结构
3. `src/hook/<活动>.cpp` 实现流程，并在 `src/hook/wpe_hook.cpp` 里 `registerParams(...)` 注册回包处理器
4. `src/core/web_message_handler.cpp` 接入前端动作，`resources/ui.html` 加界面
5. **新增 `.cpp` / `.h` 必须同步登记到 `CMakeLists.txt`**（源文件是显式列表，不是通配）
6. 头文件以 `include/` 各子目录为搜索根，`#include` 用短名（如 `"managed_thread.h"`），不要写 `"core/managed_thread.h"`

### 线程与退出

- 后台流程统一用 `ManagedThread::Start(...)`（或 `CreateManagedThread`），睡眠用 `ManagedThread::SleepCancellable(...)`
- 循环体每轮检查 `ManagedThread::StopRequested()`
- 退出顺序：`RequestStopAll()` → `JoinAll()` → `CleanupWpeHook()` → `StopIntercept()` → `UninitializeHooks()`。
  - `JoinAll` 超时（仍有线程在跑）时必须**跳过后面全部清理**直接退出进程：此时拆钩子会让残留线程跳到已卸载的代码上继续发包。
  - `CleanupWpeHook` 必须排在 `UninitializeHooks` 之前：它内部要 join 赛马主循环，而该线程可能正阻塞在等退房回包上，收包链必须还活着。
  - `UninitializeHooks` 是「断电」动作（禁 Hook + 取消等待器 + 停收包分发线程 + 等在途 detour 排空后才卸载 MinHook），必须排最后。

### 解析封包

- 读信封字段用 `BoundedReader`（`include/core/bounded_reader.h`），越界只置失败位，不做越界读
- 跨线程共享的活动状态用 `std::atomic` 或模块内互斥锁；`std::wstring` / `std::vector` 字段必须同锁访问

### 修改前端

- 只改 `resources/ui.html`，重新构建会自动重新生成 `embedded/ui_html.h`
- 从 C++ 注入 JS 时，**任何来自封包 / 配置文件 / 用户输入的字符串**都必须走 `UIBridge::EscapeJsonString`；拼进单引号字面量时它已一并转义单引号。纯数字（`std::to_wstring`）与代码内字面量可以直接拼。
- 不要用 `msg.find(L"键名")` 之外的方式猜前端意图：动作名是唯一契约，新增动作要在 `resources/ui.html` 与 `web_message_handler.cpp` 同时登记。

## 验证方式

仓库没有自动化测试，常用验证：

```powershell
cmake --build build_new --config Release --target WebView2Demo
.\build_new\bin\Release\WebView2Demo.exe
```

检查点：界面正常加载 → Hook 生效（能抓到封包）→ 目标活动流程能跑完一轮 → 退出程序不残留进程。

## 常见问题

- **找不到 vcpkg 依赖**：核对 `CMakeLists.txt` 里的 `VCPKG_ROOT`。
- **前端改了不生效**：需重新构建，让 `resources/ui.html` 重新生成 `embedded/ui_html.h`。
- **新增源码没被编译**：确认已加入 `CMakeLists.txt` 的源文件列表。
- **exe 体积**：主体是嵌入资源（UI 与两个 DLL 的十六进制数组），代码侧已压到 `/O1 /GL /LTCG`；要更小请在外层加壳。

## 许可证

MIT License

## 相关链接

- GitHub 仓库：https://github.com/lllcc666/kbxyfywd
- 问题反馈：https://github.com/lllcc666/kbxyfywd/issues
