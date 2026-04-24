# 皇城危机主线分析与任务区列表框规范

本文只分析 `2001000 皇城危机`，不混入其他主线或支线。它是 family 2 的主线起点之一，适合作为“任务区列表框 + 复选框 + 批量完成 + 已完成隐藏”的第一份规范样板。

## 1. 本次范围

- 根任务：`2001000`
- 名称：`皇城危机`
- type：`2`
- diff：`4`
- 子任务数：`8`
- 约束：当前 root 没有 `before`，`after` 也不参与根内排序，真正顺序按子任务链推进

## 2. 已确认骨架

| subtaskID | 名称 | targetScene | country | award | 验证状态 |
| --- | --- | ---: | ---: | --- | --- |
| 2001001 | 接受使命 | 1003 | 10 | 600 铜钱 / 700 历练 | XML 已验证，动作链已确认，packet 待确认 |
| 2001002 | 奇怪的声音 | 2003 | 20 | 700 铜钱 / 800 历练 | XML 已验证，动作链已确认，packet 待确认 |
| 2001003 | 歪打正着揭皇榜 | 2001 | 20 | 800 铜钱 / 1000 历练 | XML 已验证，动作链已确认，packet 待确认 |
| 2001004 | 唐太宗的回忆 | 2002 | 20 | 800 铜钱 / 800 历练 | XML 已验证，动作链已确认，packet 待确认 |
| 2001005 | 危机重重 | 2003 | 20 | 900 铜钱 / 1000 历练 | XML 已验证，动作链已确认，`200100504` 特殊提示待补 |
| 2001006 | 传说中的唐三藏 | 2003 | 20 | 800 铜钱 / 1200 历练 | XML 已验证，动作链已确认，packet 待确认 |
| 2001007 | 你逃不掉的！ | 2005 | 20 | 900 铜钱 / 1200 历练 | XML 已验证，动作链已确认，packet 待确认 |
| 2001008 | 神仙的指示 | 2005 | 20 | 700 铜钱 / 1000 历练 / 100 修为 / 50 感恩 | XML 已验证，动作链已确认，`gratitude` 待补展示 |

## 3. 主线顺序

1. `接受使命`
2. `奇怪的声音`
3. `歪打正着揭皇榜`
4. `唐太宗的回忆`
5. `危机重重`
6. `传说中的唐三藏`
7. `你逃不掉的！`
8. `神仙的指示`

这一条链的核心特征是单线推进，不是分支树。`describe` 负责故事文本，`targets` 负责目标场景，`country` 负责区域锚点，`award` 负责奖励输出。

## 4.1 已确认动作链

来源：`data/taskinfo.xml`、`TaskView` / `TaskDialog` / `TaskControl`、`data/npc.xml`、`data/map.xml`，以及 4399 攻略页 `https://news.4399.com/kabuxiyou/gonglve/201102-17-86748.html`。
这里确认的是“去哪里、点什么、先后顺序”，不是最终 packet recipe。

| subtaskID | 已确认动作 | 场景对象 / 锚点 | 备注 |
| --- | --- | --- | --- |
| 2001001 | 在驾驶舱大叔或任务档案接取任务 | 驾驶舱 / 机舱小助手(12005) | 起始报名 |
| 2001002 | 打开大地图到大唐，再去双叉岭 | 双叉岭 / 大地图 | 第一次下沉 |
| 2001003 | 到长安城，点城门边的皇榜 | 长安城 / 皇榜 | 进入大唐主线 |
| 2001004 | 进皇宫，和唐太宗对话 | 唐太宗 | 皇城核心 |
| 2001005 | 回双叉岭，点树干上的叉子，连点按钮拔叉子，再用快捷栏第一个法术“粒子光线”打发光区域 | 大树(21101)、叉子(21103/21110)、发光结界(21108)、发光粉末(21112) | 对应 `200100504` 特殊提示节点 |
| 2001006 | 点击树上被救下的人并对话，完成救援收口 | 唐三藏(21105/21111) | 救援段 |
| 2001007 | 从双叉岭左侧去五指山顶，点石头、站上去、拉顶部藤条，再找虎魔王 | 五指山顶 / 师徒石 / 虎魔王(21502) | 地形互动 |
| 2001008 | 跟五指山顶的神秘人对话 | 神秘人(21514) | 收尾 |

补充：

- `2001005` 的 `200100504` 是当前已确认的硬编码特殊提示点，说明这一段不是纯文本对话。
- `2001007` 的石头对应 `map.xml` 里的 `五指山顶` 场景服务 `师徒石`，`npc.xml` 里还能对上 `虎魔王(21502)` 和 `神秘人(21514)`。

## 4. 已验证经验

- 第一段在 `country=10`，对应报名/起始场景。
- 后续 7 段全部在 `country=20`，说明主线很快切入大唐区域。
- `targets` 目前已经能读出场景数字，分别是 `1003 / 2003 / 2001 / 2002 / 2003 / 2003 / 2005 / 2005`。
- `TaskInfoXMLParser.parseTargetScene()` 正好读取 `xml.targets.target`，所以这条主线可以直接用 `targetScene` 做自动化锚点。
- 最后一步带 `cultive=100` 和 `gratitude=50`，奖励字段比前几步更完整，列表框展示时不能只抬 `coin/exp`。

