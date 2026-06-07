# 清除煞气分析（Act641 / Active20141106Shaqi）

## 1. 周活动定位

- 最新 `swf_cache/weeklyactivity` 条目是 `id=25051902`，名称 `清除煞气`。
- 对应入口 SWF 是 `assets\activity\201411\Activity20141106Shaqi\Active20141106Shaqi.swf`。
- SWF 已下载到 `swf_cache/Active20141106Shaqi.swf`。
- 已用 FFDec 导出脚本到 `swf_cache/decompiled/Active20141106Shaqi/scripts/`。

## 2. AS3 结构

- 入口类：`Active20141106Shaqi.as`
  - 注册 `ShaqiModel` 和 `ActiveMainControl`。
  - 加载 `Tool.swf` 后打开 `UILayer` 与入口界面。
- 控制层：`control/EntryControl.as`、`control/GameControl.as`
  - 统一通过 `MsgDoc.OP_CLIENT_ACTIVITY_QINGYANG_NEW.send` 发包。
  - `GameControl.onEndGame()` 使用 `userId % 10000 * 2 + score * checkCode + 7` 生成客户端校验值。
- 模型层：`model/ShaqiModel.as`
  - 解析 `open_ui / start_game / end_game / sweep_info / sweep`。
  - 另外还存在 `reward / clear_cd_time / buy_play_game_cnt / buy_monster / exchange_info / exchange / catch / catch_res / relive` 等分支。
- 玩法层：`view/GameUI.as`、`view/ItemView.as`
  - 60 秒计时的本地小游戏。
  - 只允许点击黑球，点到白球会掉血。
  - `PASS_SCORE = 1000`，`MAX_SCORE = 1800`。

## 3. 协议结论

- 活动 ID：`641`
- 请求 Opcode：`1185429`
- 回包 Opcode：`1316501`
- 请求格式：

```text
Opcode = 1185429 (ACTIVITY_QINGYANG_NEW_SEND)
Params = 641
Body   = [opName:string][int32...]
```

主流程只需要这 5 个操作：

| 操作 | 请求体 | 响应体 |
|---|---|---|
| `open_ui` | 无 | `playCount, restTime, medalCount, passCnt, awardFlag, bestRecord, promptFlag, bonusList[3], catchPetId, catchList[2]` |
| `start_game` | 无 | `result, playCount, checkCode` |
| `end_game` | `clientCheckCode, score` | `result, unknown, unknown, bestRecord, score, unknown, medal, exp, coin` |
| `sweep_info` | 无 | `result, medal, exp, coin` |
| `sweep` | 无 | `result, playCount, restTime, medal, exp, coin` |

响应里的关键分支：

- `start_game`
  - `result = 0` 时才会给出 `checkCode`。
  - `result = 10/11/12` 分别对应正在游戏、冷却中、今日次数用完。
- `end_game`
  - `result = 3` 时是网络错误。
  - 其他结果仍会带回 bestRecord 和奖励值。
- `sweep_info` / `sweep`
  - `result = 0` 视为成功。

## 4. 自动化结论

只接主流程，不扩展购买、兑换、捕捉、复活等旁支。

最小可用闭环：

1. `open_ui`
2. 如果勾选扫荡且 `bestRecord > 0`，先走 `sweep_info -> sweep`
3. 否则走 `start_game -> end_game`
4. 结束后再拉一次 `open_ui` 刷新状态

## 5. C++ 落点

- 新增 `Act641` 协议声明：`include/activities/activity_minigames.h`
- 新增 `Act641State`：`include/internal/activity_states_internal.h`
- 新增 `Act641` 发送、回包和一键线程：`src/hook/wpe_hook.cpp`
- 新增 WebView 命令 `one_key_act641`：`src/core/web_message_handler.cpp`
- 新增“清除煞气”入口卡片：`resources/ui.html`

## 6. 备注

- 这次只接通游戏和扫荡主流程。
- 购买、兑换、捕捉、复活这些不属于主流程的功能，按你的要求没有纳入自动化。
