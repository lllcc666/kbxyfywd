# AGENTS.md - 卡布西游微端项目指南

> **项目**: 基于 WebView2 的 Windows 桌面游戏辅助工具 (浮影微端 V1.04)
> **仓库**: https://github.com/lllcc666/kbxyfywd

## 技术栈

| 组件 | 技术 |
|------|------|
| 语言 | C++17 (Win32) |
| UI | WebView2 + HTML/CSS/JS |
| 游戏嵌入 | WebBrowser (IE/ATL CAxWindow) |
| 构建 | CMake 3.16+ |
| 包管理 | vcpkg |
| Hook | MinHook |
| 压缩 | zlib + minizip |
| 平台 | Windows x64 |

## 构建命令

```powershell
# 首次构建
mkdir build_new && cd build_new
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release

# 增量构建
cmake --build build_new --config Release

# 运行
.\build_new\bin\Release\WebView2Demo.exe
```

**注意**: vcpkg 路径在 `CMakeLists.txt:30` 硬编码为 `d:/AItrace/CE/.trae/vcpkg-master`

## 项目结构

```
kbwebui/
├── demo.cpp                 # 主窗口、WebView2 初始化、消息循环
├── wpe_hook.h/cpp           # 网络封包拦截、发送、响应等待
├── packet_parser.h/cpp      # 封包解析、Opcode 定义、数据结构
├── packet_builder.h/cpp     # 封包构建器（链式调用）
├── ui_bridge.h/cpp          # C++ ↔ JavaScript 通信桥梁
├── data_interceptor.h/cpp   # HTTP 响应拦截修改
├── utils.h/cpp              # 编码转换、线程同步、远程下载
├── resources/               # 图标、资源脚本、ui.html
├── embedded/                # 自动生成的嵌入数据头文件
├── data/                    # 游戏数据 XML 缓存
└── scripts/                 # Python 工具脚本
```

## 封包协议

```
┌──────────┬──────────┬──────────┬──────────┬──────────┐
│ Magic 2B │ Len 2B   │Opcode 4B │Params 4B │Body 变长 │
└──────────┴──────────┴──────────┴──────────┴──────────┘
```

- **Magic**: `0x5344` 普通包, `0x5343` 压缩包
- **所有数值**: 小端序
- **Opcode 计算**: `byte[0] | (byte[1]<<8) | (byte[2]<<16) | (byte[3]<<24)`

## 代码风格

### 命名约定

| 类型 | 风格 | 示例 |
|------|------|------|
| 类/函数 | `PascalCase` | `PacketBuilder`, `SendPacket()` |
| 变量 | `camelCase` | `gameData`, `isRunning` |
| 全局变量 | `g_` 前缀 | `g_hWnd`, `g_webview` |
| 常量 | `UPPER_SNAKE_CASE` | `HEADER_SIZE`, `TIMEOUT_NORMAL` |
| 命名空间 | `PascalCase` | `Opcode`, `PacketProtocol` |

### 代码规范

- **标准**: C++17, MSVC `/utf-8`
- **字符串**: 中文用 `std::wstring`, 英文用 `std::string`
- **线程同步**: RAII 风格 `CriticalSectionLock`
- **头文件**: `#pragma once`, Doxygen 注释
- **导入顺序**: Windows API → 标准库 → 第三方库 → 项目头文件

### 封包构建示例

```cpp
auto packet = PacketBuilder()
    .SetOpcode(1185429)
    .SetParams(770)
    .WriteString("game_info")
    .Build();
```

## 添加新功能

1. `packet_parser.h` → 定义 Opcode 常量
2. `wpe_hook.h` → 添加状态结构（如需要）
3. `wpe_hook.cpp` → 实现发送函数
4. `ResponseDispatcher::InitializeDefaultHandlers()` → 注册响应处理器
5. `demo.cpp` → 添加 JavaScript 调用入口

## 调试

- **仅用 WebView2 控制台输出**，无 debug 模式
- 使用封包拦截对比客户端发送的封包
- 注意 UTF-8 编码问题

## 版本号更新清单

发布时同步更新：

| 文件 | 位置 |
|------|------|
| `demo.cpp:84` | `CURRENT_VERSION = X.XXf` |
| `demo.cpp` | 窗口标题 `L"卡布西游浮影微端 VX.XX"` |
| `wpe_hook.cpp` | Hook 标题 |
| `resources/ui.html` | UI 标题 |
| `resources/app.rc` | 版本信息 |

## 常见问题

- **编译报错找不到头文件**: 检查 vcpkg 路径配置
- **版本号一致仍弹更新**: 确保版本精度一致（如 `1.04f`）
