# 重构清单

## 阶段 1：UI / 消息层安全收口

- [x] 统一 helper text 输出入口
- [x] 统一 packet count 输出入口
- [x] 清理 `web_message_handler.cpp` 内部重复的 JS 拼接
- [x] 收口 `send_all_packets` / `enter_boss_battle` 的提示输出
- [x] 移除 `web_message_handler.cpp` 内部直接 `PostScriptToUI` 的旧写法
- [x] 将 `delete_selected_packets` 的清空脚本提成 helper
- [x] 将 `send_packet` 分支提成 helper
- [x] 将 `packet_window_visible` 分支提成 helper
- [x] 将 `delete_selected_packets` 分支提成 helper
- [x] 将 `clear_packets` / `start_intercept` / `stop_intercept` / `set_intercept_type` 提成 helper
- [x] 将 `set_speed` 接入 helper
- [x] 将 `toggle_auto_heal` 接入 helper
- [x] 将 `set_block_battle` 接入 helper
- [x] 将 `set_auto_go_home` 接入 helper
- [x] 将 `query_lingyu` 接入 helper
- [x] 将 `query_monsters` 接入 helper
- [x] 将 `refresh_pack_items` 接入 helper
- [x] 将 `buy_dice` 接入 helper
- [x] 将 `buy_dice_18` 接入 helper
- [x] 保留现有命令行为不变
- [x] 更新进度文档
- [x] 更新规范文档
- [x] 清理 `send_packet` 分支里的临时 `#if 0` 旧实现
- [x] 修复 `packet_window_visible` 的矩形参数传递
- [x] 将 `one_key_collect` 接入 helper
- [x] 将 `one_key_xuantta` 接入 helper
- [x] 将 `battlesix_auto_match` 接入 helper
- [x] 将 `battlesix_set_auto_battle` 接入 helper
- [x] 将 `dungeon_jump_start` / `dungeon_jump_stop` 接入 helper
- [x] 将 `one_key_horse_competition` 接入 helper
- [x] 将 `enter_boss_battle` 接入 helper
- [x] 将 `set_hijack_enabled` / `clear_hijack_rules` / `save_packet_list` / `load_packet_list` / `stop_send` 接入 helper
- [x] 将 `query_shuangtai` / `start_shuangtai` / `stop_shuangtai` / `stop_horse_competition` / `start_heaven_furui` / `stop_heaven_furui` 接入 helper
- [x] 将 `buy_item` / `use_item` 接入 helper
- [x] 将 `send_all_packets` 接入 helper
- [x] 将 `one_key_act793` / `one_key_act791` / `one_key_act782` / `one_key_act803` / `one_key_act624` / `one_key_sea_battle` 接入 helper
- [x] 将 `decompose_lingyu` 接入 helper，并拆分 `batch` / `array` 子处理
- [x] 将 `decompose_lingyu` 的 `indices` 提取逻辑收口为共享 helper
- [x] 将 `spiritCollect` 接入 helper
- [x] 将 `send_all_packets` 的进度输出收口为命名 helper
- [x] 将 `window-drag` / `window-minimize` / `window-close` / `update-dialog-show` / `update-dialog-hide` 接入 helper
- [x] 将 `key-login-dialog-show` / `key-login-dialog-hide` / `spirit-confirm-dialog-show` / `spirit-confirm-dialog-hide` 接入 helper
- [x] 将 `open-url` / `refresh-game` / `refresh-no-login` / `mute-game` / `clear-ie-cache` / `copy-login-key` / `key-login` 接入 helper
- [x] 将 `add_hijack_rule` 接入 helper
- [x] 将 `spiritCollect` 内部动作拆分为命名 helper
- [x] 将 `spiritCollect` 的结果提示收口为共享 helper
- [x] 将 `one_key_horse_competition` 的进度回调收口为命名 helper
- [x] 将 `one_key_horse_competition` 的启动 worker 收口为命名 helper
- [x] 将 `send_packet` / `battlesix_auto_match` / `dungeon_jump_start` 的线程入口收口为命名 helper
- [x] 将 `one_key_act793` / `one_key_act791` / `one_key_act782` / `one_key_act803` / `spiritCollect open_ui` / `spiritCollect getSpirits` 改为直接函数指针调用
- [x] 将 `one_key_act624` / `one_key_sea_battle` 改为共享调用重载调用
- [x] 将 `send_packet` 改为 helper 自己解析 `msg`
- [x] 将 `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 改为共享布尔写入 helper
- [x] 将 `send_packet` / `battlesix_auto_match` / `dungeon_jump_start` 的线程启动与失败清理收口为共享 helper
- [x] 将 `one_key_horse_competition` / `send_all_packets` 的 detached 线程启动收口为共享 helper
- [x] 将一批 bool 结果命令收口为共享 `HandleBoolResultCommand`
- [x] 将 `stop_task_zone` / `stop_shuangtai` / `stop_heaven_furui` / `stop_send` 收口为 `HandleActionWithTextCommand`
- [x] 将 `update-dialog` / `key-login-dialog` / `spirit-confirm-dialog` 的 show/hide 分支收口为 `HandleSetBrowserWindowVisibleCommand`
- [x] 将一批纯动作命令收口为 `HandleActionCommand`
- [x] 将 `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 的布尔写入 helper 命名对齐为 `HandleSetBoolFlagCommand`
- [x] 将 `toggle_auto_heal` / `set_block_battle` / `set_auto_go_home` 的路由 wrapper 命名对齐为 `HandleSetAutoHealEnabledCommand` / `HandleSetBlockBattleEnabledCommand` / `HandleSetAutoGoHomeEnabledCommand`
- [x] 将 `daily_tasks` / `task_zone` 的路由 wrapper 命名对齐为 `HandleStartDailyTasksCommand` / `HandleStartTaskZoneCommand`
- [x] 将 `daily_tasks` / `stop_horse_competition` 收口为 `HandleActionCommand`
- [x] 将 `set_speed` / `set_intercept_type` / `set_hijack_enabled` 收口为 `HandleActionCommand`
- [x] 将 `open-url` / `refresh-game` / `refresh-no-login` / `mute-game` / `clear-ie-cache` / `copy-login-key` / `key-login` / 浏览器可见性 wrapper 收口为命名 helper，内部继续复用 `HandleActionCommand`
- [x] 将 `buy_item` / `use_item` / `enter_boss_battle` 收口为动态结果 helper
- [x] 将 `buy_item` / `use_item` 的包数据刷新收口为共享 helper

