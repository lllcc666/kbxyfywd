# 任务自动化分析与编排规范

## 1. 目标

这份文档只做一件事：把“任务自动化”拆成可逐步推进、可验证、可扩展的流程。

- 先分析单个系列，再扩展到下一个系列
- 先确认 XML 骨架，再确认运行时节点和回包，最后才落到自动化步骤
- 这份文档的最终产物不是“分析框架”，而是“可执行的 packet recipe”：每一步都要写清封包从哪来、怎么发、等哪个回包、从哪里恢复
- 现在只以 `八卦灵盘` 为样例，后续系列按同一模板追加

## 2. 代码来源优先级

自动化任务要分成三个层次，执行控制优先于 XML 外观：

1. **执行控制层：反编译 AS3**
   - `TaskInfoXMLParser.as`
   - `TaskXMLParser.as`
   - `TaskList.as`
   - `SortedTaskList.as`
   - `TaskView.as`
   - `TaskControl.as`
   - 八卦灵盘还必须包含 `AutoBaGuaView.as` 的 `STEP_CONFIG` 和 `onNext()`。
2. **任务解释层：XML**
   - `data/taskinfo.xml`
   - `taskprop/<dialogId/1000>0.xml`
   - `config/tasktheme.xml`（运行时由 `SortedTaskList` 加载；当前仓库快照里未直接存放该文件）
3. **发包落地层：本地自动化实现**
   - `src/hook/wpe_hook.cpp`

补充说明：

- `data/taskinfo.xml` 只提供 root / subtask / targets / award / after / before，不提供逐步动作。
- `taskprop/<dialogId/1000>0.xml` 才是运行时对话节点的第一手来源；如果本地缺失，只能写到骨架层，不能把 packet 序列猜成最终结论。
- `TaskXMLParser`、`TaskView`、`TaskControl` 负责把运行时节点翻译成动作类型。
- `src/hook/wpe_hook.cpp` 是当前仓库里已经落地的 packet recipe 和恢复逻辑；但就 `皇城危机` 而言，现阶段只落了进度同步和 UI 状态，不是完整步骤执行器。
- `USER_TASK_LIST_SEND/BACK` 只负责告诉你“当前接了什么、完成了什么”，不负责推动剧情。
- `dialogId` 是运行时步骤编号，不是 root `taskID`；一个 `subtaskID` 下面可以挂多个 `dialogId`。`4006000` 的样例和皇城里的 `200100504` 特例都说明了这一点。
- UI 里看到“前往”只代表当前节点带了 `targetScene`，不代表完整 packet recipe 已经齐了。
- `TaskView.onGetDialogBack()` 只有在 `dialogId != 0` 且 `dialogId != npcid` 时才会继续取 `taskprop`；空 XML 叶子节点还会直接 `onDialogComplete()`，所以不是每个 UI 步骤都会对应一份独立 `taskprop`。
- XML 的运行时读取链是 `TASK_TALK_BACK.dialogId -> PropertyPool.getTaskProps(dialogId) -> props.children().(@id == dialogId)[0] -> TaskXMLParser.parseXML()`；因此 `<dialog>` 的书写顺序不等于执行顺序。`<desc>` 是显示内容，`<choose>` 是普通窗口选择元数据，`<alert>` 是提示，`<battle>`/`<flash>`/`targetScene` 由运行时事件继续处理。
- `OP_CLIENT_CLICK_NPC` 在 AS3 的 `MsgDoc` 中 `back=0`，NPC 点击本身不是可等待的任务回包；点击后可能先收到独立的 NPC 任务列表，也可能直接进入物品、战斗或任务对话结果。
- `TaskDialog` 里的 `choose` 节点先转成 `sendChooseId`，最后由 `TaskControl.taskDialogComplete()` 统一补 `TRAIN_INFO_SEND` + `TASK_TALK_SEND`，不是在 UI 层直接发包。
- 必须区分两条 AS3 契约：普通任务窗口在 `isAutoBaGua=false` 时，点击选择按钮走 `TRAIN_INFO_SEND(params=7,[dialogId,chooseId])` 后再发 `TASK_TALK_SEND`；内置 `AutoBaGua` 在 `isAutoBaGua=true` 时由 `WindowLayerControl.hasBaGuaTask()` 和 `TaskControl.onSendAutoBaGua()` 直发 `TASK_TALK_SEND`，不经过普通选择窗口。
- 当前 native 辅助没有接管游戏内 `GameData.playerData.isAutoBaGua`，也没有驱动 `AutoBaGuaEntrance.swf` 的 UI 状态，因此 native packet recipe 必须模拟普通任务窗口的按钮点击。`AutoBaGuaView` 的 24 个状态只作为场景/NPC/结果迁移锚点，不能据此删除 `TRAIN_INFO_SEND`。
- `targetScene` 也不是直发包，它先随 `DIALOGFINISHED` 事件回到 `TaskControl`，再由 `TaskControl.toOtherScene()` 分流。
- 目前本地快照里只看到 `taskprop_1002000.xml`、`taskprop_1003000.xml`、`taskprop_4006000.xml`，`taskprop_2001000.xml` 不在明文目录，也不在 `data_data.zip`。
- 我们还尝试了官方常见路径 `http://enter.wanwan4399.com/bin-debug/assets/taskprop/2001000.xml` 和 `http://kbxy.wanwan4399.com/bin-debug/assets/taskprop/2001000.xml`，当前都返回 404。

