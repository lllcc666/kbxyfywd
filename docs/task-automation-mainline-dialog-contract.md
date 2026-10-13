# 主线任务（皇城危机 / 魔晶战记）对话链契约与引擎偏差

本文只记录**已用 AS3 行号核实**的契约，以及 native 引擎（`src/hook/wpe_hook.cpp` 主线区块）的实际偏差。
未验证的推断全部标 `待确认`，不要当结论用。

代码位置：`swf_cache/decompiled/MainGame_remote/scripts/com/game/`

## 1. 核心结论

主线任务走**普通任务窗口**，是「客户端本地推进 + 服务端驱动」的混合，**不是**「服务端推一个 dialogId、客户端回一句」的纯问答。

- 一条 `dialog` 节点里的多句 `desc` 是**客户端本地逐句点完**，末尾才发一次 `TASK_TALK`。
- `alert` 提示节点**不发包**。
- 未接的任务必须先发 `ACCEPT_TASK`，否则停在接任务窗口，后续对话永不出现。

## 2. 契约（AS3 行号依据）

### 2.1 点击 NPC → 拿首个 dialogId

- `modules/control/MapControl.as:1578` `onClickNpc()` → `TaskControl.getTaskDialog()`
- `modules/control/task/TaskControl.as:979` `getTaskDialog()`：发 `CLICK_NPC.send(1186048)`，params=npcid，body=[0]（或 sid）。
- `OP_CLIENT_CLICK_NPC` 的 `back=0`（`locators/MsgDoc.as:279`），点击本身**没有可等待的回包**。

### 2.2 NPCTASKLIST_BACK（1315858）三条分支

`modules/control/task/TaskControl.as:876` `onNPCTaskListBack()`：

| 条件 | 行为 |
| --- | --- |
| `params.type == 0` | `onLoadPropsList` → 道具/任务列表，另走 |
| `params.type == 1` | 派发 `OPENTASKACCEPTCLIP` → **接任务窗口**，玩家点接受才发 `ACCEPT_TASK` |
| 其它 | 直接给 `result.dialogId` |
| `params.specialType < 0` | `result.chooseId = params.specialType`（负数→**才**需要 TRAIN_INFO） |

字段顺序（`modules/parse/TaskParse.as:498` `onReqNPCTaskList`）：
`dialogId, [type, taskcount, (taskcount==-1 时再读一个真正的 count), tasklist..., canReceive, taskId]`。
`type==1` 时 tasklist 里是 **subtaskId 列表**（`taskcount` 个 int）。

### 2.3 接任务

- `modules/util/TaskAcceptMachine.as:127` `onClickAccept()` → 派发 `ONACCEPTTASKBYTASKMACHINE`，携带 `acceptId`
- `TaskAcceptMachine.as:95` `onClickItem()`：`acceptId = taskId`，而 `taskId` 来自 `param.list[i]`（**子任务号**）
- `modules/control/task/TaskControl.as:1092` `onAcceptTask()`：
  `sendMessage(OP_CLIENT_REQ_ACCEPTTASK.send=1184774, evt.body)` —— **params = 子任务号，无 body**
- `TaskParse.as:589` `onAcceptTaskBack()`：`params=taskId`，body 首个 int `==1` 表示成功

### 2.4 dialogId → 回应（关键分支）

`modules/view/task/TaskView.as:88` `onGetDialogBack()`：

| dialogId | 行为 |
| --- | --- |
| `== 0` 且有 npcid | 改发 `GET_TASK_DIALOG`（等于再点一次 NPC），**不回 TASK_TALK** |
| `== npcid` | 直接 `onDialogComplete(dialogId)` → **会**回 `TASK_TALK` |
| 其它 | 读 `taskprop` 弹窗，本地点完 desc 后回 `TASK_TALK` |

`TaskView.as:107` `onGetPropsBack()`：`xml.children().length() == 0`（空 `dialog` 节点）→ 直接 `onDialogComplete`。

