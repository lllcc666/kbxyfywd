# 卡布西游每周活动界面分析文档

## 概述

- **分析日期**: 2026年2月28日
- **SWF文件**: WeeklyActivities.swf
- **数据文件**: weeklyactivity (zlib压缩XML)
- **文件地址**: `http://enter.wanwan4399.com/bin-debug/assets/weekly/WeeklyActivities.swf`

## 使用注意

- `weeklyactivity` 是周活动索引文件。凡是判断“本周 / 最新 / 当前周活动是否存在”，必须先重新下载远程 `http://enter.wanwan4399.com/bin-debug/assets/weekly/weeklyactivity` 并解压，再开始后续分析。
- 本地 `swf_cache/weeklyactivity` 只能作为本次刷新后的缓存或历史对照，不能默认视为最新。
- 正确顺序应为：远程刷新 weeklyactivity -> 确认活动名 / id / url -> 检查本地是否已有目标 SWF 或反编译结果 -> 必要时下载目标 SWF -> FFDec 反编译。

## 一、界面架构分析

### 1.1 核心类结构

| 类名 | 路径 | 功能描述 |
|------|------|----------|
| `WeeklyActivities` | `WeeklyActivities.as` | 主界面控制器 |
| `WeeklyActivityConf` | `view/WeeklyActivityConf.as` | 活动配置加载器 |
| `DemonEntranceAlert` | `view/DemonEntranceAlert.as` | 猿罪之门VIP提示弹窗 |
| `McPlayer` | `view/McPlayer.as` | 动画播放器 |

### 1.2 数据加载流程

```
启动 WeeklyActivities
    ↓
WeeklyActivityConf 初始化
    ↓
URLLoader 加载 "assets/weekly/weeklyactivity"
    ↓
ByteArray.uncompress() 解压 zlib 数据
    ↓
解析 XML 数据到 listObject
    ↓
回调 onLoadComplete()
    ↓
加载 UI SWF: "assets/weekly/weekly_activities_ui.swf"
    ↓
显示活动界面
```

### 1.3 XML 数据结构

```xml
<weekly>
    <items type="0">  <!-- 活动项目列表 -->
        <item>
            <id>活动ID</id>
            <name>活动名称</name>
            <desc>活动描述</desc>
            <award>奖励内容</award>
            <target>目标场景ID/功能ID</target>
            <tipType>提示类型(1=简单提示, 2=详细提示)</tipType>
            <url>SWF资源路径(0表示无)</url>
        </item>
    </items>
    <items type="1">  <!-- 按钮列表 -->
        ...
    </items>
</weekly>
```

### 1.4 活动点击处理逻辑

```actionscript
// 点击活动项后执行 turnINscene(index)
private function turnINscene(index:int):void {
    // 1. 记录点击日志
    WriteLog.instance.write(userId, port, 0, 0, 0, 0, "faceclick" + currentItem.id);
    
    // 2. 判断是否有自定义URL
    if (currentItem.url != "0" && currentItem.url != "") {
        // 加载自定义SWF模块
        ApplicationFacade.getInstance().dispatch(EventConst.BOBSTATECLICK, {"url": url});
    } else {
        // 3. 根据target进入场景
        sceneID = int(currentItem.target);
        if (sceneID > 0) {
            enterScene(sceneID);
        }
    }
}
```

## 二、活动类型判断

### 2.1 tipType 类型

| tipType | 说明 | UI表现 |
|---------|------|--------|
| 1 | 简单提示 | 显示在 itemtips 第1帧 |
| 2 | 详细提示 | 显示在 itemtips2 第2帧 |

### 2.2 target 字段含义

| target值 | 含义 | 示例 |
|----------|------|------|
| 0 | 加载URL SWF | 需配合 url 字段 |
| >0 | 场景ID | 直接进入对应场景 |
| 特殊值 | 特殊功能 | 如 7004=猿罪之门(需VIP) |

## 三、当月活动分析（2026年2月）

### 3.1 2026年2月活动列表