结论判断时，优先相信运行时包行为，其次才是 XML 外观顺序。

## 3. 标准分析流程

每一个新系列都按同一条流程走，不跳步，不并行猜结论。

1. 先定范围
   - 确认系列名、根 `taskID`、预期的子任务范围
   - 如果系列名不稳定，先以 `taskID` 为准
2. 再找锚点
   - 在 `data/taskinfo.xml` 里定位根任务和子任务段
   - 在 `TaskArchivesVersion3` 或相关 AS3 里找主题名、按钮名、入口名
3. 抽任务骨架
   - 记录 `subtaskID`、`targets`、`award`、`after`、`before`、`condition`
   - 标出哪些节点是对话、采集、战斗、动画、结果收口
4. 读运行时解释
   - 看 `TaskXMLParser` 如何把 XML 转成运行时节点
   - 看 `TaskView` / `TaskControl` 如何把节点翻译成动作
   - 看 `TaskList` 如何判定状态
   - 看 `TaskInfoXMLParser` 如何裁剪可接任务和前置关系
5. 还原执行顺序
   - 先按 AS3 控制器还原运行时状态和结果分支
   - 再用 XML 解释对白、选择、提示、奖励和目标场景
   - 最后按当前 native 所模拟的 AS3 契约落成发包顺序
   - 如果二者冲突，以已经落地的运行时包行为为准
   - 如果 `taskprop` 缺失，先用攻略页和场景对象把“去哪里、点什么、先后顺序”补成动作链，再回 AS3 / 抓包补 `dialogId`、`chooseId` 和 packet
6. 拆 packet 规则
   - 逐个节点标注封包来源、发送包、回包、等待条件和恢复点
   - 特殊节点单列出来，例如 `choose flag="3"`、`battle`、`flash`
7. 标记中断与恢复
   - 记录超时、失败、重试、跳步、已完成的判断点
   - 如果实现里已有恢复游标，就把恢复点单独写出来
8. 输出结论
   - 只输出已经验证的内容
   - 未确认的部分必须列入“待确认”

分析时的顺序不能反过来：

- 不能先写自动化步骤，再倒推 XML
- 不能先看 UI 按钮，再当成执行顺序
- 不能只看单个对话节点就判断整条链
- 不能把一个任务块里的特殊分支当成通用模板

## 4. 基本约束

- 不要一次性分析所有任务系列
- 每次只做一个系列，或者一个系列里的一个阶段
- 不要把 UI 排序当成执行顺序
- 不要只看任务名，必须同时看 `taskID`、`subtaskID`、`dialogId`、`sceneId`
- 只要某一步的 packet、回包、场景切换不明确，就先停下来补证据
- 任务完成判定以“最后一个子任务”是否完成为准，不以系列标题为准

## 5. AS3 / XML 规则

