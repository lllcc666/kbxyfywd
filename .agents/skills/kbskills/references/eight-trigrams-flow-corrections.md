# 八卦灵盘自动流程校正

本文定位：**AS3 AutoBaGua 基准与 native 偏差校正**。

证据等级约定：

- `AS3 已核实`：反编译控制流明确出现。
- `XML 解释`：只表示节点存在及其语义。
- `native 兼容实现`：只表示当前 C++ 的模拟行为，不能反推为 AS3 原始发包。
- `待确认`：仍缺少控制流或运行时封包证据。

## 分析依据（本地反编译文件）

- 任务 XML：`swf_cache/taskprop_4006000.xml`
- 内置自动模块：`swf_cache/decompiled/AutoBaGuaEntrance/scripts/AutoBaGua/view/AutoBaGuaView.as`（入口侧，24 个状态与点击）
- 主游戏侧：**注意是 `MainGame_remote` 变体；平铺 `MainGame/` 与 `MainGame_2026020502/` 只有 `EightTrigrams.as`，没有 AutoBaGua 集成**。
  - `swf_cache/decompiled/MainGame_remote/scripts/com/game/modules/control/task/TaskControl.as`（`onSendAutoBaGua`、`opDialogAfterBattle`、`opDialogBackType11`）
  - `.../modules/view/task/TaskView.as`（`onShowWoodsInMotiao`、`sendPopBaGua`、`getAwardWithoutComplete`）
  - `.../modules/view/task/EightTrigrams.as`（动画收口后派发推进事件）
  - `.../modules/control/WindowLayerControl.as`（八卦任务入口和自动发包联动）
  - `.../modules/control/battle/BattleControl.as`（战斗清理及 `OP_GATEWAY_BATTLE_END`）
  - `.../util/PropertyPool.as`（按 `dialogId/1000` 归组加载 taskprop）
  - `.../modules/control/task/util/TaskXMLParser.as`（XML 节点解释）

## 协议对照

本文档中的 `TASK_TALK`、`TASK_TALK_SEND`、`TASK_CLICK_NPC`、`TRAIN_INFO_SEND` 是 C++ 侧改写名，AS3 中不存在这些字面量。真实常量为：

- `MsgDoc.OP_CLIENT_REQ_TASKTALK`：send=`1184788` / back=`1315860`
- `MsgDoc.OP_CLIENT_CLICK_NPC`：send=`1186048` / back=`0`
- `MsgDoc.OP_CLIENT_REQ_TRAIN_INFO`：send=`1184772` / back=`1316142`
- `MsgDoc.OP_GATEWAY_BATTLE_END`：send=`1186323` / back=`1317125`

## 核心结论

XML 节点定义和自动执行步骤不是一一对应关系。`desc` 是任务对话，`alert` 是提示；自动模块还会使用动态/特殊 NPC 和服务器回包推进流程，不能按 XML 节点顺序把所有 dialog ID 直接发成 `TASK_TALK`。

执行基准是 `AutoBaGuaView.STEP_CONFIG` 的 24 个 `curIndex` 点击状态和 `onNext()` 分支。XML 只用于解释对白、提示、奖励和目标场景。

必须把 AS3 的两条契约分开记录：

- `GameData.instance.playerData.isAutoBaGua == true` 时，`WindowLayerControl.hasBaGuaTask()` 和 `TaskControl.onSendAutoBaGua()` 直接发送 `TASK_TALK`，不经过普通选择窗口。
- 普通任务窗口没有进入这个状态时，选择按钮由 `TaskControl.taskDialogComplete()` 处理，`chooseId != 0` 时先发 `TRAIN_INFO_SEND(params=7,[dialogId,chooseId])`，再发 `TASK_TALK`。
- 当前 kbwebui native 辅助没有接管游戏内 `isAutoBaGua`，也没有驱动内置 AutoBaGua SWF。因此 native 如果采用后一条规则，必须标为**普通任务窗口兼容路径**，不能写成内置 AutoBaGua 的通用协议。

## AS3 结果表

下表的点击顺序来自 `AutoBaGuaView.STEP_CONFIG`（24 条，条目键为 `curIndex` 字符串，每项含 `sceneId/npcId/x/y/isAutoMove`），结果事件来自同一个类的 `onNext()`（`AutoBaGuaView.as:368` 起，按 `(npcId, dialogId)` 元组分支）。`dialogId` 是服务器/任务系统回调的一部分，不等于 XML 中每个 `dialog` 都会成为自动请求。下表的“自动路径动作”只描述 AS3 明确动作；native 兼容包另行记录。