| ID | 活动名称 | 类型 | 奖励 | target |
|----|----------|------|------|--------|
| 26010501 | 营救行动 | 小游戏 | 蒜球球、洗髓丹、乾坤袋等 | 0 (SWF) |
| 26010502 | 冰冻挑战 | 小游戏 | 青衣童女、造化丹、洗髓丹等 | 0 (SWF) |
| 26010503 | 一剑破天·极限挑战 | 挑战 | 钧天圣石、苍羽幻石、日月琉璃等 | 0 (SWF) |
| 26011201 | 花之雨 | 小游戏 | 美美、天地灵气、造化丹等 | 0 (SWF) |
| 26011202 | 躲躲乐无边 | 小游戏 | 石甲牛、小玄牛、攻击速度洗髓丹等 | 0 (SWF) |
| 26011203 | 全新大王金瞳战将 | 场景 | 金瞳战将 | 19011 |
| 26012001 | 飞帖道新春 | 小游戏 | 报喜鹊、乾坤袋、法术速度洗髓丹等 | 0 (SWF) |
| 26012601 | 拯救小伙伴 | 小游戏 | 霜雪小兵、洗髓丹、造化丹等 | 0 (SWF) |
| 26012602 | 新年春饼宴 | 小游戏 | 锦绣鹿、洗髓丹、乾坤袋等 | 0 (SWF) |
| 26020301 | 保卫栋梁之树 | 小游戏 | 橙留香、洗髓丹、乾坤袋等 | 0 (SWF) |
| 26020302 | 果宝特攻恭祝新年 | 签到 | 双系洗髓丹、新年福礼包等 | 0 (SWF) |
| 26020303 | 菠萝吹雪的西游商铺 | 商铺 | 菠萝吹雪、洗髓丹、造化丹等 | 0 (SWF) |
| 26020304 | 守护水果村 | **小游戏** | 陆小果、天地灵气、洗髓丹等 | 0 (SWF) |
| 26020305 | 寻找莲蓬 | 小游戏 | 菠萝头套、香橙头套、通用表情等 | 0 (SWF) |
| 26020601 | 春节花灯盛会 | 场景 | 天焰独角驹、洗髓丹、攻击速度洗髓丹等 | 2014 |
| 26020602 | 三界烟火贺新春 | 小游戏 | 马年纳福套装、造化丹等 | 0 (SWF) |
| 26020603 | 屠苏祝百寿 | 小游戏 | 吉光百寿龟、乾坤袋等 | 0 (SWF) |
| 26020604 | 宝盆纳万财 | 小游戏 | 富甲一方蟾、乾坤袋等 | 0 (SWF) |
| 26020605 | 驱赶好斗年兽 寻莲蓬迎新春 | 小游戏 | 陆小果超进化、圣道剑等 | 0 (SWF) |
| 26022401 | 惊雷大仙超进化 | 超进化 | 惊雷圣石、苍羽幻石、日月琉璃、天地灵气等 | 0 (SWF) |
| 26022402 | 驱傩聚福寿 | 小游戏 | 灯火蝠、天香丸、洗髓丹、乾坤袋等 | 0 (SWF) |

### 3.2 小游戏类型活动判断标准

根据 AS3 代码分析，判断是否为小游戏类型：

1. **包含 GameView 类**: 存在游戏主循环逻辑
2. **包含 MonsterAI/MyPropAI**: 存在怪物AI和道具系统
3. **包含 GameOverView**: 存在游戏结算界面
4. **协议关键字**: `start_game`, `end_game`, `game_event`

## 四、重点活动分析：守护水果村 (Act770)

### 4.1 活动基本信息

| 属性 | 值 |
|------|-----|
| 活动ID | 770 |
| 活动名称 | 守护水果村 |
| 活动描述 | 大量恶灵来袭水果村！快和陆小果一起使用益果因子能量剑净化恶灵吧！ |
| 主要奖励 | 陆小果(妖怪ID:3063)、超进化后(3064)、天地灵气、洗髓丹等 |
| SWF路径 | `assets/activity/djj2026/Act770/Act770Entrance.swf` |

### 4.2 游戏机制

```
游戏开始
    ↓
玩家控制角色发射能量弹
    ↓
击中恶灵 → 恶灵被冻结
    ↓
按空格键击飞冻结的恶灵 → 获得分数
    ↓
恶灵碰撞玩家 → 扣血
    ↓
血量耗尽 或 时间结束 → 游戏结束
    ↓
计算分数 → 发送结算协议
```