### 5.1 `taskinfo.xml`

`taskinfo.xml` 定义的是任务骨架：

- `task id`
- 子任务 `subtask id`
- `describe`
- `award`
- `targets`
- `after` / `before`
- `type`
- `diff`
- `off`

对自动化来说，`taskinfo.xml` 只负责告诉你“这一条链有哪些节点”，不负责告诉你“节点怎么执行”。

### 5.2 `TaskInfoXMLParser`

`TaskInfoXMLParser` 负责把 XML 变成可用的任务清单。

- `parseClassfy()` 会根据 `after` / `before` 过滤可见任务
- `canInputOrNot()` 会继续检查前置任务是否已经满足
- `getPrefixListByAfterID()` 说明了前置关系不是纯文本判断，而是按任务编号段映射
- `parseTargetScene()`、`parseAward*()`、`getGoodsInTaskInfo()` 提供了每个子任务的执行目标和结果

这意味着“任务能不能接、能不能显示、是否该推进下一步”都有明确规则，不能靠人工猜。

### 5.3 `TaskXMLParser`

`TaskXMLParser` 负责单个任务节点的运行时解释，支持的关键节点包括：

- `alert`
- `desc` / `choose`
- `battle`
- `flash`
- `ai`
- `otherpopup`
- `condition`
- `targetScene`

其中最重要的约束是：

- 在当前 native 的普通窗口兼容链中，`choose flag="3"` 必须先发 `TRAIN_INFO_SEND`，再发 `TASK_TALK_SEND`；只有真正运行 `isAutoBaGua=true` 的内置模块才省略 `TRAIN_INFO_SEND`
- `battle` 节点必须等战斗状态真的起来
- `flash` 节点通常表示过场或收口，不等同于普通对话
- `condition` 必须满足后才能视为完成

### 5.4 `TaskList`

`TaskList` 负责任务状态。

- `0`：未接
- `1`：已接
- `2`：已完成
- `3`：受前置限制，暂不可接

它对整个系列的判断规则是：

- 看系列的第一个子任务决定“是否已接/是否可接”
- 看系列的最后一个子任务决定“是否已完成”

所以自动化文档必须按“子任务链”写，而不是按“系列名”写。

### 5.5 `SortedTaskList`

`SortedTaskList` 是任务档案 UI 的分组层。

- 主题名来自 `config/tasktheme.xml`
- `TaskArchivesVersion3` 只是展示层
- root 节点按主题名显示，不代表真实执行顺序

### 5.6 `taskprop` 缺口

`taskprop` 是运行时对话 XML，不等于 `taskinfo.xml`。

- `TaskView` 和 `TaskControl` 依赖 `PropertyPool.getTaskProps(dialogId)` 去取 `assets/taskprop/<dialogId/1000>0.xml`
- 本地如果没有对应的 `taskprop_XXXXXXX.xml`，只能说明缓存缺失，不代表任务不存在
- 这种情况下，先写清骨架和已确认 AS3 特殊分支，再把 packet 序列列入待确认
- 不要把 `TaskArchivesVersion3.swf` 当成任务执行数据源，它只是档案入口和展示层
- `200100504` 在 `TaskView` / `FaceControl` 里是硬编码特例，说明皇城有步级节点需要单独确认，不能只靠 `taskinfo.xml` 反推。

### 5.7 封包映射规则

任务分析时，必须把“节点类型”翻译成“发包动作”。当前仓库里可复用的约定是：