| `curIndex` | 点击场景/NPC | AS3 结果事件 | AS3 已明确动作 |
|---:|---|---|---|
| 1 | `2001/21210` | `(21210,400600101)` 正常推进；`(21210,400600104)` 等级不足失败分支 | `(21210,400600101)` 任务对话推进；`(21210,400600104)` 提示“妖怪等级未达 40 级”并 `onClose()` 终止，不是正常步骤 |
| 2 | `2005/21511` | `(21511,0)` | type 1 有奖励时走奖励提示并推进；无奖励时按主游戏普通分支处理 |
| 3 | `3001/31101` | `(31101,1111111)` | type 11 八卦动画收口，不补 `TASK_TALK` |
| 4 | `3001/31101` | `(31101,400600201)` | 任务对话推进 |
| 5 | `3002/31231` | `(0,400600204)` -> `(31231,31231)` | `onNext()` 明确直接发送 `TASK_TALK(31231,[400600204])`；没有直接证明此前存在 `TASK_TALK(0,[400600204])`；收到 `(31231,31231)` 后主游戏再处理后续发送 |
| 6 | `4001/41101` | `(41101,1111111)` | type 11 八卦动画收口，不补 `TASK_TALK` |
| 7 | `4001/41101` | `(41101,400600301)` | 任务对话推进 |
| 8 | `4003/41312` | `(0,0)` | 等待奖励/任务流程产生的 `(0,0)` 事件，不新增 XML 任务包 |
| 9 | `5002/51206` | `(51206,1111111)` | type 11 八卦动画收口，不补 `TASK_TALK` |
| 10 | `5002/51206` | `(51206,400600401)` | 任务对话推进 |
| 11 | `5003/51305` | `(0,0)` | 等待奖励/任务流程产生的 `(0,0)` 事件，不新增 XML 任务包 |
| 12 | `6001/61101` | `(61101,1111111)` | type 11 八卦动画收口，不补 `TASK_TALK` |
| 13 | `6001/61101` | `(61101,400600501)` | 任务对话推进 |
| 14 | `6002/61242` | 水魂战斗 -> `(0,0)` | 特殊目标 `61242` 触发战斗；战斗清理使用 `OP_GATEWAY_BATTLE_END(1186323)`；收到 `(0,0)` 后进入 `7002` |
| 15 | `7002/71201` | `(71201,1111111)` | type 11 八卦动画收口，不补 `TASK_TALK` |
| 16 | `7002/71201` | `(71201,400600601)` | 任务对话推进 |
| 17 | `8003/81303` | `(0,0)` | 等待奖励/任务流程产生的 `(0,0)` 事件，不新增 XML 任务包 |
| 18 | `9001/91101` | `(91101,1111111)` | type 11 八卦动画收口，不补 `TASK_TALK` |
| 19 | `9001/91101` | `(91101,400600701)` | 任务对话推进 |
| 20 | `9004/91435` | `(91435,400600704)` | 特殊转盘/过场迁移，必须保留该元组 |
| 21 | `10001/101101` | `(101101,1111111)` | type 11 八卦动画收口，不补 `TASK_TALK` |
| 22 | `10001/101101` | `(101101,400600801)` | 任务对话推进 |
| 23 | `10002/101201` | 墨魂战斗 -> `(101301,400600805)` | AutoBaGua 路径下战后客户端发送 `TASK_TALK(101301,[400600805])`（`TaskControl.onSendAutoBaGua()` 直发），`onNext(101301,400600805)` 进入状态 24；`400600806` 后续行为未在 `onNext()` 中确认 |
| 24 | `2001/21210` | `(21210,1111111)` | type 11 八卦动画收口，关闭自动模块并回到任务场景 |

补充两条 AS3 已核实但表格易被误读的事实：

