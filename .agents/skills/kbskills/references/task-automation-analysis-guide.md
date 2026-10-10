# 任务自动化分析指南

## 三层模型

任务自动化必须拆成三层，不能把 XML 文件顺序直接当成发包队列：

1. **AS3 执行控制层**：决定状态、事件、回调和实际分支。八卦灵盘以 `AutoBaGuaView.curIndex`、`STEP_CONFIG`、`onNext()` 为准（`swf_cache/decompiled/AutoBaGuaEntrance/scripts/AutoBaGua/view/AutoBaGuaView.as`，主游戏侧集成在 `MainGame_remote` 变体的 `TaskControl.as`）。
2. **XML 解释层**：`taskinfo.xml` 提供 root/subtask、前置关系、目标场景和奖励；`taskprop_*.xml` 提供 `desc`、`choose`、`alert`、`battle`、`flash`、`targetScene` 等节点语义。
3. **Native packet recipe 层**：根据 AS3 事件链，把点击、对话、场景、战斗和结果回包落成 C++ 发包和等待条件。

当三层冲突时，执行顺序以 AS3 和实际回包为准；XML 用来解释任务，不用来生成线性步骤。

## XML 如何解读

- `data/taskinfo.xml` 是任务骨架，不是脚本。它描述 `taskID`、`subtaskID`、`targets`、`award`、`after/before`、`condition` 和状态关系。
- `taskprop_<group>.xml` 是运行时节点目录。`dialog id` 是节点编号，不是 root 任务编号；一个子任务可以有多个 dialog。
- `desc` 是对白或完成节点，`choose` 是普通任务 UI 的选项元数据，`alert` 是提示/目标说明，`battle` 是战斗定义，`flash` 是动画/过场，`targetScene` 是任务事件的目标场景。
- `TaskXMLParser` 只把 XML 转成运行时对象（`MainGame_remote/.../control/task/util/TaskXMLParser.as` `parseXML` :36）；`TaskView`/`TaskControl` 决定何时取 XML、何时发包、何时等待回包。实际读取链：`TaskControl.getPropsBack()`（:575-589）里 `PropertyPool.instance.getTaskProps(dialogId, ...)` -> `props.children().(@id == dialogId)[0]` -> `TaskXMLParser.parseXML(xml, subtaskID, taskID, dialogId)`。
- `dialogId == 0`、`dialogId == npcid`、空 XML 叶子节点和 `1111111` 结果事件都可能走 AS3 特殊路径，不能要求每一个都有独立 taskprop 或 TASK_TALK 步骤。
- 普通任务 UI 的 `choose flag="3"` 通常由 `TaskControl.taskDialogComplete()` 发送 `TRAIN_INFO_SEND`（AS3 名 `MsgDoc.OP_CLIENT_REQ_TRAIN_INFO` send=1184772）后再发送 `TASK_TALK`（AS3 名 `MsgDoc.OP_CLIENT_REQ_TASKTALK` send=1184788）；活动专用自动模块可能绕过这条 UI 路径，必须以它自己的控制器为准。

## 八卦灵盘基准流程

`AutoBaGuaView.as` 有 24 个 `curIndex` 状态。它们是 AS3 自动模块的场景、点击和结果迁移锚点，不等于 XML 节点数量，也不等于 native recipe 的发包数量。

| 状态范围 | AS3 已核实迁移 | XML 仅作解释 | 证据等级 |
|---|---|---|---|
| 1-4 | `2001/21210` -> `2005/21511` -> `3001/31101`，经过 `400600101`、`(21511,0)`、`(31101,1111111)`、`400600201`；另有 `(21210,400600104)` 等级不足失败终止分支 | `400600101/103/102/201`；`400600104` 是 AS3 已核实的失败分支，不是正常 XML 步骤 | AS3 已核实 |
| 5-8 | `3002/31231` 的 `400600204` 特殊提交 -> `4001/41101` -> `4003/41312` | `400600203/204/202/301/303` | AS3 已核实；火炎前置包待补证 |
| 9-13 | `5002/51206` -> `5003/51305` -> `6001/61101`，包含 type 11 和 `(0,0)` 结果事件 | `400600302/401/403/402/501` | AS3 已核实 |
| 14-17 | `6002/61242` 战斗 -> `(0,0)` -> `7002/71201` -> `(71201,1111111)` -> `400600601` | `400600506/507/505` 只作 XML 解释，不单独生成自动包 | AS3 已核实 |
| 18-22 | `8003` -> `(0,0)` -> `9001/91101` -> `9004/91435` -> `(91435,400600704)` -> `10001/101101` | `400600603/602/701/703/704/801` | AS3 已核实 |
| 23-24 | `10002/101201` 点击/战斗 -> 战后发 `TASK_TALK(101301,[400600805])` -> `(101301,400600805)` -> `2001/21210` | `400600803/804/806/802` 只作 XML/结果解释；`806` 后续待确认；`400600805` 是 AS3 已核实的发送参数 | AS3 部分核实 |

关键结果：