### 4.3 协议定义 (ActivityData.as)

| 协议名 | 说明 |
|--------|------|
| `game_info` | 获取游戏信息 |
| `start_game` | 开始游戏 |
| `game_event` | 游戏事件 |
| `relive` | 复活 |
| `end_game` | 结束游戏 |
| `reduce_cd` | 减少冷却 |
| `buy_times` | 购买次数 |
| `exchange_info` | 兑换信息 |
| `exchange` | 兑换奖励 |
| `catch` | 捕捉妖怪 |
| `catch_res` | 捕捉结果 |
| `buy_monster` | 购买妖怪 |
| `get_pass_award` | 获取通关奖励 |
| `buy_gourd` | 购买葫芦 |
| `sweep` | 扫荡 |
| `sweep_info` | 扫荡信息 |

### 4.4 兑换列表

| 名称 | ID | 类型 | 数量 | 价格(勋章) |
|------|-----|------|------|-----------|
| 陆小果 | 3063 | 妖怪 | 1 | 720 |
| 天香丸 | 100034 | 道具 | 1 | 120 |
| 洗髓丹 | 100013 | 道具 | 1 | 250 |
| 天地灵气 | 300028 | 材料 | 4 | 160 |
| 自动战斗加时器 | 100209 | 道具 | 1 | 450 |
| 黄金葫芦 | 100004 | 道具 | 1 | 500 |
| 体力抗性洗髓丹 | 100154 | 道具 | 1 | 580 |

### 4.5 游戏参数

| 参数 | 值 | 说明 |
|------|-----|------|
| passScore | 500 | 通关分数阈值 |
| simpleMaxScore | 1000 | 简单模式最高分 |
| max_score | 1000 | 最高分数 |
| playCount | 2 | 默认游玩次数 |
| _endTime | 91000 | 游戏时长(91秒) |

### 4.6 校验码计算

```actionscript
// 游戏结束时的校验码计算（GameView.as:399-402，已核实）
var uid:uint = uint(GameData.instance.playerData.userId);
var temp1:int = ActOperation.operat(5, uid, 100);   // 5 = 取模
var temp2:int = ActOperation.operat(3, score, ActivityData.instance.checkCode);  // 3 = 乘
var temp3:int = ActOperation.operat(1, ActivityData.instance.checkCode, temp1);  // 1 = 加
var checkcode:int = ActOperation.operat(1, temp2, temp3);
```

`ActOperation.operat` 编码：1=加、2=减、3=乘、4=除、5=取模、6=按位与（`Act770Entrance/scripts/act_2026/Act770/main/ActOperation.as:10`）。
### 4.7 发送协议

```actionscript
// 使用 OP_CLIENT_ACTIVITY_QINGYANG_NEW 协议
sendMessage(MsgDoc.OP_CLIENT_ACTIVITY_QINGYANG_NEW.send, ActivityData.active_id, arr);
// arr[0] = 协议命令 (如 "end_game")
// arr[1] = 校验码
// arr[2] = 分数
```

## 五、其他当月活动分析

### 5.1 菠萝吹雪的西游商铺 (Act774)

| 属性 | 值 |
|------|-----|
| 活动ID | 774 |
| 类型 | 商铺/兑换 |
| 协议 | `open_ui`, `get_bonus`, `buy`, `put_monster`, `evolution` |

### 5.2 驱赶好斗年兽 (Act775)

| 属性 | 值 |
|------|-----|
| 活动ID | 775 |
| 类型 | 综合活动 |
| 协议 | `CMD_OPEN_UI`, `CMD_NPC_TALK`, `CMD_SWEEP_INFO`, `CMD_COMBAT`, `CMD_FIREWORK`, `CMD_EVOLUTION` |

### 5.3 三界烟火贺新春 (Act779)

| 属性 | 值 |
|------|-----|
| 活动ID | 779 |
| 类型 | 收集/兑换 |
| 协议 | `ui_info`, `get_award` |

### 5.4 惊雷大仙超进化 (Act780)

| 属性 | 值 |
|------|-----|
| 活动ID | 780 |
| 类型 | 超进化/战斗 |
| 协议 | `open_ui`, `combat`, `combat_end`, `exchange`, `put_monster`, `evolution`, `sweep` |