- `(21210,400600104)` 是**失败终止分支**（`AutoBaGuaView.onNext()` 中与 `(21210,400600101)` 并列），只在妖怪等级不足 40 级时由服务器返回，触发提示并关闭自动模块。它不是一个正常执行步骤，不能放进顺序链中间。
- `400600805` 是**客户端发送参数**：`TaskControl.onSendAutoBaGua()` 在战后回话为 `(101301,400600805)` 时直发 `TASK_TALK(101301,[400600805])`，随后 `onNext(101301,400600805)` 推进到状态 24。它不是“仅回包记录”。

## 回包分支规则

普通结果的 `TASK_TALK` 是否发送，取决于主游戏回包分支，不能只按 `(npcId, dialogId)` 推断。两种已核实的不补 `TASK_TALK` 路径如下：

- `TaskControl.opDialogAfterBattle()`：`params.itemCount > 0 && params.type == 1` 时，若 `isAutoBaGua` 走 `TaskView.onShowWoodsInMotiao()`，由 `sendPopBaGua(npcId, dialogId)` 派发自动推进事件并提前返回。
- type 15：`TaskControl.onGetDialogBack()` 的 `case 15` 走 `TaskView.getAwardWithoutComplete()`；在 `isAutoBaGua` 时显示奖励并 `sendPopBaGua(0,0)`，不补发 `TASK_TALK`。

`TRAIN_INFO_SEND` 只属于普通任务对话选择，不属于内置 AutoBaGua 的直发路径。`(0,0)` 是自动模块/奖励流程产生的迁移事件，不能机械地发成 `TASK_TALK(0,[0])`。

## 通天河校正

`taskprop_4006000.xml` 中：

- `400600502`、`400600503`、`400600504` 的 `desc id` 都是垂钓老人 `61202`，不能当作水魂自动步骤。
- `400600506`、`400600507`、`400600508` 是 `alert` 提示节点，不是水魂战斗后的独立对话请求。
- `401311` 是静态数据中的水魂精灵 ID；内置自动模块在通天河实际使用的目标是 `61242`。
- `400600507` 不能展开为 `RewardOnly` 或“两次同包 TASK_TALK”；`400600505` 也不能仅凭 XML 插入两个 `71201` 状态之间。
- `400600704` 的 AS3 迁移条件是 `npcId=91435, dialogId=400600704`，不能替换成无 NPC 的 `1111111` 普通收口。

内置 `AutoBaGuaView` 的关键迁移是：

1. 收到 `(61101,400600501)` 后进入场景 `6002`。
2. 场景 `6002` 的步骤配置使用目标 `61242`。
3. 战斗结束后收到 `(0,0)`，进入场景 `7002`。
4. 进入 `7002` 后点击 `71201`，收到 `(71201,1111111)` 后进入下一个状态；下一次点击由 `(71201,400600601)` 进入 `8003`。

AS3 内置 AutoBaGua 状态基线在战斗结束后由 `(0,0)` 进入 `7002`；另有运行时抓包确认 native 战后会主动点击并确认水魂对话，抓包参数见下节。两条路径的证据范围不同，不应互相覆盖。native 不等待 type 15 奖励回包作为流程条件。AS3 `BattleControl.clearBattleView()` 会发送 `OP_GATEWAY_BATTLE_END.send=1186323`。

## 水魂战后运行时抓包（native 流程）

战斗胜利后，游戏会自动弹出水魂对话；native 自动化按实测包序列主动交互。此抓包是运行时证据，优先于 XML 对话节点推测，但不改变 AS3 AutoBaGua 的独立状态迁移基线。

1. 点击水魂：`44 53 04 00 00 19 12 00 3A EF 00 00 00 00 00 00`
   - `44 53`：Magic `0x5344`；`04 00`：Body 长度 4。
   - `00 19 12 00`：Opcode `1186048` (`CLICK_NPC`)。
   - `3A EF 00 00`：Params `61242`；`00 00 00 00`：Body `[0]`。
2. 确认水魂对话：`44 53 04 00 14 14 12 00 3A EF 00 00 BD AD E0 17`
   - `14 14 12 00`：Opcode `1184788` (`TASK_TALK_SEND`)。
   - `3A EF 00 00`：Params/NPC `61242`；`BD AD E0 17`：Body/DialogId `400600509`。