| 节点/场景 | 封包来源 | 发送顺序 | 等待/判定 |
| --- | --- | --- | --- |
| 进度查询 | `QueryTaskZoneUserTaskListProgress()` | `USER_TASK_LIST_SEND` | `USER_TASK_LIST_BACK` 返回后解析 accepted / finished 列表 |
| 普通对话 / 选择 | `SendTaskZoneTalkPacket()` | `TRAIN_INFO_SEND`（`chooseId != 0`） -> `TASK_TALK_SEND` | `TASK_TALK_BACK`，用 `dialogId` 驱动恢复点 |
| 点击交互 | `SendTaskZoneClickPacket()` | `CLICK_NPC` | 通常靠后续对话、战斗或场景回包推进 |
| 场景切换 | `TaskDialog.targetScene` / `TaskEvent.SENDTOOTHERSCENE` | `ENTER_SCENE_SEND` 或房间/神兽特殊分流 | 先过 `TaskControl.toOtherScene()`，再按场景类型决定具体包 |
| 战斗启动 | `SendTaskZoneClickPacket(..., true)` 或 `SendBattlePacket()` | `CLICK_NPC` 或战斗专用包 | 战斗开始后再发 `BATTLE_READY`，之后靠战斗数据和回包推进 |
| 特殊收口 | 例如 `400600704` | 特殊 `TASK_TALK_SEND` 或无包 | 以特殊结果回包或分支节点结束 |

补充：像 `200100504` 这种皇城步级特例，必须按 `dialogId` 粒度记，不要把它当成 `taskID` 或 `subtaskID` 的同义词。
补充：`targetScene` 在 AS3 里不是直接发包，它先变成任务事件，再由 `TaskControl.toOtherScene()` 分流。普通场景、`1002` 房间、`1013/1018` 神兽区不是同一条链。

### 5.8 缺口补证标准流程

当某条任务链缺 `taskprop`、缺 `dialogId`、缺 `chooseId`，或者只知道场景描述但不知道怎么发包时，必须按下面顺序补，不允许倒推：

1. 先判定是不是伪缺口。
   - `TaskView.onGetDialogBack()` 里 `dialogId == 0` 只会重新打开任务对话，不会进入 `taskprop`。
   - `TaskView.onGetDialogBack()` 里 `dialogId == npcid` 会直接完成，不会进入 `taskprop`。
   - `TaskDialog` 解析后如果 `describe / flash / battle / otherpopup` 都为空，会直接 `onAccept(true)` 收口。
   - 这种情况不能当成“少了一份 XML”，只能记为“该节点本来就不单独挂运行时对话层”。
2. 再锁静态骨架。
   - 用 `data/taskinfo.xml` 先定 root、subtask、`targetScene`、`country`、`after`、`before`、`award`。
   - 用 `data/npc.xml`、`data/map.xml`、`data/sprite.xml` 补场景对象和交互锚点。
   - 这一层只输出“去哪里、点什么、像什么动作”，不输出最终 packet。
3. 再读运行时解释。
   - 看 `TaskView.onGetDialogBack()`、`TaskDialog.dialogComplete()`、`TaskControl.taskDialogComplete()`。
   - 把每个锚点归类成 `Talk`、`Click`、`Battle`、`SceneAcquire`、`RewardOnly`、`Flash`、`BranchOnly` 之一。
   - `choose` 节点只记录 `sendChooseId`，不能在这一层直接补成包。
4. 再补包证据。
   - 记录对应的 `OP_CLIENT_CLICK_NPC`、`TASK_TALK_SEND`、`TRAIN_INFO_SEND`、`ENTER_SCENE_SEND`、`STARTBATTLE` 以及回包。
   - 还原 `DIALOGFINISHED` 里带出来的 `dialogId`、`sceneId`、`needchoose`、`ai`。
   - 如果本地没有 `taskprop_XXXXXXX.xml`，就去拿在线资源、抓包日志或运行时回包，不要继续猜。
5. 最后定稿。
   - 每一步必须写成 `来源 -> 发送 -> 回包 -> 恢复点`。
   - 只要还缺一个环节，就标 `待确认`，并写明缺的是 XML、回包还是恢复点。
   - 当且仅当静态锚点、运行时节点、packet 循环三者闭合，才算确定。

皇城危机的应用方式：

- `2001005` 的 `200100504` 已经说明这是步级特例，属于“运行时节点已知、XML 可能不单列”的类型。
- `2001007` 的石头、藤条、虎魔王属于“场景对象已知、packet 仍待补证”的类型。
- 这两类都不能靠 `taskinfo.xml` 自己闭环，必须回到第 4 步补回包。

## 6. 八卦灵盘样例

### 6.1 XML 结构

当前已验证的任务链是：