### 5.5 寻找莲蓬 (Act720)

| 属性 | 值 |
|------|-----|
| 活动ID | 720 |
| 类型 | 抽奖/任务 |
| 协议 | `ui_info`, `draw`, `stage_award`, `get_egg_award`, `select_task` |

## 六、文件输出位置

| 内容 | 路径 |
|------|------|
| WeeklyActivities反编译 | `swf_cache/decompiled/WeeklyActivities/` |
| Act770反编译 | `swf_cache/decompiled/Act770Entrance/` |
| Act774反编译 | `swf_cache/decompiled/Act774Entrance/` |
| Act775反编译 | `swf_cache/decompiled/Act775Entrance/` |
| Act779反编译 | `swf_cache/decompiled/Act779Entrance/` |
| Act780反编译 | `swf_cache/decompiled/Act780Entrance/` |
| Act720反编译 | `swf_cache/decompiled/Act720Entrance/` |
| 本文档 | `swf_cache/decompiled/WeeklyActivities/docs/weekly_activities_analysis.md` |

## 七、总结

1. **WeeklyActivities.swf** 是每周活动界面的入口模块，负责加载和显示活动列表
2. 活动数据存储在 **weeklyactivity** zlib压缩XML文件中
3. **2026年2月** 共有 **21个活动**，其中 **17个为小游戏类型**
4. **守护水果村 (Act770)** 是典型的小游戏活动，包含完整的游戏循环、怪物AI和校验码系统
5. 所有活动通过 `OP_CLIENT_ACTIVITY_QINGYANG_NEW` 协议与服务器通信

## 八、轮换复用与过期清理

### 8.1 新周活动多半是旧活动的轮换
三类常见形态：①同玩法换 ID（日期码变）；②同 ID 换 SWF/换玩法；③参数微调（次数、奖励、时间窗、冲刺常量）。
所以**先查历史实现，再决定要不要重推**：
- `git log --oneline -- src/hook/ src/protocol/ | head -40`
- `git show <rev>:src/hook/<模块>.cpp | sed -n '1,200p'`
- 对照点：入口 `operation` 列表、`start_game/end_game/sweep_info` 参数、校验码算法、时长/次数常量、代价表。
命中同构旧活动时优先复用（改名 + 换 ID + 调常量），并在结论里写明复用了哪个提交。

### 8.2 报废活动就是死代码，当轮清掉
判据（四道扫描全部归零才算干净）：

| 检查项 | 手段 |
|---|---|
| 不可达函数 | 跨文件 + **文件作用域（注册表/初始化器）** 引用做根，再算函数体内传递闭包 |
| 未引用内部链接函数 | MSVC `C4505`（`/W4`）逐轮清 |
| 零引用全局/静态量 | 全仓库符号出现次数 ≤1 |
| 空壳声明 | 头文件里有声明、无定义且无引用 |

删除顺序（自顶向下，**一层一编译**）：
`UI 按钮/DOM` → `window.*` JS → 入口 `StartOneKeyAct*Packet` → 线程体 `Act*ThreadProc` → 回包处理器 + `registerParams` → 发包函数族 → 状态结构体/静态量/宏 → 头文件声明与命名空间。

红线：
- `C4505` 只在该 `.cpp` **重编译**时重现；扫描前先 `touch` 全部 `.cpp`，否则会误判收敛。
- 自写零引用扫描必须忽略缩进、且把文件作用域初始化算作引用；否则「互相调用的发送函数族」会被当成活代码（本项目曾因此漏掉 38 个函数）。
- 一次删太多会因行号漂移切坏相邻函数（踩过两次，回滚重做）；多行声明要整段删。
- 攒着不清的代价：一次清理 28 个文件、8,086 行，花了数小时；活动报废当轮清只需十几分钟。

### 8.3 常见残留位置
- 头文件里「定义删了、声明忘删」的空壳。
- 未展开的 `#define ACTnnn_STATE` 宏、文件作用域 `g_actnnn*` 静态量。
- `ActivityStateManager` 里只被自身访问器引用的 `ActnnnState` 结构体。
- `GetActivityRemainingCount` 等 `switch` 里指向已删活动的 `case`。
