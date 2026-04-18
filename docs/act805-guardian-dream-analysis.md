# 守护梦境分析（Act805 / Activity20140213Lele）

## 1. 周活动定位

- 最新 `swf_cache/weeklyactivity` 条目是 `id=26041002`，名称 `守护梦境`。
- 对应入口 SWF 是 `assets/activity/201402/20140213Lele/Activity20140213Lele.swf`。
- 已下载到 `swf_cache/Activity20140213Lele.swf`，并导出脚本到 `swf_cache/decompiled/Activity20140213Lele/scripts/`。

## 2. AS3 结构

- 入口类：`Activity20140213Lele.as`
  - 注册 `KBLeleControl`、`KBLeleModel`，主界面是 `KBEnterView`。
- 控制层：`control/KBLeleControl.as`
  - 统一通过 `MsgDoc.OP_CLIENT_ACTIVITY_QINGYANG_NEW.send` 发包。
  - 关键操作是 `game_info / start_game / end_game / sweep_info / sweep`。
- 模型层：`model/KBLeleModel.as`
  - 解析所有 805 回包。
  - `decodeOpenUI()` 读取次数、冷却、勋章、历史最高分、上次分数。
  - `decodeStartGame()` 读取 `checkcode`。
  - `decodeEndGame()`、`decodeSweepInfo()`、`decodeSweep()` 读取奖励。
- 数据层：`KBDataCenter.as`、`model/KBLeleData.as`
  - `max_score = 350`，活动兑换宠物是 `1354 大眼雀`。
- 战斗层：`view/KBBatttleView.as`
  - 本地小游戏，结束时提交 `sendEndGame(awardNum, 1)`。

## 3. 协议结论

- 活动 ID：`805`
- 请求 Opcode：`1185429`
- 回包 Opcode：`1316501`
- 请求格式：

```text
Opcode = 1185429 (ACTIVITY_QINGYANG_NEW_SEND)
Params = 805
Body   = [opName:string][int32...]
```

- 关键操作：
  - `game_info`
    - 回包：`result, playCount, restCdTime, isPop, medalNum, historyBestScore, lastScore`
  - `start_game`
    - 回包：`result, playCount, checkCode`
  - `end_game`
    - 请求：`clientCheckCode, score`
    - 回包：`result, playCount, restCdTime, medalNum, exp, coin`
  - `sweep_info`
    - 回包：`result, endType, medal, exp, coin`
  - `sweep`
    - 回包：`result, playCount, restTime, medal, exp, coin`

## 4. 关键校验

- `clientCheckCode = userId % 1000 + checkCode + score`
- AS3 在发送 `end_game` 后，还会额外发送：

```text
Opcode = 1184791 (TASK_DAILY_SEND)
Params = 3
Body   = [3, 4039001, 0]
```

- 这一步用于同步小游戏相关的日常任务进度。

## 5. 代码落点

- 新增 `Act805` 协议声明：`include/activities/activity_minigames.h`
- 新增 `Act805State` 和状态管理入口：`include/internal/activity_states_internal.h`
- 新增 `Act805` 一键流程、回包解析和响应注册：`src/hook/wpe_hook.cpp`
- 前端“最新活动”入口切换到 `守护梦境`：`resources/ui.html`
- WebView 消息处理新增 `one_key_act805`：`src/core/web_message_handler.cpp`

## 6. 实现结论

- `守护梦境` 可以直接复用当前统一活动小游戏框架，不需要新 opcode。
- 最小闭环是：
  - `game_info`
  - 可扫荡时走 `sweep_info -> sweep`
  - 否则 `start_game -> end_game(350分)`
- 当前代码已经按这个闭环接入。
