# AGENTS.md

给在本仓库里写代码的 AI / 人类协作者的硬约束。这里的每一条都对应一次已经发生过的真实故障，
不是风格偏好。违反其中任何一条，改动应被拒绝合入。

详细的问题清单与整改依据见 `docs/code-audit-report.md`。

## 构建

```bash
cd "D:/AItrace/CE/.trae/kbwebui" && env -u NO_PROXY -u no_proxy -u HTTP_PROXY -u http_proxy \
  -u HTTPS_PROXY -u https_proxy cmd //c "cmake --build build_new --config Release --target WebView2Demo"
```

必须剥掉成对的大小写重复环境变量，否则 MSBuild 报
`MSB6001 ... 字典中的关键字:"NO_PROXY"所添加的关键字:"no_proxy"`。
成功标志：`WebView2Demo.vcxproj -> ...\build_new\bin\Release\WebView2Demo.exe`。

`/W4` 已开、`/WX` 未开。**新增代码不得引入新告警**（尤其 C4244 / C4267 收窄、C4100 未用参数）。

仓库没有自动化测试，改动必须靠「能编译 + 手工跑一轮目标活动」验证。

## 线程与退出（P0）

- 后台流程统一 `ManagedThread::Start(name, body)` 或 `CreateManagedThread(..., name)`。
  **禁止裸 `std::thread` + `detach()`** —— 脱离登记表的线程在退出时无人 join，
  会在已卸载的 MinHook 代码上继续发包。
- 线程名必填且要有区分度。全部叫 `"raw"` 时 `JoinAll` 超时日志等于没有信息。
- 睡眠一律 `ManagedThread::SleepCancellable(ms)`，禁止裸 `Sleep(ms)`。
  裸 Sleep 期间线程看不到停止请求，`RequestStopAll` 后 `JoinAll` 只能超时并 detach。
- 循环体每轮检查 `ManagedThread::StopRequested()`；用会话号（session / token）取消的模块，
  要在长睡眠前后各比对一次会话号。
- **退出顺序固定为**：
  `RequestStopAll()` → `JoinAll(timeout)` → `CleanupWpeHook()` → `StopIntercept()` → `UninitializeHooks()`。
  - `JoinAll` 仍有线程未退（返回非 0）时，**跳过后面全部清理直接退出进程**。
    宁可泄漏几十 KB 映射，也不能让残留线程跳到已释放内存。
  - `CleanupWpeHook` 必须在 `UninitializeHooks` 之前：它内部要 join 赛马主循环，
    而该线程可能正阻塞在等退房回包上，收包链必须还活着。
  - `UninitializeHooks` 是「断电」动作：禁 Hook → 取消等待器 → 停收包分发线程 →
    **等在途 detour 计数归零** → 才卸载 MinHook。

## 封包解析（P0）

- 读 body 一律用 `ByteReader`（`include/protocol/byte_reader.h`）或它上面的
  `BoundedReader`（`include/core/bounded_reader.h`）。**禁止裸 `body[i]` / 指针算术取字段**，
  也不要用已删除的 `ReadInt32LE` 系列自由函数（无边界、自增 offset，是越界读的根因）。
- 读取器的语义是「越界不推进 offset、返回 0、置 fail 位」。因此手写守卫不再是安全的前提，
  但仍然不要删掉语义性的守卫（例如「这个分支要求至少有 N 字节才成立」）。
- 新增 opcode 必须在 `include/protocol/packet_protocol.h` 的 `namespace Opcode` 里命名，
  **一个数值只准有一个名字**。需要贴合语境的别名要先确认协议层确实同一条，否则另起数值。
- 注册回包处理器时核对**请求侧真实使用的 opcode**。历史上出现过三处注册的 opcode 与
  请求侧根本不是同族（1316503 / 1318401 / 1185685），注册了但永远不会触发。

## 注入 JS（P0）

- 任何来自封包 / 配置文件 / 用户输入的字符串，拼进 JS 前必须走
  `UIBridge::EscapeJsonString`。它已一并转义单引号。
- 纯数字（`std::to_wstring`）与代码内字面量可以直接拼。
- 禁止把字符串塞进 `\"...\"` 或 `'...'` 里直接 `script +=`。

## 安全（P0）

- **不得关闭 TLS 证书校验**。`WINHTTP_OPTION_SECURITY_FLAGS` 不允许写
  `SECURITY_FLAG_IGNORE_UNKNOWN_CA` / `IGNORE_CERT_DATE_INVALID` / `IGNORE_CERT_CN_INVALID`。
  更新检查走的就是这条链路，关掉校验等于允许中间人替换下载地址。
- 面向本机的原始发包接口（`send_packet`）是设计功能，不要「顺手加个校验」，
  但也不要在它周边新增特权路径。
- 跨线程共享的状态必须 `std::atomic` 或同锁访问。`bool` / `HWND` 裸全局被两个线程读写
  就是 UB，不要以「实际上不会同时发生」为由放过。

## 状态与常量

- 真相源只能有一份：版本号在 `include/hook/hook_lifecycle.h`（`APP_VERSION_NUM` /
  `APP_WINDOW_TITLE_W`），窗口类名 / 登录 URL / 标题栏高度在 `include/core/app_host.h`
  （`kMainWindowClass` / `kLoginUrl` / `kTitleBarHeight`）。**不要在业务代码里再写一遍字面量**。
- 活动 ID、opcode、超时、上限值都要有名字。禁止 `76`、`128`、`1024`、`392` 这类魔数。
- 结构体字段必须有人读。只写不读的字段一律删除 —— 它们会让人误以为状态被跟踪了。
- 停止函数的正确形态是「置停止标志 + 等线程真正收尾」，不是只 `Reset()` 状态结构。
  恢复现场（取回已存仓库的妖怪、还原首发等）**不受停止标志影响**：中途停止更要恢复。

## 注释

- 注释要写「为什么」，尤其是反直觉的地方（为什么不能先关拦截、为什么用会话号而不是
  StopRequested、为什么这里必须持锁）。
- 注释与代码不符比没有注释更糟。改行为时同步改注释；
  发现注释描述的是旧设计，删掉而不是留着。

## 禁止事项

- 不要手工编辑生成文件：`embedded/ui_html.h`、`embedded/webview2loader_data.h`、
  `include/activities/story_task_recipe.h`（由 `scripts/` 下的脚本生成）。
- 新增 `.cpp` / `.h` 必须登记到 `CMakeLists.txt`（源文件是显式列表，不是通配）。
- 头文件 `#include` 用短名（`"managed_thread.h"`），搜索根是 `include/` 各子目录。
- 不要为了「防御性」给每个入口都加判空 —— `ShowWindow` / `PostMessage` 对空句柄本身安全。
  防御要加在真实会失败的地方（`get_CoreWebView2` 失败、`Build()` 返回空 vector、
  `SendPacket` 超时），并且失败必须能传到调用方，不能吞掉后 `return true`。