3. 固定顺序为 `CLICK_NPC(61242,[0])` -> `TASK_TALK(61242,[400600509])`。确认包 NPC 仍是水魂 `61242`，不能发给墨艮 `71201`。
4. 发送确认后短等 `TASK_TALK_BACK(type=15)` 奖励回包（3s 窗口）以确认内丹；回包缺失不阻塞流程，延时后继续 7002。该等待只用于观测与 UI 提示，不作为推进条件。
5. `400600509` 由上述抓包验证，不要求它出现在 `taskprop_4006000.xml`；`400600505` 是 XML 中墨艮 `71201` 的交接对话，不能替代水魂确认包。Native 普通任务兼容链在进入 7002 后另发 `TASK_TALK(71201,[400600505])`，之后再开始 `400600601`；这不是 AS3 AutoBaGua 状态分支。

### 水魂卡点根因（本次修复）

运行时抓包（发送侧，共 133 包）确认：`CLICK_NPC(61242,[0])` 之后**没有** `BATTLE_READY`/`USER_OP`/`BATTLE_START`，日志即停在该点击包。服务端对该点击返回的是**交丹对白**（游戏内文本「不要再攻击我啦」），不是开战。

旧实现的问题：水魂步点击后只轮询「等战斗开始」10s，超时即 `flowFailed`。因此永远不会补发 `TASK_TALK(61242,[400600509])`，也就拿不到 `400059 妖怪内丹`，流程卡死在对白上（对白文本未回「这倒是省事不少」）。

修复后的水魂步行为（`wpe_hook.cpp` `RunEightTrigramsWaterSoulInteraction`）：

1. 点击前布防 `captureNextTaskTalkResponse`，随后收到的 `TASK_TALK_BACK` 写入进度状态。
2. 单次等待「开战或对白」谁先到就以谁为准（窗口 `EIGHT_TRIGRAMS_BATTLE_START_WAIT_MS`），避免对白路径白等超时、战斗路径被提前打断。
3. 开战分支：走 `RunEightTrigramsBattleLoop`；战后仍按实测顺序补 `CLICK_NPC(61242,[0])` -> `TASK_TALK(61242,[dialogId])`。
4. 对白分支：用回包取回的 `dialogId`（回包缺失时退回 `400600509`）补发 `TASK_TALK(61242,[dialogId])`，再等 `type=15` 奖励回包拿内丹。
5. 若点击响应已直接发放内丹，则跳过补发，避免多余任务请求。
6. UI 回显实际使用的 `dialogId` 与奖励状态，便于下一轮抓包核对服务端下发的真实对话编号。

同轮抓包还确认：整轮流程的每步形态是 `CLICK_NPC(npcId,[0])` + `TASK_TALK(npcId,[dialogId])`，带选项的先发 `TRAIN_INFO_SEND(7,[dialogId,chooseId])`；流程推进到 `61242` 前的顺序与 `EIGHT_TRIGRAMS_STEPS` 一致。抓包中出现的 `TASK_TALK(npcId,[1111111])` 由真实 AS3 客户端发出（native 无 `1111111` 字面量），说明 hook 与运行中的 AS3 客户端并存发包。

### 墨魂卡点根因（本次修复）

症状：墨魂战斗能正常进入、自动战斗正确、战后刷新能拿到阴阳石，但界面永久停在「少废话，看打！」对话框。

运行时抓包（发送侧 87 行，中间开始）末 6 行是决定性证据：

```
83 CLICK_NPC(101201,[0])
84 TASK_TALK(101201,[400600803])
85 CLICK_NPC(101201,[0])      <-- 多余，本次修复删除
86 TASK_TALK(101201,[400600804])
87 heartbeat
```

根因（两条 AS3 路径交叉确认）：

1. **语义错位**：`CLICK_NPC(npcid,[0])` 是 `TaskControl.getTaskDialog()`（`TaskControl.as:1006`，由 `MapControl.onClickNpcNew` -> `MASTERISINSPECIALAREA` 链路触发）的「打开任务对话」形态；开战的形态是 `BattleControl.onClientNpc`（`BattleControl.as:384`）的 `CLICK_NPC(sid,[counter])`，`counter` 从 1 起。
2. **延迟回放导致卡界面**：line 85 的点击让服务端重推 `400600803`。若该回包在战斗界面存在期间到达，`TaskControl.onGetDialogBack` 会把它存进 `tDialogObj`（`TaskControl.as:305-310`）而不立即显示；战斗结束走 `onBattleWin` 时再 `opDialogAfterBattle(this.tDialogObj,true)` 重放（`TaskControl.as:222-225`）。于是战斗与奖励都正常，界面却停在 `400600803` 的「少废话，看打！」按钮上等待点击。