## 阶段 2：函数命名对齐

- [x] 将 `query_lingyu` / `query_monsters` / `refresh_pack_items` / `buy_dice` / `clear_packets` / `query_shuangtai` / `battlesix_auto_match` / `battlesix_cancel_match` / `battlesix_set_auto_battle` / `one_key_collect` / `one_key_xuantta` / `one_key_horse_competition` 的内部 wrapper 命名对齐
- [x] 将 `one_key_act*` / `one_key_sea_battle` 的内部启动 helper 命名对齐为 `HandleStartOneKeyActCommand` / `HandleStartOneKeySeaBattleCommand`
- [x] 将 `one_key_act*` / `one_key_sea_battle` 的一键入口命名对齐为 `StartOneKeyAct*` / `StartOneKeySeaBattlePacket`
- [x] 将 `one_key_act793` / `one_key_act791` / `one_key_act782` / `one_key_act803` / `one_key_act624` 的路由处理函数命名对齐为 `HandleStartOneKeyAct793Command` / `HandleStartOneKeyAct791Command` / `HandleStartOneKeyAct782Command` / `HandleStartOneKeyAct803Command` / `HandleStartOneKeyAct624Command`
- [x] 将 `one_key_xuantta` 的路由处理函数命名对齐为 `HandleStartOneKeyXuanttaCommand`
- [x] 将 `send_packet` / `send_all_packets` 的路由处理函数命名对齐为 `HandleSendRawPacketCommand` / `HandleStartSendAllPacketsCommand`
- [x] 将 `query_shuangtai` 的路由处理函数命名对齐为 `HandleQueryShuangTaiMonstersCommand`
- [x] 将 `query_lingyu` / `query_monsters` / `refresh_pack_items` / `buy_dice` / `query_shuangtai` / `battlesix_auto_match` / `battlesix_set_auto_battle` / `dungeon_jump_start` / `dungeon_jump_stop` 的路由处理函数继续对齐为 `HandleQuery*` / `HandleRefresh*` / `HandleBuy*` / `HandleStart*` / `HandleStop*` / `HandleSet*`
- [x] 将 `battlesix_auto_match` / `dungeon_jump_start` / `one_key_horse_competition` 的内部 worker 命名继续对齐为 `HandleBattleSixAutoMatchWorker` / `HandleDungeonJumpWorker` / `HandleOneKeyHorseCompetitionWorker`
- [x] 通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮 one-key 活动命名收口
- [x] 通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮消息层路由命名收口
- [x] 通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮 query / start / set 路由别名继续收口
- [x] 通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮内部 worker 命名收口
- [x] 通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮 `send_packet` / `send_all_packets` / `query_shuangtai` 以及 worker 收口
- [x] 记录外部消息名的兼容封装点，保留原有 `toggle_*` / `query_*` / `refresh_*` 以及浏览器相关路由名
- [ ] 统一 `Send*` / `Process*Response` / `Start*` / `Stop*` / `Cancel*` 的职责边界
- [ ] 统一活动模块的命名风格
- [x] 标记每个业务状态的唯一 owner
- [x] 整理剩余浏览器 wrapper 命名（`HandleRefresh*` / `HandleOpen*` / `HandleMute*`）
- [x] 将 `SendDailyTasksAsync` / `SendEightTrigramsTaskAsync` / `SendOneKeyCollectPacket` 对齐为 `Start*` 启动入口
- [x] 将剩余一批真正承担“启动整套流程”的入口对齐为 `StartOneKeyTowerPacket` / `StartOneKeyBattleSixPacket` / `StartOneKeyDungeonJumpPacket` / `StartOneKeyShuangTaiPacket` / `StartOneKeyHorseCompetitionPacket` / `StartOneKeyHeavenFuruiPacket`，并将战斗六取消入口对齐为 `CancelBattleSixMatch`
- [x] 补充坐骑大赛 / 八卦灵盘任务区 / 福瑞宝箱的 owner 注释
- [x] 补充基础战斗 / MD5 / 一键采集 / 背包缓存 / 登录 key 捕获 / 万妖盛会流程 / 跳舞大赛 / 深度挖宝的 owner 注释
- [x] 通过 `cmake --build build_new --config Release --target WebView2Demo` 验证本轮改动

## 阶段 3：模块归档

- [ ] 为清晰的活动模块建立稳定边界
- [ ] 逐步减少 `wpe_hook.cpp` 中的历史堆叠内容
- [ ] 为每个迁移步骤保留验证记录

## 禁止项

- [ ] 不改协议常量
- [ ] 不改封包布局
- [ ] 不做大规模重构
- [ ] 不修改 generated 文件