- `taskID = 4006000`
- `subtaskID = 4006001 ~ 4006008`
- XML 目标场景依次覆盖 `2001 / 3001 / 4001 / 5002 / 6001 / 7002 / 9001 / 10001`

这条链的重点不是“8 个子任务”，而是“8 个阶段 + 多个中间对话/采集/战斗/收口步骤”。

### 6.2 AS3 基准顺序与 native 兼容层

`AutoBaGuaView.STEP_CONFIG` 的 24 个状态和 `onNext()` 的结果分支，是八卦灵盘的 AS3 状态基准；它们不是 XML 节点列表，也不是 native 的一一对应发包表。当前 native 没有接管游戏内 `isAutoBaGua`，因此会在必要位置模拟普通任务窗口，但这只能作为兼容实现，不能反写成 AS3 AutoBaGua 的原始协议事实。

已核实的 AS3 阶段迁移如下：

| 阶段 | AS3 状态/结果链 | XML 解释 | 证据等级 |
|---|---|---|---|
| 1 | `2001/21210` -> `(21511,0)` -> `3001/31101`；另有 `(21210,400600104)` 等级不足失败终止分支 | `400600101`、`400600103`；`400600104` 是 AS3 已核实失败分支 | AS3 已核实 |
| 2 | `(31101,400600201)` -> `(0/31231,400600204)` -> `(31231,31231)` -> `4001` | `400600201`、`400600203/204` | AS3 已核实；火炎前置包待补证 |
| 3 | `(41101,400600301)` -> `(0,0)` -> `5002/51206` | `400600301`、`400600303` | AS3 已核实 |
| 4 | `(51206,400600401)` -> `(0,0)` -> `5003` -> `6001` | `400600401`、`400600403` | AS3 已核实 |
| 5 | `(61101,400600501)` -> `6002/61242` 战斗 -> `(0,0)` -> `7002/71201` -> `(71201,1111111)` -> `(71201,400600601)` | `400600506/507/505` 不单独生成 AS3 自动包；native 兼容对话另见下文 | AS3 已核实 |
| 6 | `(71201,400600601)` -> `8003` -> `(0,0)` -> `9001/91101` | `400600601`、`400600603` | AS3 已核实 |
| 7 | `(91101,400600701)` -> `9004/91435` -> `(91435,400600704)` -> `10001/101101` -> `(101101,400600801)` | `400600701/703/704/801` | AS3 已核实 |
| 8 | `10002/101201` 点击/战斗 -> 战后发 `TASK_TALK(101301,[400600805])` -> `(101301,400600805)` -> `2001/21210` 收口 | `400600803/804/806/802` 只作 XML/结果解释；`806` 后续待确认；`400600805` 是 AS3 已核实发送参数 | AS3 部分核实 |

特别注意：`400600507` 是提示节点，不能生成发包；`400600505` 不是 AS3 自动状态分支，不能只凭 XML 宣称是 AS3 自动包。它仍属于 native 普通任务窗口兼容链中的墨艮交接对话，见下文。`400600704` 的 AS3 迁移元组是 `(91435,400600704)`，不能替换成 `TASK_TALK(1111111)`。另外，`(21210,400600104)` 是 AS3 已核实的“妖怪等级不足 40 级”失败终止分支（提示后 `onClose()`），不是正常执行步骤。

当前 C++ 的 `EIGHT_TRIGRAMS_STEPS` 将 XML 对话、奖励和分支展开成多于 24 项的 native recipe。这可以作为实现快照单独记录，但不能称为 AS3 的 24 步原始发包顺序；其中 `400600102/202/302/402/602/702/802`、`400600803/804` 等节点仍需逐项区分“native 兼容假设”和“AS3 已验证动作”。`400600104` 不是 XML-only 分支而是 AS3 失败终止分支；墨魂战后 `400600805` 是 AS3 已验证的客户端发送参数（`TASK_TALK(101301,[400600805])`）。

### 6.3 八卦灵盘的特殊规则

