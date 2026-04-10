# 任务自动化分析与编排规范

## 1. 目标

这份文档只做一件事：把“任务自动化”拆成可逐步推进、可验证、可扩展的流程。

- 先分析单个系列，再扩展到下一个系列
- 先确认 XML 结构，再确认 AS3 行为，最后才落到自动化步骤
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
   - 看 `TaskXMLParser` 如何把 XML 转成运行对象
   - 看 `TaskList` 如何判定状态
   - 看 `TaskInfoXMLParser` 如何裁剪可接任务和前置关系
5. 还原执行顺序
   - 先按 XML 原始顺序理解剧情
   - 再按运行时代码和抓包修正实际执行顺序
   - 如果二者冲突，以运行时为准
6. 拆 packet 规则
   - 逐个节点标注发送包、回包、等待条件
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

## 7. 后续系列的分析规范

后续像 `魔晶战记`、`地心传说`、`卡布历险` 这类长系列，统一按下面顺序推进：

1. 先定位系列根任务 `taskID`
2. 再列出所有子任务 `subtaskID`
3. 再从 `taskinfo.xml` 抽出 `targets`、`award`、`after`、`before`、`condition`
4. 再去 `TaskXMLParser` 查单节点类型
5. 再去 AS3 / 抓包确认 packet 顺序
6. 最后才写自动化步骤表

任何一个系列都必须输出这四样东西：

- 入口条件
- 子任务顺序
- packet 规则
- 中断 / 恢复条件

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
