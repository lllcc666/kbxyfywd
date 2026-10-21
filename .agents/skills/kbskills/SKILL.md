---
name: kbskills
description: 卡布西游 SWF/AS3 逆向分析技能。用于任务、周活动、小活动、封包、Opcode、Hook、自动化分析，以及把结果快速落到 C++/UI 时使用。
---

# kbskills

## 直接流程
1. 先确认活动名、活动 ID、SWF 名、现象、是否周活动。
2. 先查当前仓库和 `swf_cache/decompiled/`，有现成结果就直接复用。
3. 本机有 `FFDec/JPEXS/ffdec-cli/ffdec.jar` 就直接用，不再额外找工具。
4. 周活动必须先刷新远端 `weeklyactivity`，不要把本地缓存当最新。地址：`http://enter.wanwan4399.com/bin-debug/assets/weekly/weeklyactivity`
5. 按“入口 -> 控制 -> 模型/常量 -> 视图/节点 -> 协议”顺序读。
6. 结论至少要包含：实现位置、AS3 文件、关键 opcode/params、关键字段、问题原因、下一步。

## 必须遵守
- 本地 `swf_cache/weeklyactivity` 只能当缓存或对照，不能当最新依据。
- 本地 `swf_cache/decompiled/<swf名>/scripts` 已有结果时，直接使用，不要重复下载。
- 八卦灵盘必须同时对照内置 `AutoBaGuaEntrance.swf` 的自动步骤；不要把 `taskprop_4006000.xml` 的每个 `dialog`/`alert` 节点直接展开为发包步骤，详细校正见 `references/eight-trigrams-flow-corrections.md`。
- 对八卦灵盘，执行顺序以 `AutoBaGuaView.STEP_CONFIG` 的点击状态和 `onNext()` 的结果分支为准；主游戏 `TaskControl.onSendAutoBaGua()` 决定自动路径何时补发 `TASK_TALK`，`AutoBaGuaControl` 只提供自动模块的发送封装。XML 只解释对话、提示、奖励和目标场景，不能单独证明某个 `dialogId` 是自动发包参数。
- 八卦水魂战后抓包确认：`CLICK_NPC(61242,[0])` -> `TASK_TALK(61242,[400600509])`，不等待 type 15 奖励回包。此后 native 普通任务窗口兼容链在 7002 用墨艮 `400600505` 完成交接，再发送带选择的 `400600601`；这是兼容流程，不是 AS3 AutoBaGua 状态包。柿果段则在固定转盘迁移 `TASK_TALK(91435,[400600704])` 后进入 10001，先用墨兑 `400600702` 确认收果，再选择 `400600801`；墨魂段战斗结束后还需发送 `TASK_TALK(101301,[400600805])`（AS3 `onSendAutoBaGua()` 直发参数）。AS3 状态和 native 兼容细节见参考文档。`400600507` 是提示节点，不发包。
- 八卦回包不能只按 `(npcId, dialogId)` 判断是否补发：`TaskControl.opDialogAfterBattle()` 对 type 1 且 `itemCount > 0` 走奖励提示并直接推进，type 15 走 `getAwardWithoutComplete()` 并产生 `(0,0)` 推进事件，这两类都不补 `TASK_TALK`；只有进入 `onSendAutoBaGua()` 的分支才补发。
- type 11 且 `dialogId == 1111111`（AS3 判定字段是 `params.id`）走 `TaskControl.opDialogBackType11()` -> `EightTrigrams`，动画结束才派发八卦推进事件，也不补发 `TASK_TALK`；火炎的 `(31231,31231)` 是例外：入口 `AutoBaGuaView.onNext()` 只推进 `curIndex=6` 并转场景，不自己发包；再发 `TASK_TALK(31231,[31231])` 由主游戏侧 `TaskControl.onSendAutoBaGua()` 处理 `(31231,31231)` 回包时补发。
- `(21210,400600104)` 是 AS3 已核实的“妖怪等级不足 40 级”失败终止分支（提示后 `onClose()`），不是正常执行步骤，不能放进顺序链中间；`400600805` 是墨魂战后客户端要发送的 `TASK_TALK(101301,[400600805])`（`onSendAutoBaGua()` 直发），不能当作“仅回包记录”。
- 包名对照（kbskills 文档里的 `TASK_TALK / TASK_TALK_SEND / TASK_TALK_BACK / TASK_CLICK_NPC / TRAIN_INFO_SEND / TASK_FINISHED_BACK` 都是 C++ 侧改写名，AS3 源码里不存在这些字面量）：`TASK_TALK` -> `MsgDoc.OP_CLIENT_REQ_TASKTALK` send=1184788/back=1315860；`TASK_CLICK_NPC` -> `MsgDoc.OP_CLIENT_CLICK_NPC` send=1186048/back=0；`TRAIN_INFO_SEND` -> `MsgDoc.OP_CLIENT_REQ_TRAIN_INFO` send=1184772/back=1316142；`TASK_FINISHED_BACK` -> `MsgDoc.OP_CLIENT_REQ_TASKFINISHED` send=1184775/back=1315847。在 AS3 中按常量名搜索时应使用 `OP_CLIENT_REQ_TASKTALK` 等真实名。
- XML 的读取链是 `TASK_TALK_BACK.dialogId` -> `PropertyPool.getTaskProps(dialogId)` -> `children().(@id == dialogId)[0]` -> `TaskXMLParser.parseXML()`；`<dialog>` 书写顺序不是自动执行队列，`alert/desc/choose/battle/flash` 只能按其运行时解释使用。
- 只有 AS3 明确发送、`onNext()` 明确识别，或运行时封包已验证的编号，才能写入自动发包表；XML 中存在但 AS3 没有对应分支的 `dialogId` 必须标为“未验证”，不能猜成步骤。
- 用户明确不需要新工具时，停止下载，继续用现有工具链。
- 不要因为一个路径缺失就改走泛搜或换别的流程。
- 卡住时先说明卡点，再按本技能既定顺序往下，不要跳步。

