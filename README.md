# 卡布西游浮影微端

[![Version](https://img.shields.io/badge/version-1.11-blue.svg)](https://github.com/lllcc666/kbxyfywd)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-lightgrey.svg)](https://github.com/lllcc666/kbxyfywd)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

基于 WebView2 的 Windows 游戏辅助桌面程序，使用 Win32 C++17、ATL/WebBrowser、MinHook 和嵌入式资源实现。

## 项目特点

- 使用 WebView2 承载工具界面，前端资源来自 `resources/ui.html`
- 使用 ATL `CAxWindow` + `IWebBrowser2` 承载游戏页面
- 提供网络封包拦截、协议解析、活动自动化、数据桥接等能力
- 构建时将 HTML、WebView2Loader、zlib 等资源嵌入到可执行文件中
- 以 Windows x64 + MSVC + CMake 为主要开发环境

## 技术栈

| 组件 | 说明 |
|------|------|
| 语言 | C++17 |
| 桌面框架 | Win32 API |
| UI | WebView2 + HTML/CSS/JavaScript |
| 游戏承载 | ATL / WebBrowser |
| Hook | MinHook |
| 压缩/加载 | zlib / MemoryModule |
| 构建 | CMake 3.16+ |
| 依赖管理 | vcpkg |

## 环境要求

- Windows 10/11 x64
- Visual Studio 2019 或 2022
- CMake 3.16 及以上
- Python 3，且可通过 `python3` 命令调用
- vcpkg

## 构建方式

仓库根目录下的 `CMakeLists.txt` 当前硬编码了：

```text
d:/AItrace/CE/.trae/vcpkg-master
```

如果你的本地环境路径不同，需要先调整该路径或修改 CMake 配置。

### 生成工程

```powershell
cmake -S . -B build_new -G "Visual Studio 17 2022" -A x64
```

### 编译 Release

```powershell
cmake --build build_new --config Release
```

### 运行程序

```powershell
.\build_new\bin\Release\WebView2Demo.exe
```

## 目录结构

```text
kbwebui/
├─ src/
│  ├─ app/                  # 程序入口与主窗口初始化
│  ├─ core/                 # 桥接、工具函数、消息处理、数据拦截
│  ├─ hook/                 # Hook 主逻辑与自动化实现
│  └─ protocol/             # 协议解析与封包构建实现
├─ include/
│  ├─ activities/           # 各活动/功能模块声明
│  ├─ core/                 # core 层头文件
│  ├─ hook/                 # hook 层公开头文件
│  ├─ internal/             # 内部状态机、等待器、私有结构
│  └─ protocol/             # 协议相关类型与声明
├─ resources/               # UI、图标、RC 资源
├─ embedded/                # 嵌入式头文件与嵌入资源
├─ data/                    # 本地 XML 数据缓存
├─ scripts/                 # Python 脚本工具
├─ swf_cache/               # 下载缓存，不作为源码
├─ build_new/               # 本地构建输出目录
├─ CMakeLists.txt
├─ README.md
└─ AGENTS.md
```

## 核心文件说明

- `src/app/demo.cpp`
  - 程序入口、窗口生命周期、WebView2 初始化、全局 UI 流程
- `src/hook/wpe_hook.cpp`
  - Hook 生命周期、数据包拦截、响应分发、自动化主逻辑
- `src/hook/wpe_hook_helpers.cpp`
  - Hook 层辅助实现
- `src/protocol/packet_parser.cpp`
  - 协议解析、Opcode/Params 相关处理、解压逻辑
- `src/protocol/packet_builder.cpp`
  - 封包构造辅助
- `src/core/ui_bridge.cpp`
  - C++ 到前端 JavaScript 的桥接
- `src/core/web_message_handler.cpp`
  - 前端消息到原生逻辑的入口
- `src/core/data_interceptor.cpp`
  - HTTP/资源响应拦截与修改
- `include/activities/*.h`
  - 活动声明、状态访问接口、自动化入口函数
- `include/internal/*.h`
  - 内部状态结构、等待器、状态机

## 资源与生成文件

### 源资源

- `resources/ui.html`
- `resources/app.rc`
- `resources/app.ico`

### 构建生成

以下文件由构建过程生成，不建议手工编辑：

- `embedded/ui_html.h`
- `embedded/webview2loader_data.h`
- `embedded/zlib_data.h`

### 仓库内维护的嵌入头文件

以下文件位于 `embedded/`，但不是当前 CMake 自定义命令生成的：

- `embedded/minhook_data.h`
- `embedded/speed_x64_data.h`
- `embedded/minizip_helper.h`

## Python 脚本

- `scripts/embed_html.py`
  - 将 `resources/ui.html` 转成嵌入头文件
- `scripts/embed_dll.py`
  - 将 DLL 转成嵌入头文件
- `scripts/download_swf.py`
  - 下载活动 SWF 到本地缓存目录
- `scripts/fetch_activities.py`
  - 获取活动数据/缓存辅助脚本

## 协议说明

当前项目中的基础封包格式为：

```text
Magic(2) + Length(2) + Opcode(4) + Params(4) + Body
```

- 普通包 Magic: `0x5344`
- 压缩包 Magic: `0x5343`
- 协议数值按小端处理

## 开发建议

### 添加新活动功能

通常按下面顺序扩展：

1. 在协议层补充所需常量、Opcode、数据结构
2. 在 `include/activities/` 中增加活动声明
3. 在 `include/internal/` 中补充内部状态结构（如果需要）
4. 在 `src/hook/wpe_hook.cpp` 中实现发送、响应处理和自动化流程
5. 在 `src/core/web_message_handler.cpp` 或 `src/app/demo.cpp` 中接入 UI 入口

### 修改前端

- 优先修改 `resources/ui.html`
- 通过重新构建生成新的 `embedded/ui_html.h`
- 不要直接手改 `embedded/ui_html.h`

### 修改构建配置

- 入口文件与实现文件现在统一从 `src/` 目录编译
- 公共头文件按 `include/` 子目录分类
- 如果新增源文件，需要同步更新 `CMakeLists.txt`

## 验证方式

当前仓库没有配置自动化测试框架，常用验证方式为：

- 重新编译：`cmake --build build_new --config Release`
- 启动程序：`.\build_new\bin\Release\WebView2Demo.exe`
- 检查 UI 是否正常加载
- 检查相关活动功能、Hook 流程和响应处理是否仍然正常

## 常见问题

### 构建时找不到 vcpkg 依赖

检查 `CMakeLists.txt` 中的 `VCPKG_ROOT` 是否与你的本地路径一致。

### 修改了前端但程序里没有生效

确认是否重新执行了构建，让 `resources/ui.html` 重新生成到 `embedded/ui_html.h`。

### 新增了源码文件但编译没有包含

确认文件已经放到正确目录，并已同步加入 `CMakeLists.txt`。

## 许可证

MIT License

## 相关链接

- GitHub 仓库：https://github.com/lllcc666/kbxyfywd
- 问题反馈：https://github.com/lllcc666/kbxyfywd/issues
