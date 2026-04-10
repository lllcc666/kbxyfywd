# 天之骄子的特训分析（Act666 / Active20140515KongKong）

## 1. 下载结果

- 活动入口来自 `swf_cache/weeklyactivity`，对应条目是 `id=26040702`，名称是 `天之骄子的特训`。
- 目标 SWF 已下载到 `swf_cache/Active20140515KongKong.swf`。
- 已用 FFDec 导出脚本到 `swf_cache/decompiled/Active20140515KongKong/scripts/`。

## 2. AS3 结构

- `Active20140515KongKong.as`
  - 入口类，只负责挂 `ActEnterUI`。
  - 退出时销毁 `EventCenter` 和 `DataCenter`。
- `view/ActEnterUI.as`
  - 加载 `enter_ui.swf`。
  - 注册 `KongKongControl` 和 `KongKongModel`。
  - 首次拉 `open_ui`，根据返回值控制开始、扫荡、购买次数、兑换、冷却加速等按钮。
- `model/KongKongModel.as`
  - 统一解析服务端回包。
  - 处理 `open_ui / start_game / end_game / exchange_info / exchange / buy_play_game_cnt / buy_monster / reward / clear_cd_time / buy_gourd / sweep_info / sweep / catch / catch_res`。
- `view/GameMain.as`
  - 本地横版射击小游戏。
  - 距离推进到 3000 后触发 boss 预演和 boss 战。
  - 结束时按 `medalCount + checkCode + isPass` 结算并回包。
- `view/Act666ExchWin.as` / `view/KongKongSweepWin.as`
  - 兑换面板和扫荡面板。
- `view/GameDeadAlert.as`
  - 死亡后可复活或直接结束。

## 3. 协议结论

这个活动走的是统一活动协议：

```text
Opcode = 1185429 (ACTIVITY_QINGYANG_NEW_SEND)
Params = 666
Body   = [opName:string][int32...]
```

关键操作和回包格式如下：

| 操作 | 请求体 | 响应体 |
|---|---|---|
| `open_ui` | 无 | `playCount, restTime, medalCount, passCount, getAwardFlag, bestFlag, rewardList[4], catchId, catchList[2]` |
| `start_game` | `isPropt, type`，当前 UI 里实际传 `0, 0` | `result, playCount, checkCode` |
| `choose_type` | `type` | `result, type`，当前源码里基本没走到 |
| `end_game` | `medalCount, clientCheckCode, isPass` | `type, playCount, restTime, score, bestFlag, passCount, medalCount, exp, coin` |
| `exchange_info` | 无 | `len + [current, max] * len` |
| `exchange` | `index, num` | `result, index, currentCount, medalCount` |
| `buy_play_game_cnt` | 无 | `result, playCount` |
| `buy_monster` | 无 | `result` |
| `reward` | `index` | `result` |
| `clear_cd_time` | `restTime` | `result, restTime, kbCoin` |
| `buy_gourd` | `type, buyCnt` | `result` |
| `catch` | `monsterId` | `result, medalCount` |
| `catch_res` | 无 | 只触发 UI 刷新 |
| `sweep_info` | 无 | `medalCount, exp, coin` |
| `sweep` | 无 | `type, playCount, restTime, medalCount, exp, coin` |

补充：

- `clientCheckCode = serverCheckCode & (userId % serverCheckCode)`。
- `game_event`、`choose_type` 在 AS3 里定义了，但主流程里几乎没用到，可以先当保留接口。

## 4. 本地玩法结论

- 这是一个本地渲染的射击小游戏，不是每一发子弹都回服务器。
- 服务器只关心：
  - 开局 `start_game`
  - 结束 `end_game`
  - 兑换 / 扫荡 / 买次数 / 买宠 / 冷却 / 捕捉这些辅助操作
- `GameMain` 里真正决定结算的是本地累计的 `medalCount`，然后和服务端下发的 `checkCode` 一起提交。

## 5. C++ 方案

### 5.1 状态层

建议新增一个 `Act666State`，或者直接复用现有活动状态管理器里的一组字段，至少要保存：

- `playCount`
- `restTime`
- `medalCount`
- `passCount`
- `getAwardFlag`
- `bestFlag`
- `rewardList[4]`
- `catchId`
- `catchList[2]`
- `limitList`
- `checkCode`
- `awardList`
- `isPass`

### 5.2 封包层

复用现有 `BuildActivityPacket(Opcode::ACTIVITY_QINGYANG_NEW_SEND, 666, op, values)`：

- `SendAct666OpenUiPacket()`
- `SendAct666StartGamePacket()`
- `SendAct666EndGamePacket(medalCount, clientCheckCode, isPass)`
- `SendAct666ExchangeInfoPacket()`
- `SendAct666ExchangePacket(index, num)`
- `SendAct666BuyCountPacket()`
- `SendAct666ClearCdPacket(restTime)`
- `SendAct666SweepInfoPacket()`
- `SendAct666SweepPacket()`
- `SendAct666CatchPacket(monsterId)`

### 5.3 回包解析

在 `src/hook/wpe_hook.cpp` 增加 `ProcessAct666Response()`：

- 先读字符串 op
- 再按上面的表解析 int32
- 成功后同步到状态层
- 同步刷新 `UIBridge` 提示文本

### 5.4 自动化流程

最小可用方案：

1. 拉 `open_ui`
2. 校验次数和冷却
3. 如果走扫荡且 `bestFlag > 0`，先拉 `sweep_info` 再发 `sweep`
4. 否则拉 `start_game`
5. 拿到 `checkCode` 后提交 `end_game`

如果要做完整自动化，再把本地小游戏也做成线程式仿真，但这不是协议最低闭环的必要条件。

### 5.5 集成点

- `src/hook/wpe_hook.cpp`
  - 增加 Act666 状态、发送函数、回包处理、线程入口。
- `include/internal/activity_states_internal.h`
  - 如果要做独立状态，放一个新的 `Act666State`。
- `include/protocol/packet_protocol.h`
  - 不需要新 opcode，现有 `ACTIVITY_QINGYANG_NEW_SEND` 已够用。
- `src/core/web_message_handler.cpp`
  - 如果前端要加按钮，再补一个 UI 命令入口。

## 6. 结论

- 这个活动就是 `ID=666` 的统一活动协议，不需要另起一套 opcode。
- C++ 侧优先做“状态 + 协议 + 结算”闭环，先别把本地射击玩法全量重写。
- 如果你要我继续，我可以直接按这份方案补 `wpe_hook.cpp` 的 Act666 分支。