## 先看什么
- 入口类：`*Main.as`、`*Entrance.as`、`*Module.as`
- 逻辑层：`*Control.as`、`*Model.as`、`*Data.as`、`*Define.as`
- 展示层：`*View.as`、`*Win.as`、`*Item.as`
- 协议层：`MsgDoc.as`、`MessageConst.as`、`opcode`、`params`

## 快速判断
- 周活动优先看 `weeklyactivity`
- 本地已有反编译结果，直接看 `swf_cache/decompiled/<swf名>/scripts`
- 本地没有结果，再下载对应 SWF 并用 FFDec 导出 `script`

## 常见模块
- 新活动协议：`1185429 / 1316501`
- 旧活动协议：`1184833 / 1324097`
- 小游戏：`1184789 / 1315861`
- 跳舞大赛：`1187375 / 1318447`
- 灵玉：`1185809 / 1316881`
- 妖怪背包：`1187329 / 1318401`
- 进入世界：`1183750 / 1314822`

- 协议族对应 AS3 常量名：新活动 `OP_CLIENT_ACTIVITY_QINGYANG_NEW`，旧活动 `OP_CLIENT_ACTIVITY_QINGYANG`，小游戏 `OP_CLIENT_LITTLE_GAME`，跳舞 `OP_CLIENT_DANCE_ACTIVITY`，灵玉 `OP_CLIENT_SPIRIT_EQUIP_ALL`，妖怪背包 `OP_CLIENT_REQ_SPIRIT_LIST`，进入世界 `OP_CLIENT_REQ_ENTER_WORLD`。
## 需要细节时再展开
- 通用分析套路：`references/module-game-analysis.md`
- 小游戏/周活动自动化落地流水线与实现红线：`references/workflow-little-game-automation.md`
- 周活动索引：`references/weekly-activities-analysis.md`
- 协议与 opcode：`references/protocol.md`、`references/protocol-base.md`、`references/opcode-index.md`
- 脚本辅助：`scripts/analyze_swf.py`、`scripts/calc_opcode.py`
- 任务自动化通用指南：`references/task-automation-analysis-guide.md`
- 八卦灵盘校正记录：`references/eight-trigrams-flow-corrections.md`

## 不要做
- 不要先泛搜再推断
- 不要复述大段代码，直接给结论
- 不要把仓库里的路径习惯写成通用规则
- 新增活动时优先只接主流程完成，不默认展开礼包、兑换、买宠等入口，除非用户明确要求。
## 额外经验

- 搜索 D 盘工具时，先查 `D:\Program Files (x86)\FFDec\ffdec.jar` 和 `ffdec-cli.exe`，再按顶层目录做定向递归，不要直接整盘 `D:\` 全量递归。
- `FFDec` 可直接用 `java -jar "D:\Program Files (x86)\FFDec\ffdec.jar"` 启动；先跑 `-help export`，再执行 `-export script ...`。
- 周活动固定流程：刷新远端 `weeklyactivity` -> 下载对应 SWF -> 用 FFDec 导出 `scripts/` -> 按 `入口类 -> 控制 -> 模型/常量 -> 视图/节点 -> 协议` 顺序分析。
- 这条流程是固定流水线，不需要再额外思考或泛搜；只要定位到最新周活动，就直接按这条链路往下走。
- 落地到自动化后，"编译通过"不等于逻辑正确：先按 AS3 状态机核对（回包成对等待、多阶段活动的状态迁移、各操作门槛逐字段比对），细节见 `references/workflow-little-game-automation.md`。
- 结论与修复都要附 AS3 文件行号依据；"看起来合理"的推断不写进代码。交付前对改动做一次只读 review（含边界与回退链路）；子代理没给出结论时，自己逐条核对并附证据。
- 现象相同、原因常不同：界面"只扣次数不发奖励 / 提前结束 / 该扫荡却走完整流程"这类表现，必须先用 AS3 与本地状态机逐段对照定位，再动代码。
- 新周活动很可能只是旧活动的轮换或微调：动手分析前先 `git log --oneline -- src/hook/`、`git show <rev>:<file>` 翻历史实现，确认同构旧活动是否已存在（往往带完整状态机与 opcode），能复用就不从头推。
- 过期活动就是死代码，报废当轮就删干净（按钮/JS/入口/线程体/发包族/回包处理器/注册/状态结构体），不要攒着；判据与删除顺序见 `references/weekly-activities-analysis.md` 的「八、轮换复用与过期清理」。