## 5. 分析流程

1. 先锁 root，只看 `2001000`。
2. 再抽子任务表，不看 UI 名字。
3. 再把 `targets`、`country`、`award` 三列补齐。
4. 再回 AS3 确认运行时解释，重点是 `TaskInfoXMLParser`、`TaskList`、`TaskView`、`TaskControl`。
5. 再补 packet 和场景切换顺序。
6. 最后才写任务区列表框，不要反过来用 UI 猜执行序列。

### 5.1 当前封包边界

- `QueryTaskZoneUserTaskListProgress(2001000)` 只做进度查询，不会推动子任务执行。
- `StartHuangchengWeijiTaskAsync()` 目前只同步列表和状态提示，不是完整的步骤执行器。
- 现阶段这条 native 流程里还没有调用 `SendTaskZoneTalkPacket`、`SendTaskZoneClickPacket`、`SendEnterScenePacket` 这种步级发送器。
- `taskprop_2001000.xml` 当前缺失，所以每个子任务的 `dialogId`、点击/战斗/收口封包序列还不能定稿。
- 这份文档现在的角色是把骨架、列表框和缺口说明清楚，等 `taskprop` 补齐后再升级成完整的 `来源 -> 发送 -> 回包 -> 恢复点` 说明。

### 5.2 运行时链路

皇城的实际执行链不是从 `taskinfo.xml` 直接读出来的，而是走这条链：

1. 点击 NPC / 任务入口后，`TaskControl.getTaskDialog()` 先发 `OP_CLIENT_CLICK_NPC`。
2. 服务端回 `dialogId` 后，`TaskView.onGetDialogBack()` 先判断是否是短路分支。
3. `dialogId == 0` 时只会重新发 `GET_TASK_DIALOG`，`dialogId == npcid` 时直接完成，不会加载 `taskprop`。
4. 只有真正的步骤型节点才会走 `PropertyPool.getTaskProps(dialogId)`，也就是 `assets/taskprop/<dialogId/1000>0.xml`。
5. `TaskXMLParser.parseXML()` 把 `desc` / `choose` / `battle` / `flash` / `otherpopup` / `targetScene` 翻成运行时对象。
6. `TaskDialog` 按这些节点决定是继续对白、触发战斗、切场景，还是先弹提示再收口。
7. `TaskDialog.onDialogComplete()` 再把 `dialogId` 和 `chooseId` 回传给 `TaskControl.taskDialogComplete()`，由它真正发送 `TRAIN_INFO_SEND` 和 `TASK_TALK_SEND`。
8. `TaskView.currentDialogID == 200100504` 时，`TaskView` 和 `FaceControl` 都会单独打补丁式提示，这说明皇城里确实有步级特例，不是单纯的任务骨架。

`targetScene` 这层也不是直接等于一个包，它先变成任务事件，再由 `TaskControl.toOtherScene()` 分流。`1002` 会走房间入口，`1013/1018` 走神兽区，其余场景才走普通换图链。

所以这里“缺”的不是链，而是本地缓存里没把 `taskprop_2001000.xml` 带全。

### 5.3 缺口补证流程

这条主线的缺口补证，直接按总规范里的 `5.8` 走，顺序固定如下：

1. 先判定是不是伪缺口。
   - `dialogId == 0` 和 `dialogId == npcid` 这类节点，不单独挂 `taskprop`，不能当成缺文件。
   - `TaskDialog` 解析后如果没有 `describe / flash / battle / otherpopup`，它会自动收口，也不能当成缺步骤。
2. 再锁皇城静态锚点。
   - `2001001 ~ 2001008` 的 `targetScene`、`country`、奖励、场景对象已经定了，先把“去哪里、点什么”写死。
   - `2001005` 用 `200100504` 作为步级特例锚点。
   - `2001007` 用 `师徒石`、`藤条`、`虎魔王` 作为场景交互锚点。
3. 再拿运行时回包补 `dialogId` / `chooseId`。
   - 现场看 `TaskView.onGetDialogBack()` 的 `dialogId`。
   - 现场看 `TaskDialog.dialogComplete()` 里带出来的 `sceneId` 和 `needchoose`。
   - 有 `choose` 的节点，先记 `sendChooseId`，不要直接猜成普通对白。
4. 再补 packet。
   - 对话段补 `TASK_TALK_SEND`，有选择时先补 `TRAIN_INFO_SEND`。
   - 场景段补 `ENTER_SCENE_SEND` 或对应的分流包。
   - 战斗段补 `STARTBATTLE` / 战斗回包。
5. 最后定稿。
   - 每一步都写成 `来源 -> 发送 -> 回包 -> 恢复点`。
   - 只要四项里有一项没闭合，就继续留在“待确认”，不要提前写成最终实现。

## 6. 任务区列表框规范

