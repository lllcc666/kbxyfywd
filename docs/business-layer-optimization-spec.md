# 业务层函数规范与优化方案

## 1. 目标

本规范用于整理当前项目的业务层函数使用方式，目标是：

- 不做大规模重构
- 不修改协议、封包格式和核心业务结果
- 统一函数职责、命名和调用边界
- 让后续新增功能可以沿用同一套组织方式

## 2. 适用范围

本规范主要约束以下文件和职责：

- `src/core/web_message_handler.cpp`
- `src/core/ui_bridge.cpp`
- `src/app/demo.cpp`
- `src/hook/wpe_hook.cpp`
- `src/hook/wpe_hook_helpers.cpp`
- `src/hook/horse_competition.cpp`
- `include/activities/*.h`
- `include/internal/*.h`
- `include/core/*.h`

不包含以下内容：

- `embedded/*.h` 生成文件
- `build_new/` 构建产物
- `swf_cache/` 缓存数据
- 协议常量、Opcode、Params 的重定义

## 3. 当前状态

当前业务层已经形成了几个清晰边界，但仍存在以下问题：

- `web_message_handler.cpp` 里仍有较长的消息分发链
- 一部分简单命令仍直接写在分支内部
- 同类函数的命名和职责没有完全对齐
- UI 文本拼接方式不统一
- 少量状态仍以“全局变量 + 局部 static”并存

最近已完成的低风险整理包括：

- `packet_window_visible`
- `delete_selected_packets`
- `clear_packets`
- `start_intercept`
- `stop_intercept`
- `set_intercept_type`
- `send_packet`
- `set_speed`
- `toggle_auto_heal`
- `set_block_battle`
- `set_auto_go_home`
- `query_lingyu`
- `query_monsters`
- `refresh_pack_items`
- `buy_dice`
- `buy_dice_18`
- `decompose_lingyu` 主入口已收口到命名 helper，并拆出 `batch` / `array` 子处理
- `decompose_lingyu` 的 `indices` 提取逻辑已收口为共享 helper
- `spiritCollect` 主入口已收口到命名 helper，并拆出 `open_ui`、`getSpirits`、`verifyPlayer`、`sendSpirit`、`history` 子处理
- `spiritCollect` 的结果提示逻辑已收口为共享 helper
- `send_all_packets` 的进度输出已收口为命名 helper
- `one_key_horse_competition` 的进度回调已收口为命名 helper
- `one_key_horse_competition` 的启动 worker 已收口为命名 helper
- `send_packet` / `battlesix_auto_match` / `dungeon_jump_start` 的线程入口已收口为命名 helper
- `one_key_act793` / `one_key_act791` / `one_key_act782` / `one_key_act803` / `spiritCollect open_ui` / `spiritCollect getSpirits` 已优先使用直接函数指针
- `one_key_act624` / `one_key_sea_battle` 已共用 `InvokeOneKeyActSend` 调用重载
- `send_packet` 已改为 helper 自己解析 `msg`
- `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 已共用布尔写入 helper
- `send_packet` / `battlesix_auto_match` / `dungeon_jump_start` 已共用线程启动 helper
- `one_key_horse_competition` / `send_all_packets` 已共用 detached 线程启动 helper
- 一批 bool 结果命令已共用 `HandleBoolResultCommand`
- `stop_task_zone` / `stop_shuangtai` / `stop_heaven_furui` / `stop_send` 已收口为 `HandleActionWithTextCommand`
- `update-dialog` / `key-login-dialog` / `spirit-confirm-dialog` 的 show/hide 分支已收口为 `HandleSetBrowserWindowVisibleCommand` wrapper
- 一批纯动作命令已收口为 `HandleActionCommand`
- `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 已收口为 `HandleSetBoolFlagCommand`
- `buy_item` / `use_item` / `enter_boss_battle` 已共用动态结果 helper
- `buy_item` / `use_item` 已共用包数据刷新 helper

## 4. 安全性与可行性评估

### 4.1 安全性结论

结论是高安全、低风险。

原因：

- 只调整 UI / 消息层的分发方式
- 不改封包布局，不改协议常量
- 不改网络发送顺序，不改响应解析逻辑
- 不跨模块挪动复杂业务流

### 4.2 可行性结论

结论是可行，且适合分阶段推进。

原因：

- 当前代码已经有天然的命令入口
- 许多分支本来就只有一到两行逻辑，适合提成 helper
- 提取 helper 不会改变业务行为
- 文档和清单可以同步跟进，便于控制边界

### 4.3 主要风险

- `msg.find(...)` 仍然可能产生子串命中
- 某些命令需要保留原有分支顺序
- 跨线程 UI 输出如果继续散落，后续会重新变乱
- 复杂业务流如果过早拆分，容易越界到别的模块

### 4.4 风险控制

- 只抽取“单步动作” helper
- 保持原有分支顺序
- 不改协议参数，不改业务结果
- 复杂流程先保留在原文件内

## 5. 函数使用规范

### 5.1 `Send*`

职责：