`modules/view/task/TaskDialog.as:933` `onClickPersonSay(flag,...)`：
- `flag` 是**按钮编号，不是发包参数**。
- `case 1` → `onAccept(false)`（本地继续）；`case 2` → `onNotAccept()`（关窗）。
- `default` → `needSendChoose != 0` 时 `sendChooseId = flag - 8`。
- `needSendChoose` 来自 `setData(...,needchoose)`，而 `needchoose` 来自服务端回包的 `chooseId`；主线恒为 0。

`modules/control/task/TaskControl.as:1047` `taskDialogComplete()`：
```
if (chooseid != 0) sendMessage(TRAIN_INFO.send=1184772, 7, [dialogid, chooseid]);
sendMessage(OP_CLIENT_REQ_TASKTALK.send=1184788, npcid, [dialogid]);
```
即 **TRAIN_INFO 只在服务端给了 chooseId 时才发**。

### 2.5 alert 提示节点不发包

- `modules/view/task/TaskView.as:207` `showTaskAlert()` → `TaskAlert.show`（`TaskAlert.as:14`）
- `TaskView.as:237` `tipsNeedSendPacket()` 是关闭回调：只有 `needToChangeMouse.sendback` 有值（`flag != 0` 或 `type == 26`）才发（`sendback==1` → `onDialogComplete`；`==-1` → `DIALOGFINISHED`）。
- `modules/view/task/TaskTips.as:119` `dispos()`：无条件 `callback.apply(null,[])`，但**普通 alert 的 callback 不在该分支**。实测全部 110 个 alert 节点**没有一个带 `flag`**，所以 alert 一律不发包。

### 2.6 TASK_TALK_BACK 字段（`TaskParse.as:172`）

| params | body 布局 |
| --- | --- |
| 1 / 5 / 22 | `npcid, itemCount, (type,id,count)*, dialogId` |
| 21 | `npcid, count, (type,id)*, dialogId`（每项 **2** 个 int） |
| 2 / 14 / 17 | `dialogId, [account,(type,id,count)*, flag]` |
| 3 | `dialogId, goodId` |
| 7 | `boardId, dialogId, npcid` |
| 8 | `[dialogId, abilityNum, ...]` |
| 10 | `dialogOne, dialogTwo, npcid` |
| 11 | `id, currentId, trigram, exp, npcid, itemid, medal` |
| 15 | `coin, exp, cultivate, itemCount, ...`（**无 dialogId**） |
| 26 | `dialogId, coin, exp, cultivate, itemCount, ...`（**首个 int 是 dialogId**） |
| 4 / 6 / 9 / 12 / 13 / 16 / 18 / 19 / 20 / 23 / 24 / 100 | 无 dialogId |

`TaskParse.as:133` `onReqTaskFinishedBack()`：`subtaskID = params`（不是 body），body 只是可选奖励。

`TaskControl.as:738` `onDialogFinished()`：`diaComGoodPara != null`（type 1/5/26 带奖励）→ `getAwardWithoutComplete`，奖励提示关掉后才 `onDialogComplete`。

### 2.7 场景切换

- `TaskControl.as:1123` `toOtherScene()`：`1002`→`REQ_ENTER_ROOM`；`1013/1018`→`ENTERSHENSHOU`；其它→`SENDUSERTOOTHERSCENE`
- `MapControl.as:1211` `sendUserToOtherScene()` → `ENTERSCENE` → `MapControl.as:1330` `onEnterScene()` → `changeScene()` → `MapView.loadMap()` → `MapControl.as:174` `onMapLoaded()` 发 `OP_CLIENT_REQ_ENTER_SCENE.send(1184313)`，**params = mapId**。

### 2.8 战斗 sid

`manager/MonsterManger.as:197`：
- `combine1(1,0,id,level)` = `type<<28 | val<<22 | id<<8 | level`（`battle.id < 10000`）
- `combine2(2,id)` = `type<<28 | id`（其它）
- `battle.id == -1` → 用当前 NPC id；`useID != 0` 时 sid 走 `combine2(10, id)`