- native 当前未接管内置 `AutoBaGua` 状态；普通任务窗口选择可以使用 `TRAIN_INFO_SEND` 再 `TASK_TALK_SEND`，但这是 native 兼容路径，不是内置 AutoBaGua 的通用规则。
- 内置 AS3 自动模块在 `isAutoBaGua=true` 时由 `WindowLayerControl` / `TaskControl.onSendAutoBaGua()` 直发 `TASK_TALK_SEND`，不经过普通选择窗口。
- `400600303` 是特殊场景点击，实际对象/发包参数需要区分 `41311` 静态对象与 `41312` wire 目标，并等待 `(0,0)` 结果后再进入下一场景。
- `400600506` 对应通天河 `61242` 特殊战斗目标；AS3 协议概念是 `CLICK_NPC`，native 的特殊编码需要单独标为抓包实现，不应当作 AS3 函数名。
- 水魂战斗结束后先等待 `(0,0)`，进入 `7002` 后按 `71201` 的 `1111111` 和 `400600601` 分支推进；不要把 `400600507` 或 `400600505` 当成额外自动任务包。
- Native 普通任务窗口兼容顺序：按实测包发送 `CLICK_NPC(61242,[0])` -> `TASK_TALK(61242,[400600509])`，进入 `7002` 后点击墨艮并发送 `TASK_TALK(71201,[400600505])` 完成交接；再点击墨艮，发送 `TRAIN_INFO_SEND(7,[400600601,3])` -> `TASK_TALK_SEND(71201,[400600601])`。`400600505` 的兼容用途由 1.14 native recipe 和 XML 对话语义支持，不代表 AS3 自动模块也发此包。
- 柿果 native 普通任务兼容顺序：固定发送 `TASK_TALK(91435,[400600704])` 转到 `10001` 后，点击墨兑并发 `TASK_TALK(101101,[400600702])` 确认收果；再点击墨兑，发送 `TRAIN_INFO_SEND(7,[400600801,3])` -> `TASK_TALK_SEND(101101,[400600801])`。AS3 状态基线对应 `(91435,400600704)` -> `(101101,1111111)` -> `(101101,400600801)`；702 是 native 对普通任务窗口的兼容确认，不是 AS3 自动分支。
- `400600704` 必须按 `(91435,400600704)` 记录；`1111111` 只适用于对应的 type 11/动画结果分支。
- `400600803/804` 是 XML 对话/目标资源，不能直接声称是 AutoBaGua 自动模块先发的两个任务包；native 当前若模拟普通窗口，必须明确标注为兼容实现。
- Native 普通任务窗口墨魂兼容链：点击 `101201` 后固定发送 `TASK_TALK(101201,[400600803])` 确认“少废话，看打！”，再固定发送 `TASK_TALK(101201,[400600804])` 确认战斗目标 alert，均按 400ms 延时，不等待动态对话回包；随后等待真实战斗，战后发送 `TASK_TALK(101301,[400600805])`。804 的确认包由 AS3 alert 确定回调和 `TaskControl.taskDialogComplete()` 发送契约推导，尚无运行时抓包验证；805 是 AS3 `onSendAutoBaGua()` 的原生发送参数。不代表 AutoBaGua 模块的完整原生步骤。
- `400600805` 是墨魂战后客户端发送的 `TASK_TALK(101301,[400600805])`（`onSendAutoBaGua()` 直发），随后 `onNext(101301,400600805)` 进入状态 24；`400600806` 的后续行为尚未由 `AutoBaGuaView.onNext()` 直接确认，不能写成与 805 完全等价。
- `g_eightTrigramsResumeStepIndex` 表示 native 的下一个待完成 recipe 项，不等同于 AS3 `curIndex`；进入步骤时保持当前索引，只有对应闭环完成后才前移。

### 6.4 封包闭环记录规范

八卦灵盘的 packet recipe 必须标注来源和证据等级，不能把 XML 节点直接当作发包步骤：