- 只负责构造和发送封包
- 允许做轻量参数校验

禁止：

- 等待长时间响应
- 内嵌复杂状态切换
- 直接拼接大量 UI 文本

建议命名：

- `SendXxxPacket`
- `SendXxxQueryPacket`
- `SendXxxStartPacket`
- `SendXxxEndPacket`

### 5.2 `Process*Response`

职责：

- 只负责解析响应
- 只做状态更新、下一步触发和结果上报

禁止：

- 重建完整业务流程
- 把 UI 路由逻辑塞进解析阶段

### 5.3 `Start*` / `Stop*`

职责：

- `Start*` 只启动一次业务周期
- `Stop*` 只停止、取消或清理状态

禁止：

- 在 `Stop*` 里做复杂恢复流程
- 在 `Start*` 里加入大量解析代码

### 5.4 `Query*`

职责：

- 只做查询
- 只表达“是否发出查询”

### 5.5 `Update*`

职责：

- 只更新本地状态或 UI 状态
- 不承担发送逻辑

### 5.6 `Handle*`

职责：

- 只做入口层拆分
- 适合承接消息解析、参数提取和单步动作调用

### 5.7 `Reset*`

职责：

- 只做清空和恢复默认值
- 不夹带额外副作用

### 5.8 `HandleActionWithTextCommand`

职责：

- 只承接“执行单步动作 + 统一提示文案”的轻量封装
- 适合 `Stop*` 中不需要额外状态恢复的命令

禁止：

- 在该 helper 中加入参数解析
- 在该 helper 中做复杂恢复或跨线程逻辑

适用例：

- `stop_task_zone`
- `stop_shuangtai`
- `stop_heaven_furui`
- `stop_send`

### 5.9 `HandleActionCommand`

职责：

- 只承接“执行单步动作、不回写固定文案”的纯动作封装
- 适合参数已解析完成后的轻量动作调用

禁止：

- 在该 helper 中加入状态判断或结果分支
- 在该 helper 中输出业务提示文案

适用例：

- `window-drag`
- `window-minimize`
- `window-close`
- `clear_packets`
- `start_intercept`
- `stop_intercept`
- `set_intercept_type`
- `query_lingyu`
- `query_monsters`
- `set_speed`
- `set_hijack_enabled`
- `open-url`
- `key-login`
- `buy_dice`
- `refresh_pack_items`
- `daily_tasks`
- `task_zone`
- `query_shuangtai`
- `stop_horse_competition`
- `clear_hijack_rules`
- `copy-login-key`
- `refresh-no-login`
- `mute-game`
- `clear-ie-cache`
- `refresh-game`

### 5.10 `HandleSetBoolFlagCommand`

职责：

- 只把消息里的布尔值写入对应状态
- 适合 `set_*` / `toggle_*` 这类“更新开关”命令的内部实现

禁止：

- 在 helper 中加入反转逻辑
- 在 helper 中混入额外业务动作

### 5.11 命名对齐

当路由行为是“发送、开始、设置、停止”时，内部 helper 应尽量使用同义前缀：

- `Send*`
- `Start*`
- `Stop*`
- `Set*`

避免使用与实际动作不一致的命名，例如把纯写入开关的逻辑写成 `Toggle*`。

## 6. 消息路由规范

### 6.1 当前规则

`src/core/web_message_handler.cpp` 负责：

- 识别消息类型
- 提取参数
- 调用业务函数

### 6.2 目标规则

建议后续统一成以下风格：

- 先解析命令名
- 再解析参数
- 最后调用具体 helper

如果仍然使用 `msg.find(...)`：

- 只保留路由层判断
- 不把业务细节继续堆到分支里
- 保持分支顺序稳定

### 6.3 已落地的路由拆分

当前已拆出的低风险路由应维持如下职责：