这个功能要放在任务区，不要混到普通活动页里。实现上可以参考 `EightTrigrams` 的任务区模态生命周期，也可以参考 `TaskAcceptMachine` 的列表弹窗骨架，但不能直接复用它的单选接取逻辑。

### 6.1 列表来源

- 优先从 `TaskList.getSubtaskList(2001000)` 读取运行时链。
- 如果运行时任务列表尚未初始化，再回 `data/taskinfo.xml` 取根节点子任务。
- 排序必须固定按 `subtaskID` 升序，不按 UI 渲染顺序。

### 6.2 过滤规则

- `TaskList.getStateOfSpecifiedSubtask(subtaskID) == 2` 的任务直接隐藏，不渲染。
- 已完成任务不显示，不是禁用态。
- 未完成任务可显示复选框。
- 如果某个步骤被前置卡住，要保留为“待处理”状态，但不能冒充已完成。

### 6.3 行组件

每一行建议包含以下信息：

- 复选框
- 子任务名
- targetScene
- 奖励摘要
- 当前状态标签

如果后续要做得更轻，可以只保留复选框和子任务名，但 `targetScene` 至少要能在详情里查到。

### 6.4 按钮行为

- “完成所选”按钮只处理当前勾选项。
- 执行顺序仍然要按 `subtaskID` 升序，不允许并行跳步。
- 每完成一步就立刻刷新列表，并重新过滤已完成项。
- 如果某一步已经在刷新前变成完成态，要自动跳过。
- 如果某一步被前置阻塞，要停止批处理并提示原因。

### 6.5 禁止项

- 不要只改 `TaskList` 内存数组当成完成。
- 不要把 `makeTaskComplete()` 当成主线批处理的唯一入口，它现在不是通用批量引擎。
- 不要把 `tasktheme.xml` 或 UI 排序当成真实执行顺序。
- 不要把已完成任务留在列表里再靠颜色区分。

## 7. 代码落点

- 数据入口：`TaskInfoXMLParser.as`
- 状态入口：`TaskList.as`
- 任务对话：`TaskView.as`
- 完成回写：`TaskControl.as`
- 任务区弹窗骨架：`TaskAcceptMachine.as`
- 行组件骨架：`TaskMachineItem.as`
- 任务区模态样板：`EightTrigrams.as`

如果后续要把这个列表框接到本项目的 UI 桥接层，原则也一样：只传任务列表、选中项和完成结果，不直接把状态逻辑散落到多个层里。

## 8. 待确认

- 每个子任务的 dialogId 和 packet 序列。
- `taskprop_2001000.xml` 当前不在本地缓存中，必须先补齐这份运行时对话 XML，不能直接按 `taskinfo.xml` 猜 packet。
- `gratitude` 是否要在列表框里展示成单独奖励项。
- 批处理按钮是否允许自动接取未接任务，还是只处理已接任务。
- 任务区是否需要“显示已完成”开关，默认应为关闭。
- 是否需要把当前可执行步骤和已完成总数分开显示。

## 9. 经验流程

以后补主线文档，统一按这条流程写：

1. 先锁 root。
2. 再列子任务。
3. 再抽 `targetScene / country / award`。
4. 再读 AS3 的运行时解释。
5. 再补列表框和批处理规则。
6. 最后补已验证与待确认。

## 10. 结论

- `皇城危机` 可以作为主线列表框的第一份模板。
- 它是单线顺序任务，不适合照搬八卦灵盘那种阶段卡点逻辑。
- 列表框必须默认隐藏已完成任务，并且按序串行完成勾选项。

## 11. 为什么没有对应 XML

这里缺的不是 `taskinfo.xml`，而是运行时对话层的 `taskprop_2001000.xml`。

- `TaskArchivesVersion3.swf` 只是档案展示层，不承载任务执行数据。
- `TaskInfoXMLParser` 读的是 `data/taskinfo.xml`，只能确认骨架、子任务、目标场景和奖励。
- `PropertyPool.getTaskProps(dialogId)` 才会去取 `assets/taskprop/<dialogId/1000>0.xml`。
- 这意味着 `2001000` 理论上应对应 `assets/taskprop/2001000.xml`，但当前本地缓存没有这份文件。
- `TaskView.onGetDialogBack()` 的短路分支和 `TaskDialog` 的空节点自动收口，都会让一部分动作锚点不再单独暴露成 `taskprop`。
- 所以现在能确认的是“皇城危机存在且骨架完整，动作链也已补齐”，但完整 dialog/packet 序列仍不能直接定稿。
- 这不是说链不存在，而是本地仓库快照里没有那份运行时 XML。我们检查过明文目录和 `data_data.zip`，都没有 `taskprop_2001000.xml`。

## 12. 代码实现约束

- 任务区 UI 必须按列表框实现，默认隐藏已完成项。
- 批量执行只能按 `subtaskID` 升序串行处理，不能并行跳步。
- 任务完成状态必须来自运行时回包，不能只改本地数组。
- 在 `taskprop_2001000.xml` 缺失前，不要把未验证的 dialogId 当成最终实现。
- 代码先落“任务列表 + 进度刷新 + 选择框”，再补完整 packet 流程和恢复点。
