# 驱赶毒蚊分析（Act717 / BanishMosquito）

## 1. 周活动定位

- `swf_cache/weeklyactivity` 里存在两个同名条目：
  - `id=25092605`，名称 `驱赶毒蚊`，`url=assets/activity/banishMosquito/BanishMosquito.swf`
  - `id=26060101`，名称 `驱赶毒蚊`，`url=assets/activity/banishMosquito/BanishMosquito.swf`
- 当前抓到的最新远端 `weeklyactivity` 也包含这条精确活动名，且 SWF 路径一致。
- 已下载到 `swf_cache/BanishMosquito.swf`，并导出脚本到 `swf_cache/decompiled/BanishMosquito/scripts/`。

## 2. AS3 结构

- 入口类：`ac2025.BanishMosquito.BanishMosquito`
  - 进入活动时注册 `MosquitoModel`，并请求 `open_ui`。
  - 主界面提供 `开始游戏`、`扫荡`、`兑换`、`购买` 等入口。
- 模型层：`ac2025.BanishMosquito.data.MosquitoModel`
  - `open_ui` 读取 `restPlayCount / restCdTime / totalBadgeNum / promptFlag / isSweep / catchList`。
  - `start_game` 返回 `result`，成功后打开本地小游戏。
  - `end_game` 和 `sweep` 都会返回奖励列表。
  - `sweep_info` 用于判断是否可扫荡并预览奖励。
- 游戏层：`ac2025.BanishMosquito.view.BanishMosquitoGame`
  - 是本地小游戏，结束时直接提交 `end_game(rewardCount)`。
  - `rewardCount` 就是本地捕获结果，不存在额外的校验码逻辑。
- 奖励层：`ResultView`、`SweepView`
  - 两者都只负责展示服务端返回的奖励列表。

## 3. 协议结论

- 活动 ID：`717`
- 请求 Opcode：`1185429`
- 回包 Opcode：`1316501`
- 请求格式：

```text
Opcode = 1185429 (ACTIVITY_QINGYANG_NEW_SEND)
Params = 717
Body   = [opName:string][int32...]
```

主流程只需要这 5 个操作：

| 操作 | 请求体 | 响应体 |
|---|---|---|
| `open_ui` | 无 | `playCount, restTime, totalBadgeNum, promptFlag, isSweep, skip, catch1, catch2` |
| `start_game` | `promptFlag` | `result, playCount` |
| `end_game` | `rewardCount` | `result, playCount, restTime, awardLen, awards...` |
| `sweep_info` | 无 | `type, awardLen, awards...` |
| `sweep` | 无 | `result, playCount, restTime, awardLen, awards...` |

## 4. 结论

- 这次只接主流程：
  - `open_ui`
  - `sweep_info -> sweep`
  - `start_game -> end_game`
- 购买、兑换、买次数等与主流程无关的入口不接入自动化。
- 该活动没有 `checkCode` 型校验，适合直接按 `rewardCount` 结算的统一小游戏框架。