- `400600507` 是 XML alert，不是独立 AS3 自动发包步骤；`400600505` 是墨艮的 XML 交接对话，不是 AS3 自动状态分支本身。
- Native 普通任务窗口兼容链不同：实测水魂确认 `61242/400600509` 后进入 7002，先点墨艮并发送 `400600505` 完成交接，再点墨艮并发送带 `chooseId=3` 的 `400600601`。1.14 native recipe 曾包含此顺序；应作为兼容行为记录，不能写成 AS3 原始协议。
- `400600704` 的 AS3 元组是 `(91435,400600704)`，不能替换成无 NPC 的 `1111111`。
- `400600805` 是墨魂战后客户端发送的 `TASK_TALK(101301,[400600805])`（`onSendAutoBaGua()` 直发），随后 `onNext(101301,400600805)` 推进到状态 24；`400600806` 的后续迁移尚未由 `AutoBaGuaView.onNext()` 直接确认。
- `400600803/804` 虽然存在于 XML，但不能直接写成 AutoBaGua 自动模块先发的两个任务包。
- Native 普通任务窗口墨魂兼容链固定确认 803（“少废话，看打！”）和 804（战斗目标 alert）：分别发 `TASK_TALK(101201,[400600803])`、`TASK_TALK(101201,[400600804])`，各用 400ms 固定延时后再等待真实战斗，战后发送 `TASK_TALK(101301,[400600805])`。804 包依据 AS3 alert 确定回调和普通 `TaskControl.taskDialogComplete()` 契约推导，尚无运行时抓包验证；805 是 AS3 `onSendAutoBaGua()` 的原生发送参数。不要把这些记成 AutoBaGua 模块的完整原生步骤。

## 水魂和协议规则

- `400600502/503/504` 的 XML `desc id` 是垂钓老人 `61202`，不能当作水魂自动步骤。
- AS3 自动流程在 `6002` 使用特殊目标 `61242`；静态精灵 ID `401311` 不能替代该场景目标。
- 水魂战斗结束后等待 `(npcId=0, dialogId=0)`，再进入 `7002`；随后由 `71201` 的 `1111111` 和 `400600601` 分支推进。
- 上述是 AS3 AutoBaGua 基线；native 未设置 `isAutoBaGua` 时，仍须完成 XML/历史 native recipe 所示的墨艮 `400600505` 交接，再开始 `400600601`。
- 柿果 native 兼容链：固定转盘迁移 `(91435,400600704)` 后进入 `10001`，点墨兑并发 `400600702` 收果确认，再点墨兑并发送带 `chooseId=3` 的 `400600801`。AS3 基线是 `(91435,400600704)` -> 场景 `10001` -> `(101101,1111111)` -> `(101101,400600801)`；`400600702` 是普通任务窗口兼容对话，不要把它说成 AS3 自动分支。
- AS3 协议概念是 `OP_CLIENT_CLICK_NPC` 和 `OP_GATEWAY_BATTLE_END`；native 的 `SendBattlePacket(...)` 及其特殊 params 只能标为抓包/兼容实现，不能当作 AS3 函数名。
- `400600704` 应记录为 `(91435,400600704)`；`1111111` 只适用于对应的 type 11/动画结果分支。
- 普通任务窗口的选择流程是 `TRAIN_INFO_SEND(params=7,[dialogId,chooseId])` 后 `TASK_TALK_SEND`；内置 `isAutoBaGua=true` 路径由 `onSendAutoBaGua()` 直发 `TASK_TALK_SEND`。当前 native 若采用前者，必须明确标为兼容路径。
- `currentId = dialogId / 100 % 4006000` 等恢复计算只能作为 AS3 任务列表语义记录；native 的 `g_eightTrigramsResumeStepIndex` 是 recipe 索引，不能直接当作 AS3 `curIndex`。

## 文档定稿标准

- `taskinfo.xml` 和 `taskprop` 只解释任务骨架及节点语义，不直接生成线性 packet 队列。
- 只有 AS3 明确发送、`onNext()` 明确识别，或运行时抓包已验证的编号，才能写入“已验证自动包表”。
- XML-only 节点必须标为“XML 解释”或“native 兼容假设”，不能标成 AS3 自动发包事实。
- 八卦灵盘当前应标记为：AS3 状态链已核实、XML 骨架已核实、native packet recipe 部分核实、完整运行闭环待校正。

## 通用落地模板

1. 从 `taskinfo.xml` 建立任务骨架和子任务顺序。
2. 从 `taskprop` 标注节点语义，不把 XML 节点数量当状态数量。
3. 从 AS3 控制器提取入口、状态、迁移条件、特殊 NPC 和回包。
4. 将每个状态拆成最小动作：`CLICK_NPC`、`TASK_TALK_SEND`、`ENTER_SCENE_SEND`、战斗启动、战斗回合和结果等待。
5. 对每个等待条件记录 opcode、params、body 字段和恢复点。
6. 未被 AS3 或抓包确认的 packet 必须列为待确认，不用 XML 外观补猜。