| 代表节点/事件 | 记录方式 | 说明 |
|---|---|---|
| `400600101` / `chooseId=3` | native 普通窗口兼容层：`TRAIN_INFO_SEND(7,[400600101,3])` -> `TASK_TALK_SEND(21210,[400600101])` | 这是普通任务窗口契约；内置 AutoBaGua 直发规则不同 |
| `400600303` | `CLICK_NPC(41312)` -> 等待 `(0,0)` | 场景特殊点击；不能直接写成普通 `TASK_TALK` |
| `400600506` | `CLICK_NPC` 特殊战斗入口 -> 战斗开始 -> `OP_GATEWAY_BATTLE_END(1186323)` | native 的具体 params 需注明抓包来源 |
| 水魂后 native 兼容链 | `CLICK_NPC(61242,[0])` -> `TASK_TALK(61242,[400600509])` -> `7002` 墨艮 `400600505` 交接 -> 墨艮 `400600601` 选择 `3` | 水魂两包为运行时抓包；墨艮 505 是普通任务窗口兼容对话，和 AS3 `(71201,1111111)` 状态分开记录 |
| `400600704` | 记录 `(91435,400600704)` | 不得写成无 NPC 的 `1111111` 收口 |
| `400600805` | 战后发送 `TASK_TALK(101301,[400600805])` -> `onNext(101301,400600805)` 进入状态 24 | AS3 明确分支（`onSendAutoBaGua` 直发参数） |
| `400600806` | 仅记录为可能的结果分支 | 后续迁移待确认，不与 805 自动等同 |
| `(0,0)` / `1111111` | 记录为运行时结果事件 | 不能机械发成 `TASK_TALK(0,[0])` 或套用到所有阶段 |

AS3 状态基线与 native 兼容 recipe 必须分开描述。当前 native 水魂段使用抓包确认的 `61242/400600509`，之后保留墨艮 `400600505` 交接再发送 `400600601`；已知其他偏差仍包括转盘收口、提前处理 `400600803` 等，不能因此标记整条链为 AS3 原生发包或完整闭环。

## 7. 后续系列的分析规范

后续像 `魔晶战记`、`地心传说`、`卡布历险` 这类长系列，统一按下面顺序推进：

1. 先定位系列根任务 `taskID`
2. 再列出所有子任务 `subtaskID`
3. 再从 `taskinfo.xml` 抽出 `targets`、`award`、`after`、`before`、`condition`
4. 再去 `TaskXMLParser` 查单节点类型
5. 再去 AS3 / 抓包确认 packet 顺序
6. 最后才写自动化步骤表

任何一个系列都必须输出这五样东西：

- 入口条件
- 子任务顺序
- packet 来源 / 发送顺序 / 回包点
- 中断 / 恢复条件
- 当前未确认项

## 8. 编写模板

以后每补一个系列，建议固定成下面的结构：

- 系列名
- `taskID`
- 适用前置
- 子任务列表
- 实际执行顺序
- 特殊 packet 规则
- 失败恢复策略
- 当前未确认项

## 9. 资源缺口处理

如果某个主线或系列只拿到了 `taskinfo.xml`，但对应 `taskprop` 还没找到，处理顺序必须固定：

1. 先锁 root 和子任务链。
2. 再补 `targetScene`、`country`、`award` 和特殊 AS3 锚点。
3. 再补 `taskprop` 缺失说明。
4. 最后才写自动化代码和 UI 入口。

## 10. 当前结论

- 八卦灵盘已经可以按“阶段化步骤表”编排
- 其他系列不要混在一起分析
- 文档和代码都必须围绕“单系列、按阶段、可恢复”来写

## 11. 分批记录

当前已把第一批系列记录单独拆到 [task-automation-series-batch-01.md](docs/task-automation-series-batch-01.md)。

- 后续每一批只补一个主题块
- 每一批都要保留“已验证”和“待确认”两栏
- 没有 `tasktheme.xml` 的时候，不要硬把 UI 主题名当成执行链名称

## 12. 全量目录入口

- 全量根任务总目录：`docs/task-automation-series-full-catalog.md`
- 第一批阶段化样例：`docs/task-automation-series-batch-01.md`
- 后续新增系列先落总目录，再拆分批次文档

## 13. 主线入口

- 皇城危机主线分析：`docs/task-automation-mainline-2001000-huangcheng-weiji.md`
- 后续主线文档统一沿用同样的“root -> 子任务 -> scene -> UI -> 批处理 -> 待确认”顺序
