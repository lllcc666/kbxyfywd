# 土幻的曙光分析（Act684 / Activity20141023EarthMagicDawn）

## 1. 周活动定位

- 最新 `swf_cache/weeklyactivity` 条目是 `id=26060102`，名称 `土幻的曙光`。
- 对应入口 SWF 是 `assets/activity/201410/Activity20141023EarthMagicDawn/Activity20141023EarthMagicDawn.swf`。
- SWF 已下载到 `swf_cache/Activity20141023EarthMagicDawn.swf`，并导出脚本到 `swf_cache/decompiled/Activity20141023EarthMagicDawn/scripts/`。

## 2. AS3 结构

- 入口类：`Activity20141023EarthMagicDawn.as`
  - 注册 `ActiveControl`、`ActiveModel`，入口界面为 `ActivityEntrance`。
- 控制层：`control/ActiveControl.as`
  - 所有业务都走 `MsgDoc.OP_CLIENT_ACTIVITY_QINGYANG_NEW.send`。
  - `open_ui`、`start_game`、`end_game`、`sweep_info`、`sweep` 是主流程。
- 模型层：`model/ActiveModel.as`
  - `open_ui` 解析 `playCount / restTime / bubbleNum / rewardCount / todayCount / ruleFlag / drawFlag / maxScore / catch_list`。
  - `start_game` 返回 `result / playCount / checkCode`。
  - `end_game` 与 `sweep` 都返回 `result_type / playCount / restTime / todayCount / medal / coin / exp / score / maxScore`。
  - `sweep_info` 返回 `result / medal / coin / exp`。
- 玩法层：`view/MainView.as`、`view/GameView.as`、`view/GameOverView.as`、`view/ActEarthMagicSweepWin.as`
  - 主按钮只控制“开始游戏”和“扫荡”。
  - 兑换、购买、抽取、复活等分支虽然存在，但不属于主流程。

## 3. 协议结论

- 活动 ID：`684`
- 请求 Opcode：`1185429`
- 回包 Opcode：`1316501`
- 请求格式：

```text
Opcode = 1185429 (ACTIVITY_QINGYANG_NEW_SEND)
Params = 684
Body   = [opName:string][int32...]
```

主流程只需要这 4 个操作：

| 操作 | 请求体 | 响应体 |
|---|---|---|
| `open_ui` | 无 | `playCount, restTime, bubbleNum, rewardCount, todayCount, ruleFlag, drawFlag, maxScore, skip, catch1, catch2` |
| `start_game` | `ruleFlag` | `result, playCount, checkCode` |
| `end_game` | `clientCheckCode, score` | `result_type, playCount, restTime, todayCount, medal, coin, exp, score, maxScore` |
| `sweep_info` | 无 | `result, medal, coin, exp` |
| `sweep` | 无 | `result_type, playCount, restTime, todayCount, medal, coin, exp, score, maxScore` |

## 4. 关键校验

- `clientCheckCode = checkCode + (userId % 100) * checkCode + score`
- `start_game` 返回 `checkCode` 后，`end_game` 才能直接结算。
- `sweep` 需要先满足 `maxScore > 0`，再走 `sweep_info -> sweep`。

## 5. 代码落点

- 新增 `Act684` 协议声明：`include/activities/activity_minigames.h`
- 新增 `Act684State`：`include/internal/activity_states_internal.h`
- 新增 `Act684` 发送、回包和一键线程：`src/hook/wpe_hook.cpp`
- 新增 WebView 命令 `one_key_act684`：`src/core/web_message_handler.cpp`
- 新增“土幻的曙光”入口卡片：`resources/ui.html`

## 6. 实现结论

- 这次只接主流程：
  - `open_ui`
  - `sweep_info -> sweep`
  - `start_game -> end_game`
- 购买、兑换、抽取、复活等与主流程无关的入口未接入自动化。