### 2.9 点击 NPC 的第三态：任务列表窗口（此前遗漏，是首要根因）

`NPCTASKLIST_BACK` 的 `type` 决定客户端弹哪种窗口，**两种窗口都不回包**，回包只发生在玩家点了条目之后：

| type | 派发 | 窗口类 | 玩家动作 → 发包 |
| --- | --- | --- | --- |
| 1 | `OPENTASKACCEPTCLIP`（`TaskControl.as:909`） | `TaskAcceptMachine` | 点「接受」→ `ACCEPT_TASK(subtaskId)` |
| 0 | `ADDTASKINTOTASKLISTITEMS`（`TaskControl.as:923`） | `TaskListItems` | 点任务条目 → `OPENDIALOG` → 对话 → `TASK_TALK(npcid,[dialogId])` |

type==0 的完整链（行号依据）：

- `TaskControl.as:898-903`：`params.type == 0 && taskcount > 0` → `onLoadPropsList(params,result)` 并 `return`（**不进后面的默认分支**）
- `TaskControl.as:926-936` `onLoadPropsList` → `PropertyPool.getTaskPropsList(各条 dialogId)` → `loadPropsListBack`
- `TaskControl.as:951` `loadPropsListBack`：对每个 `tasklist[i]` 用**该条目自己的 `dialogId`** 取 taskprop，`TaskXMLParser.parseXML(xml, tasklist[i].subtaskID, tasklist[i].taskID, tasklist[i].dialogId)`，成功才 push 进 `result.tasklist`
- `TaskControl.as:965` `dispatch(ADDTASKINTOTASKLISTITEMS, result)` → `WindowLayerControl.as:389` `onAddTaskIntoTaskListItems` → `:410` `TaskListItems.addTask(event.body)`
- `TaskListItems.as:373` `printComplete`：为每条 `data.tasklist[i]` 建 `TaskListItem(1, tasklist[i].data, opNPCList, tasklist[i].state)`（`:395`）；末尾再挂一条 `addTask(0, dialogTwo, dispos)`（`:471`）即「离开」
- `TaskListItem.as:114-125` `onClick` type==1 → `callback.apply(null,[this.obj])` → `TaskListItems.opNPCList:478` → `TaskUtils.dispatchEvent(TaskEvent.OPENDIALOG,{task, needchoose:needSendChoose})` + `dispos(true)`
- `TaskControl.as:112` 监听 `OPENDIALOG` → `openDialog:1151`；若该 Task 的 `describe/flash/battle/otherpopup` **全空**，直接 `dispatch(MASTERISOUTSPECIALAREA,{npcid,dialogID,chooseid})`（`:1156`）→ `taskDialogComplete`
- 否则进 `TaskDialog`；`onClickPersonSay` default → `onAccept(true)` → `dialogComplete` → `DIALOGFINISHED` → `TaskControl.onDialogFinished:738` → `view.onDialogComplete` → `COM_TASK_DIALOG` → `taskDialogComplete:1047` → `TASK_TALK(npcid,[dialogId])`

**关键结论：`NPCTASKLIST` body 的第一个 int（`params.dialogId`）在 type==0 分支里不是「可直接回包的对话」**，它只是该 NPC 的默认对白标识；真正对应子任务的节点在 `tasklist` 每一条自己的 `dialogId` 里（`loadPropsListBack:951` 用它取 taskprop）。

**native 现有实现的错**：`StoryTaskOnNpcTaskListResponse`（`wpe_hook.cpp:~12480`）只解析了 `type==1` 的子任务号列表，对 `type==0` 完全没读 `tasklist`，却仍把 body[0] 当 `dialogId` 存进 `pendingDialogId` 并回 `TASK_TALK`。正确的做法是：type==0 时遍历 `tasklist`，取属于本子任务（`subtaskID = dialogId/100`）的那条 `dialogId`，用它回 `TASK_TALK`；body[0] 不能直接用。

### 2.10 接取门槛 `canReceive`

