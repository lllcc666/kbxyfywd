# 任务自动化分析与编排规范

## 1. 目标

这份文档只做一件事：把“任务自动化”拆成可逐步推进、可验证、可扩展的流程。

- 先分析单个系列，再扩展到下一个系列
- 先确认 XML 骨架，再确认运行时节点和回包，最后才落到自动化步骤
- 这份文档的最终产物不是“分析框架”，而是“可执行的 packet recipe”：每一步都要写清封包从哪来、怎么发、等哪个回包、从哪里恢复
- 现在只以 `八卦灵盘` 为样例，后续系列按同一模板追加

## 2. 代码来源优先级

自动化任务的真实依据，按优先级从高到低是：

1. `data/taskinfo.xml`
2. `config/tasktheme.xml`（运行时由 `SortedTaskList` 加载；当前仓库快照里未直接存放该文件）
3. 反编译 AS3：
   - `TaskInfoXMLParser.as`
   - `TaskXMLParser.as`
   - `TaskList.as`
   - `SortedTaskList.as`
   - `TaskView.as`
   - `TaskControl.as`
4. 本地自动化实现：
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
- `TaskDialog` 里的 `choose` 节点先转成 `sendChooseId`，最后由 `TaskControl.taskDialogComplete()` 统一补 `TRAIN_INFO_SEND` + `TASK_TALK_SEND`，不是在 UI 层直接发包。
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
   - 先按 XML 原始顺序理解剧情
   - 再按运行时代码、抓包和当前 hook 的实现修正实际执行顺序
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

- `choose flag="3"` 时，必须先发 `TRAIN_INFO_SEND`，再发 `TASK_TALK_SEND`
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

### 6.2 实际执行顺序

`wpe_hook.cpp` 里已经把八卦灵盘写成固定步骤表，执行顺序如下：

| 阶段 | XML 子任务 | 实际执行顺序 | 说明 |
|---|---:|---|---|
| 1 | 4006001 | 400600101 -> 400600103 -> 400600102 -> 400600104 | 起始对话 + 采集 + 拒绝分支 |
| 2 | 4006002 | 400600201 -> 400600203 -> 400600204 -> 400600202 | 墨坤 / 夺灵丹 / 火炎 / 墨坎 |
| 3 | 4006003 | 400600301 -> 400600303 -> 400600302 | 墨坎 / 菩提子 / 墨离 |
| 4 | 4006004 | 400600401 -> 400600403 -> 400600402 | 墨离 / 黑河水 / 墨震 |
| 5 | 4006005 | 400600501 -> 400600506 -> 400600507 -> 400600505 | 墨震 / 水魂战斗 / 收口 / 墨艮 |
| 6 | 4006006 | 400600601 -> 400600603 -> 400600602 | 墨艮 / 火铜 / 墨巽 |
| 7 | 4006007 | 400600701 -> 400600703 -> 400600702 -> 400600704 | 墨巽 / 柿果 / 墨兑 / 命运大转盘 |
| 8 | 4006008 | 400600801 -> 400600803 -> 400600804 -> 400600805/806 -> 400600802 | 墨兑 / 墨魂对话 / 墨魂战斗 / 胜败分支 / 收尾 |

### 6.3 八卦灵盘的特殊规则

- `chooseId = 3` 的节点，先发 `TRAIN_INFO_SEND` 再发 `TASK_TALK_SEND`
- `400600704` 是特殊转盘收口，不按普通 NPC 对话处理
- `400600303` 的菩提子获取是特殊场景点击，不是普通谈话
- `400600506` 的水魂战斗按实际封包走战斗流程，不是简单点 NPC
- 自动化必须先确认自动战斗开启，否则直接失败
- 任务恢复点由 `g_eightTrigramsResumeStepIndex` 维护，不能从头假设

### 6.4 封包闭环样式

八卦灵盘里，当前实现已经把“步骤 -> 发包 -> 回包 -> 恢复点”写成了固定流程。可以直接按下面理解：

| 代表节点 | 当前实现中的发包动作 | 备注 |
| --- | --- | --- |
| `400600101` / `chooseId=3` | `TRAIN_INFO_SEND(7, [400600101, 3])` -> `TASK_TALK_SEND(21210, 400600101)` | 选择分支，先训练信息后对话 |
| `400600103` / `400600203` / `400600403` / `400600603` / `400600703` / `400600803` | `CLICK_NPC` -> `TASK_TALK_SEND` | 先点 NPC，再发任务对话 |
| `400600303` | `CLICK_NPC(41312)` | 场景内特殊点击，参数不是表面 NPC id |
| `400600506` | `SendBattlePacket(0x17, 0x10, 0x08, 1)` -> `BATTLE_READY` | 抓包确认的特殊战斗包 |
| `400600704` | `TASK_TALK_SEND(1111111)` | 结果收口，不按普通对话处理，且不强制匹配 NPC |

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
