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
- [x] 将 `daily_tasks` / `task_zone` 的路由 wrapper 命名对齐为 `HandleSendDailyTasksCommand` / `HandleStartTaskZoneCommand`
- [x] 将 `daily_tasks` / `stop_horse_competition` 收口为 `HandleActionCommand`
- [x] 将 `set_speed` / `set_intercept_type` / `set_hijack_enabled` 收口为 `HandleActionCommand`
- [x] 将 `open-url` / `key-login` / 浏览器可见性 wrapper 收口为 `HandleActionCommand`
- [x] 将 `buy_item` / `use_item` / `enter_boss_battle` 收口为动态结果 helper
- [x] 将 `buy_item` / `use_item` 的包数据刷新收口为共享 helper

## 阶段 2：函数命名对齐

- [ ] 统一 `Send*` / `Process*Response` / `Start*` / `Stop*` 的职责边界
- [ ] 统一活动模块的命名风格
- [ ] 标记每个业务状态的唯一 owner
- [ ] 记录兼容封装点

## 阶段 3：模块归档

- [ ] 为清晰的活动模块建立稳定边界
- [ ] 逐步减少 `wpe_hook.cpp` 中的历史堆叠内容
- [ ] 为每个迁移步骤保留验证记录

## 禁止项

- [ ] 不改协议常量
- [ ] 不改封包布局
- [ ] 不做大规模重构
- [ ] 不修改 generated 文件