`TaskParse.as:544` 的 `canReceive` 与 `:545` 的 `taskId` 都在 `type==1` 分支里。`TaskAcceptMachine.onClickAccept:127` 只在 `canReceive == 1` 时才派发 `ONACCEPTTASKBYTASKMACHINE`；否则只弹「您已经接受了任务…还没有完成哦」并关窗，**不发 `ACCEPT_TASK`**。

接取成功后客户端**不做任何事**：`TaskModel.as:112` `onAcceptTaskBack` 只有 `O.traceSocket`，没有刷新、没有自动点 NPC。所以「接完再点一次 NPC」是 AS3 的真实行为，native 补这一次点击是对的。

### 2.11 主线系列的强制先后顺序

`after` 不是任务号而是锚点，经 `TaskInfoXMLParser.getPrefixListByAfterID:466` 映射：

- `after="101"` → `type = 101 % 100 % 10 = 1` → `2000000 + (101 % 100) * 1000` = **2001000**
- `after="102"` → **2002000**，`after="103"` → **2003000**，依此类推

`canInputOrNot:186` 要求被映射到的组状态 `== 2`（已完成），否则该组不可见、不可接。因此主线是**严格串行**：皇城危机(2001000) → 山崩地裂(2002000) → 龙战于野(2003000) → …。「魔晶战记一个都完不成」与「皇城危机第一关接不上」是同一个根因的两种表现，必须先修好 2001000。

### 2.12 子任务没有入口 NPC

配方 `SUBTASKS` 里 `2001002`（奇怪的声音，场景 2003）与 `2004002`（通过大裂口，场景 2102）的 `entryNpc == 0`。

根因：`gen_story_recipe.py` 的 `parse_taskprop` 只从 `<desc id="N">` 里取**正数** NPC（`npcs = [x for x in npc_ids if x > 0]`）。这两个子任务的节点里 `desc` 的 id 全是 `-1`（自己）或 `0`（选项），没有说话 NPC：
- `200100201`：`desc id` = `-1,0,-1,0`（flash + 自白，无 NPC）
- `200400201`：`desc id` = `-1,0,-1,0`；`200400202` 是 `alert`（无 desc）

这两个子任务是**场景触发型**：进图后由场景 AI（`SpecialArea.sendMsg` `modules/view/person/SpecialArea.as:68`）派发 `MASTERISINSPECIALAREA` 自动开对话，不是点某个固定 NPC。native 现状（`entryNpc==0` 时跳过点击、直接等服务端推 dialogId）对这条路是**合理的**，不是必然失败。是否真的会推，需真机进图后在诊断回显里看有没有 `TASK_TALK_BACK`。

**是否要额外补一次点击，属待确认**：若要补，依据是 `npc.xml` 的 `<istask>1</istask>`（`Npc.as:186` 用 `istask == 1` 标记可点任务 NPC），而不是猜某个 id。

## 3. native 引擎的偏差

`src/hook/wpe_hook.cpp` 主线区块（`StartStoryTaskAsync` / `ReplyStoryDialogNode` / `RunStorySubtask` / `StoryTaskOn*Response`）。