修复（`wpe_hook.cpp` 墨魂 804 分支）：

1. 删除 804 步多余的 `CLICK_NPC(101201,[0])`，只保留 `TASK_TALK(101201,[400600804])` 确认 alert。
2. 未观测到开战信号时**不硬失败、不触发自动重启**（重启会重发该点击、再次重推 803），只提示后继续进入战斗循环等待战斗结束。
3. 保留战后固定 `TASK_TALK(101301,[400600805])` 收口。

与 803 步的分工：803 是 Talk 步，按 AS3 普通窗口形态发 `CLICK_NPC(101201,[0])` + `TASK_TALK(101201,[400600803])`（等价于点「少废话，看打！」）；服务端在确认该对话后推进任务并发起战斗。

抓包同时确认：`400600803`/`400600804`/`400600805`/`400600806` 在本地 XML 中分别是 `desc`/`alert`/`desc`/`desc`，本地 `.xml`/`.bin` 中**没有** `<battle>` 节点与「少废话」文本（`<battle`、`battle`、`400600803` 在 `taskprop_4006000.bin` 中均 0 命中）。

## 实现规则

- 不要在 `400600501` 和水魂战斗之间插入 `400600506` 或 `400600502` 的额外 `TASK_TALK`。
- 不要在水魂战斗入口前猜测添加额外 `TASK_TALK`；战斗胜利后的点击与确认只按下节运行时抓包执行。
- `400600102/202/302/402/505/602/702/802` 是 XML 对话资源；没有 AS3 或运行时回包证据时，不能把它们写成已验证的自动 `TASK_TALK` 参数。
- `400600104` 不是 XML-only 对话：它是 AS3 已核实的失败终止分支 `(21210,400600104)`，只能在等级不足时作为服务器结果出现，不能作为正常步骤放入顺序链。
- `400600103/203/303/403/603/703` 是 alert 或目标解释节点，不能仅凭 XML 生成自动包。
- `400600803/804` 是墨魂 XML 对话/目标资源，不能直接声称是 AutoBaGua 自动模块先发的两个任务包；native 若模拟普通窗口，必须标为兼容实现。
- Native 普通任务窗口的墨魂兼容链（**本次修复后**）固定为：`CLICK_NPC(101201,[0])` -> `TASK_TALK(101201,[400600803])`（确认「少废话，看打！」，服务端据此推进任务并开战）-> `TASK_TALK(101201,[400600804])`（确认 804 战斗目标 alert，**不再补 `CLICK_NPC(101201,[0])`**）-> 等待战斗开始/结束 -> 战后 `TASK_TALK(101301,[400600805])`。803 没有 `<choose>`，不发 `TRAIN_INFO_SEND`；804 的确认依据是 AS3 `TaskView.onCloseAlertHandler()`「确定」回调调用 `onDialogComplete(currentDialogID)`，再由 `TaskControl.taskDialogComplete()` 按 `npcid/dialogID` 发 `TASK_TALK`；805 是 AS3 `onSendAutoBaGua()` 在 AutoBaGua 路径下的原生发送参数（对应 `onNext(101301,400600805)` 进入状态 24）。803/804 两步已由本次运行时抓包（发送侧 87 行）验证；805 仍是源码推导。
- `400600805` 是墨魂战后客户端发送的 `TASK_TALK(101301,[400600805])`（`TaskControl.onSendAutoBaGua()` 在 `isAutoBaGua` 下直发），随后 `AutoBaGuaView.onNext(101301,400600805)` 进入状态 24；`400600806` 的后续行为待确认。
- 八卦灵盘的场景/NPC/迁移顺序以 `AutoBaGuaEntrance.swf` 为准，XML 只用于解释任务文本、分支和奖励。
- 当前 C++ 水魂段按抓包执行 `61242/400600509`，随后 native 普通任务兼容流程先用墨艮 `400600505` 完成交接，再用 `400600601` 开始下一阶段；柿果段固定处理 `91435/400600704` 转场后，native 还需在 `10001` 发送墨兑交接对话 `400600702`，再发送带选择的 `400600801`。`400600507` 仅作提示，不发包。这些兼容步骤不等同 AS3 自动包序列。