- `packet_window_visible` 负责窗口显示状态和矩形参数
- `delete_selected_packets` 负责索引解析和删除
- `clear_packets` 负责清空封包列表
- `start_intercept` / `stop_intercept` 负责截获开关
- `set_intercept_type` 负责截获类型
- `send_packet` 负责原始封包发送
- `set_speed` 负责速度修改
- `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 负责本地开关
- `query_lingyu` / `query_monsters` / `refresh_pack_items` / `buy_dice` / `buy_dice_18` 负责单步查询或发送
- `decompose_lingyu` 负责灵玉分解路由，内部再拆 `batch` / `array` 子处理
- `decompose_lingyu` 的参数提取由共享 helper 负责
- `spiritCollect` 负责精魄收集路由，内部再拆 `open_ui`、`getSpirits`、`verifyPlayer`、`sendSpirit`、`history`
- `spiritCollect` 的结果提示由共享 helper 负责
- `send_all_packets` 的进度文本由共享 helper 负责
- `one_key_horse_competition` 的进度文本和启动 worker 由共享 helper 负责
- `send_packet` / `battlesix_auto_match` / `dungeon_jump_start` 的线程入口由共享 helper 负责
- `one_key_act793` / `one_key_act791` / `one_key_act782` / `one_key_act803` / `spiritCollect open_ui` / `spiritCollect getSpirits` 由直接函数指针负责
- `one_key_act624` / `one_key_sea_battle` 由共享调用重载负责
- `one_key_act*` / `one_key_sea_battle` 的内部发送 helper 已对齐为 `HandleSendOneKeyActCommand` / `HandleStartOneKeySeaBattleCommand`
- `send_packet` 的参数解析由 helper 负责
- `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 的布尔写入由共享 helper 负责
- `send_packet` / `battlesix_auto_match` / `dungeon_jump_start` 的线程启动与失败清理由共享 helper 负责
- `one_key_horse_competition` / `send_all_packets` 的 detached 线程启动由共享 helper 负责
- 一批 bool 结果命令由共享 `HandleBoolResultCommand` 负责
- `stop_task_zone` / `stop_shuangtai` / `stop_heaven_furui` / `stop_send` 由共享 `HandleActionWithTextCommand` 负责
- `update-dialog` / `key-login-dialog` / `spirit-confirm-dialog` 的 show/hide 分支由 `HandleSetBrowserWindowVisibleCommand` 负责
- 一批纯动作命令由共享 `HandleActionCommand` 负责
- `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 由共享 `HandleSetBoolFlagCommand` 负责
- `buy_item` / `use_item` / `enter_boss_battle` 由动态结果 helper 负责
- `buy_item` / `use_item` 的包数据刷新由共享 helper 负责

## 7. 状态管理规范

### 7.1 单一 owner

每个业务状态只允许有一个明确 owner：

- 一个 `State` 结构
- 一个自动化对象
- 一个内部管理器

禁止在以下位置重复维护同一状态：

- 全局变量
- 成员变量
- 局部 `static`
- 临时线程参数

### 7.2 状态分类

建议把状态分成三类：

- 生命周期状态：是否运行、是否停止、是否等待
- 业务状态：分数、次数、房间状态、当前层数
- UI 状态：是否显示、提示文本、进度

### 7.3 线程安全

- 跨线程读写的标志优先使用 `std::atomic`
- 只在单线程访问的数据可以保留普通类型，但需要注明
- 状态切换尽量通过枚举或阶段标识，不要靠一组布尔值拼接

### 7.4 Reset 约束

所有状态 owner 都应提供统一重置入口：

- `Reset()`
- `Clear()`

重置必须恢复默认值，不附带额外业务动作。

## 8. UI 输出规范

### 8.1 统一出口

业务层优先使用以下方式输出 UI：

- `UIBridge::Instance().UpdateHelperText(...)`
- `UIBridge::Instance().ExecuteJS(...)`
- `SetHelperText(...)` 这类薄封装

### 8.2 代码规范

- 不要在每个业务函数里重复拼接相同的 JS 模板
- 不要把 UI 输出和封包构造写进同一个长函数
- 不要让 UI 路由层承担业务状态计算

### 8.3 文案规范

- 保留现有中文业务语义
- 同类提示尽量复用统一前缀
- 状态提示尽量使用“业务名：状态说明”的格式

## 9. 模块边界

### 9.1 `src/core/web_message_handler.cpp`

定位：

- 前端消息总入口
- 只做命令识别、参数解析、函数分发

要求：

- 不写活动细节
- 不写复杂业务状态机
- 不无限扩展 `msg.find(...)`

### 9.2 `src/core/ui_bridge.cpp`

定位：

- C++ 到 JavaScript 的统一输出层

要求：

- 业务层优先通过桥接层输出 UI
- JS 拼接尽量集中在桥接层

### 9.3 `src/hook/wpe_hook.cpp`

定位：

- Hook 生命周期
- 响应分发
- 通用封包工具

要求：

- 新增功能优先拆到独立活动模块
- 通用能力保留在此处

### 9.4 `include/activities/*.h`

定位：

- 活动对外声明

要求：

- 只放入口、响应入口和公开常量
- 不堆实现细节

### 9.5 `include/internal/*.h`

定位：

- 内部状态和私有辅助结构

要求：

- 业务状态优先放这里
- 不向公共头文件外泄内部细节

## 10. 设计原则

1. 最小修改优先
2. 保持向后兼容
3. 先规范，再归档，再考虑更大抽取
4. 保持协议和业务结果不变
5. 保留现有中文 UI 语义

## 11. 后续执行顺序

### 第一顺序

- 继续统一 `Handle*` 类路由 helper
- 继续把简单命令从长分支中抽离
- 保持当前消息入口可读

### 第二顺序

- 统一 `Send*` / `Process*Response` / `Start*` / `Stop*` 的职责
- 标记每个业务状态的唯一 owner
- 清理重复的布尔状态

### 第三顺序

- 如果需要，再按活动模块迁移到独立文件
- 避免一次性把 `wpe_hook.cpp` 大拆大改

## 12. 验收标准

- 不修改 `Opcode` / `Params`
- 不修改协议布局
- 不修改现有业务结果
- 不修改 generated 文件
- 编译通过
- 文档与代码状态一致