| # | 偏差 | 状态 |
| --- | --- | --- |
| 1 | 没有接任务能力（`ACCEPT_TASK` 全仓库不存在） | 已加 `Opcode::ACCEPT_TASK_SEND=1184774`；`type==1` 时存 `pendingAcceptSubtaskIds`；点完入口 NPC 后 2.5s 窗口内补发并重贴 NPC |
| 2 | `KIND_CHOOSE` 拿 taskprop 的 `<choose flag>` 当 chooseId 发 TRAIN_INFO | 已删除该分支，统一只回 `TASK_TALK`（与 2.4 一致，正确） |
| 3 | 未登记 dialogId / alert 节点一律回 TASK_TALK | `KIND_ALERT` 改为不发包；未登记节点仍回（保流程） |
| 4 | 配方 8 处 scene 与 entryNpc 的 `npc.xml mapid` 不一致 | 生成器改用 `mapid` 校验；降到 4 处，剩余打印警告 |
| **5** | **`NPCTASKLIST type==0` 的 `tasklist` 完全没解析**：`StoryTaskOnNpcTaskListResponse` 只读 body[0] 当 dialogId 就回 `TASK_TALK`，而 AS3 在 type==0 时用 `tasklist[i].dialogId`（见 2.9） | **已修**：完整解析两种 type；type==0 用列表条目 dialogId（优先本子任务、其次同组），type==1 只入接取队列 |
| **6** | **`canReceive` 未参与判断**：AS3 只在 `canReceive==1` 时发 `ACCEPT_TASK` | **已修**：读 `type==1` 后的 `canReceive`，不为 1 则清空接取队列 |
| 7 | 配方有 2 个子任务 `entryNpc==0`（2001002 / 2004002），native 跳过点击 | 研判为**场景触发型**，现状路径合理，见 2.12；待真机确认 |

配方生成：`scripts/gen_story_recipe.py` → `include/activities/story_task_recipe.h`（365 节点 / 57 子任务 / 11 组）。
`Node::acceptFlag` 与 `pick_accept_flag()` 已成死字段，已删。

### 3.1 本轮落地（`src/hook/wpe_hook.cpp` `StoryTaskOnNpcTaskListResponse`，编译通过）

1. 完整解析两种 `type`：
   - `type==0`：读 `taskCount` 条 `[dialogId, state]`（`state` 按 AS3 只透传、不过滤），优先挑 `dialogId/100 == 当前子任务`；退一步取同组（`/1000` 相同）中 **dialogId 最小**的那条（AS3 列表按 dialogId 升序）；都取不到才回退 body 首个 int。
   - `type==1`：读 `taskCount` 个子任务号，再读 `canReceive`；不为 1 就清空接取队列（不发 `ACCEPT_TASK`）。为 1 时只保留 `== 当前子任务` 的条目，匹配不到就一条都不接（避免把同一 NPC 的其它任务一起接下来）。
   - 无 `type` 字段 / `taskCount<=0`：沿用 body 首个 int 作默认对白。
2. `pendingAcceptSubtaskIds` 改为按 `type==1` 回包整体替换，避免上一个 NPC 的残留。
3. 诊断回显扩展为打印 `type / count / canReceive / 采用dialogId / headerDialogId / bodyInts[]`，真机一眼可判分支。

只读 review 结论（子代理，未改码）：越界检查、atomic 读取、`Take` 与写锁的配合**均无问题**；`taskcount==-1` 时不漏读，但 AS3 的 `specialType` 语义未保留（主线普通流程无影响）。已按 review 收敛两处：`type==0` 回退由"组内首条"改为"组内 dialogId 最小"，`type==1` 无匹配时不再保留整表。

## 4. 验证方式

改完后必须真机抓一份 2001001 的 `1315858` body，逐字节对照 `TaskParse.as:498` 的读取顺序，确认：

- `type` 到底是 0 还是 1（决定走 2.9 还是 2.10）
- `type==1` 时 `canReceive` 与 `taskId` 的值
- `type==0` 时 `tasklist` 每项的 `dialogId / state`

已排除的方向：
- 场景进不去（2001001 场景 1003，NPC 11001 阳光机长 `mapid=1003`，匹配）
- `ACCEPT_TASK` params 语义错（`TaskAcceptMachine.acceptId = tasklist[i]` = 子任务号，native 语义正确）
- `after` 前置链挡接取（2001000 无 `after`，是链首）

其余 4 处场景错配（2001005 / 2004004 / 2011001 / 2011002）：`taskinfo` 的 targets 与对话 NPC 所在图自相矛盾，需进图实测判定谁对。

## 5. 相关文档

- `docs/task-automation-analysis-guide.md`：任务自动化通用分析流程
- `docs/task-automation-series-full-catalog.md`：全部根任务目录
- `docs/task-automation-mainline-2001000-huangcheng-weiji.md`：皇城危机骨架
