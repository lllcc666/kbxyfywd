# 重构进度

日期：2026-04-08

## 当前阶段

第一阶段已经完成，第二阶段已开始，当前在做 UI / 消息层的命名对齐与轻量包装整理。

## 已完成

- 已输出业务层函数规范与优化方案
- 已确认当前边界：不改协议、不改封包格式、不拆大业务模块
- 已将 `web_message_handler.cpp` 的常用 UI 输出收口到 `UIBridge`
- 已将 `packet_window_visible`、`delete_selected_packets`、`clear_packets`、`start_intercept`、`stop_intercept`、`set_intercept_type`、`send_packet` 拆成命名 helper
- 已把 `set_speed`、`toggle_auto_heal`、`set_block_battle`、`set_auto_go_home`、`query_lingyu`、`query_monsters`、`refresh_pack_items`、`buy_dice`、`buy_dice_18` 接到 helper 路由
- 已修复 `packet_window_visible` 在 helper 提取后丢失窗口矩形参数的问题
- 已清理 `send_packet` 分支里临时保留的 `#if 0` 旧实现
- 已通过 `cmake --build build_new --config Release --target WebView2Demo`
- 已将 `one_key_collect`、`one_key_xuantta`、`battlesix_auto_match`、`battlesix_set_auto_battle`、`dungeon_jump_start`、`dungeon_jump_stop`、`one_key_horse_competition` 和 `enter_boss_battle` 收口到命名 helper
- 已将 `buy_item`、`use_item`、`send_all_packets` 和 `one_key_act793` / `one_key_act791` / `one_key_act782` / `one_key_act803` / `one_key_act624` / `one_key_sea_battle` 收口到命名 helper
- 已将 `decompose_lingyu` 收口到命名 helper，并拆出 `batch` / `array` 子处理
- 已将 `decompose_lingyu` 的 `indices` 提取逻辑收口为共享 helper
- 已将 `spiritCollect` 收口到命名 helper
- 已将 `send_all_packets` 的进度输出收口为命名 helper
- 已将 `window-drag`、`window-minimize`、`window-close`、`update-dialog-show/hide`、`key-login-dialog-show/hide`、`spirit-confirm-dialog-show/hide`、`open-url`、`refresh-game`、`refresh-no-login`、`mute-game`、`clear-ie-cache`、`copy-login-key`、`key-login` 和 `add_hijack_rule` 收口到命名 helper
- 已将 `spiritCollect` 内部动作拆成 `open_ui`、`getSpirits`、`verifyPlayer`、`sendSpirit`、`history` 的命名 helper
- 已将 `spiritCollect` 的结果提示收口为共享 helper
- 已将 `one_key_horse_competition` 的进度回调收口为命名 helper
- 已将 `one_key_horse_competition` 的启动 worker 收口为命名 helper
- 已将 `send_packet` / `battlesix_auto_match` / `dungeon_jump_start` 的线程入口收口为命名 helper
- 已将 `one_key_act793` / `one_key_act791` / `one_key_act782` / `one_key_act803` / `spiritCollect open_ui` / `spiritCollect getSpirits` 改为直接函数指针调用
- 已将 `one_key_act624` / `one_key_sea_battle` 改为共享调用重载调用
- 已将 `query_lingyu` / `query_monsters` / `refresh_pack_items` / `buy_dice` / `clear_packets` / `query_shuangtai` / `battlesix_auto_match` / `battlesix_cancel_match` / `battlesix_set_auto_battle` / `one_key_collect` / `one_key_xuantta` / `one_key_horse_competition` 的内部 wrapper 命名对齐
- 已将 `one_key_act*` / `one_key_sea_battle` 的内部启动 helper 命名对齐为 `HandleStartOneKeyActCommand` / `HandleStartOneKeySeaBattleCommand`
- 已将 `one_key_act*` / `one_key_sea_battle` 的一键入口对齐为 `StartOneKeyAct*` / `StartOneKeySeaBattlePacket`
- 已将 `one_key_act793` / `one_key_act791` / `one_key_act782` / `one_key_act803` / `one_key_act624` 的路由处理函数对齐为 `HandleStartOneKeyAct793Command` / `HandleStartOneKeyAct791Command` / `HandleStartOneKeyAct782Command` / `HandleStartOneKeyAct803Command` / `HandleStartOneKeyAct624Command`
- 已将 `one_key_xuantta` 的路由处理函数对齐为 `HandleStartOneKeyXuanttaCommand`
- 已将 `send_packet` / `send_all_packets` 的路由处理函数对齐为 `HandleSendRawPacketCommand` / `HandleStartSendAllPacketsCommand`
- 已将 `query_shuangtai` 的路由处理函数对齐为 `HandleQueryShuangTaiMonstersCommand`
- 已将 `query_lingyu` / `query_monsters` / `refresh_pack_items` / `buy_dice` / `query_shuangtai` / `battlesix_auto_match` / `battlesix_set_auto_battle` / `dungeon_jump_start` / `dungeon_jump_stop` 的路由处理函数继续对齐为 `HandleQuery*` / `HandleRefresh*` / `HandleBuy*` / `HandleStart*` / `HandleStop*` / `HandleSet*`
- 已将 `battlesix_auto_match` / `dungeon_jump_start` / `one_key_horse_competition` 的内部 worker 命名继续对齐为 `HandleBattleSixAutoMatchWorker` / `HandleDungeonJumpWorker` / `HandleOneKeyHorseCompetitionWorker`
- 已再次通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮 one-key 活动命名收口
- 已再次通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮消息层路由命名收口
- 已再次通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮 query / start / set 路由别名继续收口
- 已再次通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮内部 worker 命名收口
- 已再次通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮 `send_packet` / `send_all_packets` / `query_shuangtai` 以及 worker 收口
- 已记录外部消息名的兼容封装点，当前仍保留原有 `toggle_*` / `query_*` / `refresh_*` 以及浏览器相关路由名
- 已将 `send_packet` 改为 helper 自己解析 `msg`
- 已将 `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 改为共享布尔写入 helper
- 已将 `send_packet` / `battlesix_auto_match` / `dungeon_jump_start` 的线程启动与失败清理收口为共享 helper
- 已将 `one_key_horse_competition` / `send_all_packets` 的 detached 线程启动收口为共享 helper
- 已将一批 bool 结果命令收口为共享 `HandleBoolResultCommand`
- 已将 `stop_task_zone` / `stop_shuangtai` / `stop_heaven_furui` / `stop_send` 收口为 `HandleActionWithTextCommand`
- 已将 `update-dialog` / `key-login-dialog` / `spirit-confirm-dialog` 的 show/hide 分支收口为 `HandleSetBrowserWindowVisibleCommand`
- 已将一批纯动作命令收口为 `HandleActionCommand`
- 已将 `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 的布尔写入 helper 命名对齐为 `HandleSetBoolFlagCommand`
- 已将 `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 的路由 wrapper 命名对齐为 `HandleSetAutoHealEnabledCommand` / `HandleSetBlockBattleEnabledCommand` / `HandleSetAutoGoHomeEnabledCommand`
- 已将 `daily_tasks` / `task_zone` 的路由 wrapper 命名对齐为 `HandleStartDailyTasksCommand` / `HandleStartTaskZoneCommand`
- 已将 `daily_tasks` / `stop_horse_competition` 收口为 `HandleActionCommand`
- 已将 `set_speed` / `set_intercept_type` / `set_hijack_enabled` 收口为 `HandleActionCommand`
- 已将 `open-url` / `refresh-game` / `refresh-no-login` / `mute-game` / `clear-ie-cache` / `copy-login-key` / `key-login` / 浏览器可见性 wrapper 收口为命名 helper，内部继续复用 `HandleActionCommand`
- 已整理剩余浏览器 wrapper 命名（`HandleRefresh*` / `HandleOpen*` / `HandleMute*`）
- 已将 `SendDailyTasksAsync` / `SendEightTrigramsTaskAsync` / `SendOneKeyCollectPacket` 对齐为 `StartDailyTasksAsync` / `StartEightTrigramsTaskAsync` / `StartOneKeyCollectPacket`
- 已将剩余一批真正承担“启动整套流程”的入口对齐为 `StartOneKeyTowerPacket` / `StartOneKeyBattleSixPacket` / `StartOneKeyDungeonJumpPacket` / `StartOneKeyShuangTaiPacket` / `StartOneKeyHorseCompetitionPacket` / `StartOneKeyHeavenFuruiPacket`，并将战斗六取消入口对齐为 `CancelBattleSixMatch`
- 已再次通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮命名收口
- 已完成业务状态 owner 目录梳理，并补充了关键 owner 注释，明确了 `ActivityStateManager` / `g_battleSixAuto` / `g_battleSixFlow*` / `g_shuangtaiAuto` / `g_spiritCollectState` / `g_dungeonJumpState` / `g_horseGameThread` / `g_danceState` / `g_deepDigState` / `g_taskZone*` / `g_eightTrigramsProgress` / `g_heavenFurui*` / `g_itemPositionMap` / `g_loginKey` / `g_loginKeyCaptured` / `g_autoHeal` / `g_md5CheckIndex` / `g_collect*` / `g_audioSessionState` 的归属
- 已通过 `cmake --build build_new --config Release --target WebView2Demo`
- 已开始梳理活动模块命名风格，并补充了 `include/activities` / `include/internal` 的边界说明
- 已将 `buy_item` / `use_item` / `enter_boss_battle` 收口为动态结果 helper
- 已将 `buy_item` / `use_item` 的包数据刷新收口为共享 helper

## 进行中

- 继续保持命令路由、参数解析、业务调用三层分离
- 继续把剩余业务状态 owner 注释同步到对应模块
- 继续同步更新文档与清单，避免进度漂移
- 继续观察剩余 `Send*` packet helper 是否需要升格为 `Start*` / `Cancel*`

## 下一步

- 如有必要，再整理剩余的简单 `msg.find(...)` 分支
- 暂不进入 `wpe_hook.cpp` 或活动模块的大范围重构
- 继续观察命令顺序和子串命中的风险

## 当前约束

- 不改 `Opcode` / `Params`
- 不改协议布局
- 不改 generated 文件
- 不做跨模块大拆分
- 保留现有中文 UI 文案
