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

## 3. native 引擎的偏差与已做修复

`src/hook/wpe_hook.cpp` 主线区块（`StartStoryTaskAsync` / `ReplyStoryDialogNode` / `RunStorySubtask` / `StoryTaskOn*Response`）。

| # | 偏差 | 处理 |
| --- | --- | --- |
| 1 | 没有接任务能力（`ACCEPT_TASK` 全仓库不存在） | 已加 `Opcode::ACCEPT_TASK_SEND=1184774`；`StoryTaskOnNpcTaskListResponse` 解析 `type==1` 存 `pendingAcceptSubtaskIds`；`RunStorySubtask` 点完入口 NPC 后 2.5s 窗口内补发并重贴 NPC。**真机仍接不上，见 §4** |
| 2 | `KIND_CHOOSE` 拿 taskprop 的 `<choose flag>` 当 chooseId 发 TRAIN_INFO | 已删除该分支，统一只回 `TASK_TALK` |
| 3 | 未登记 dialogId / alert 节点一律回 TASK_TALK | `KIND_ALERT` 改为不发包；未登记节点仍回（保流程） |
| 4 | 配方 8 处 scene 与 entryNpc 的 `npc.xml mapid` 不一致 | 生成器改用 `mapid` 校验（npc id 不跨地图重复，可靠）；降到 4 处，剩余打印警告 |

配方生成：`scripts/gen_story_recipe.py` → `include/activities/story_task_recipe.h`（365 节点 / 57 子任务 / 11 组）。
`Node::acceptFlag` 与 `pick_accept_flag()` 已成死字段，已删。

## 4. 未解决 / 待确认

**皇城危机第一个子任务（2001001 接受使命）真机仍接不上。** 这是当前首要问题，已排除：
- 详情页/按钮不存在（native 已补发 `ACCEPT_TASK`）
- 场景进不去（2001001 场景 1003，NPC 11001 阳光机长在 1003，匹配）

待验证方向（按可能性排序）：
1. `ACCEPT_TASK` 的 params 到底该是 **subtaskId** 还是 **taskId**。当前按 `TaskAcceptMachine.acceptId = taskId(=子任务号)` 推断为 subtaskId，**没有抓包实测**。若错，改成 `2001000` 试。
2. NPCTASKLIST 的 `type/taskcount` 偏移是否读对。需抓一份真实 2001001 的 `1315858` body 逐字节对照（`TaskParse.as:498` 那段）。
3. 2001001 是否真的走 `type==1` 分支。它也可能是 `dialogId != 0` 直接进对话（即任务早已被自动接下但没显示）。需打印实际收到的 `dialogId/type/taskcount`。
4. 接任务后是否必须**再次点击 NPC**才下发对话。当前实现重贴了一次，未验证必要性与时机（Sleep 是否够）。

其余 4 处场景错配（2001005 / 2004004 / 2011001 / 2011002）：`taskinfo` 的 targets 与对话 NPC 所在图自相矛盾，需进图实测判定谁对。

## 5. 相关文档

- `docs/task-automation-analysis-guide.md`：任务自动化通用分析流程
- `docs/task-automation-series-full-catalog.md`：全部根任务目录
- `docs/task-automation-mainline-2001000-huangcheng-weiji.md`：皇城危机骨架
