# 守护水果村 (Act770) 活动分析文档

## 概述

- **活动ID**: 770
- **活动名称**: 守护水果村
- **活动描述**: 大量恶灵来袭水果村！快和陆小果一起使用益果因子能量剑净化恶灵吧！
- **SWF路径**: `assets/activity/djj2026/Act770/Act770Entrance.swf`
- **分析日期**: 2026年2月28日

## 一、活动类型判断

**这是一个小游戏类型活动**，判断依据：

1. ✅ 包含 `GameView.as` - 游戏主视图类
2. ✅ 包含 `MonsterAI.as` - 怪物AI系统
3. ✅ 包含 `MyPropAI.as` - 道具AI系统
4. ✅ 包含 `GameOverView.as` - 游戏结算界面
5. ✅ 包含 `GamePauseView.as` - 游戏暂停界面
6. ✅ 协议包含 `start_game`, `end_game` 关键字

## 二、类结构分析

### 2.1 文件列表

| 文件 | 功能 |
|------|------|
| `Act770Entrance.as` | 活动入口 |
| `data/ActivityData.as` | 活动数据定义 |
| `model/ActiveModel.as` | 数据模型 |
| `control/ActiveControl.as` | 控制器 |
| `main/GameView.as` | **游戏主视图** |
| `main/MonsterAI.as` | 怪物AI |
| `main/MyPropAI.as` | 道具AI |
| `main/ObjectManager.as` | 对象池管理 |
| `main/ActOperation.as` | 操作计算 |
| `main/IObject.as` | 对象接口 |
| `view/MainView.as` | 主界面 |
| `view/GameOverView.as` | 游戏结束界面 |
| `view/GamePauseView.as` | 游戏暂停界面 |
| `view/CountedDown.as` | 倒计时 |
| `view/ExchageView.as` | 兑换界面 |
| `view/RuleView.as` | 规则说明 |
| `view/ActivityEntrance.as` | 活动入口视图 |
| `view/Act770SweepWin.as` | 扫荡窗口 |
| `view/Act770BatchExchWin.as` | 批量兑换窗口 |
| `data/Act770ErrorCode.as` | 错误码定义 |
| `eventManaget/MyEvent.as` | 事件定义 |
| `eventManaget/MyEventManager.as` | 事件管理器 |

## 三、游戏机制详解

### 3.1 游戏流程

```
┌─────────────────────────────────────────────────────────┐
│                     游戏开始                             │
│  - 初始化游戏时间: 91秒                                  │
│  - 初始化血量: 5格                                       │
│  - 初始化分数: 0                                         │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│                   游戏循环 (每帧)                        │
│  1. 更新倒计时                                           │
│  2. 生成恶灵 (根据时间进度调整数量和速度)                │
│  3. 移动恶灵 (朝向玩家)                                  │
│  4. 移动能量弹                                          │
│  5. 碰撞检测                                            │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│                   玩家操作                               │
│  - 鼠标点击: 发射能量弹                                  │
│  - 空格键: 击飞被冻结的恶灵                              │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│                   游戏结束条件                           │
│  1. 血量耗尽 (6格全部扣完)                              │
│  2. 时间结束 (91秒)                                     │
└─────────────────────────────────────────────────────────┘
                          ↓
┌─────────────────────────────────────────────────────────┐
│                   结算流程                               │
│  1. 计算校验码                                          │
│  2. 发送 end_game 协议                                  │
│  3. 显示结算界面                                        │
│  4. 判断是否通关 (分数>=500)                            │
└─────────────────────────────────────────────────────────┘
```

### 3.2 恶灵生成规则

```actionscript
// 根据游戏时间生成不同数量和速度的恶灵
if (time >= 0 && time <= 20) {
    creatCount = Math.random() * 2 + 1;  // 1-2只
    speed = 2;
}
else if (time >= 22 && time <= 40) {
    creatCount = Math.random() * 3 + 1;  // 1-3只
    speed = 3;
}
else if (time >= 42 && time <= 60) {
    creatCount = Math.random() * 2 + 2;  // 2-3只
    speed = Math.random() * 2 + 3;       // 3-5
}
else if (time >= 63 && time <= 88) {
    creatCount = Math.random() * 2 + 2;  // 2-3只
    speed = Math.random() * 3 + 3;       // 3-6
}
```

### 3.3 分数计算规则

```actionscript
// 击飞恶灵获得分数
if (hitCount == 1) {
    addNum = 10;
}
else if (hitCount > 1) {
    addNum = 10 * hitCount + hitCount * hitCount;  // 连击加成
}

// 示例：
// 击飞1只: 10分
// 击飞2只: 20 + 4 = 24分
// 击飞3只: 30 + 9 = 39分
// 击飞5只: 50 + 25 = 75分
```

### 3.4 血量机制

```actionscript
// 恶灵碰撞玩家，扣除血量
// 冷却时间: 2秒
private function subBlood():void {
    if (getTimer() - _lastSubBloodTime <= 2000) return;
    
    _lastSubBloodTime = getTimer();
    var curFrame:int = bg.blood.currentFrame;
    curFrame++;
    
    if (curFrame >= 6) {
        // 血量耗尽，游戏暂停
        gamePause();
    }
}
```

## 四、协议定义

### 4.1 协议常量

```actionscript
public static const active_id:int = 770;

public static const GAME_INFO:String = "game_info";        // 获取游戏信息
public static const START_GAME:String = "start_game";      // 开始游戏
public static const GAME_EVENT:String = "game_event";      // 游戏事件
public static const REVIVE:String = "relive";              // 复活
public static const GAME_OVER:String = "end_game";         // 结束游戏
public static const CLEAR_CD:String = "reduce_cd";         // 减少冷却
public static const BUY_GAME_TIMES:String = "buy_times";   // 购买次数
public static const EXCHANGE_INFO:String = "exchange_info";// 兑换信息
public static const EXCHANGE:String = "exchange";          // 兑换奖励
public static const CATCH:String = "catch";                // 捕捉妖怪
public static const CATCH_RES:String = "catch_res";        // 捕捉结果
public static const BUY_MONSTER:String = "buy_monster";    // 购买妖怪
public static const GET_PASS_AWARD:String = "get_pass_award"; // 获取通关奖励
public static const BUY_GOURP:String = "buy_gourd";        // 购买葫芦
public static const SWEEP:String = "sweep";                // 扫荡
public static const SWEEP_INFO:String = "sweep_info";      // 扫荡信息
```

### 4.2 协议发送方式

```actionscript
// 使用 OP_CLIENT_ACTIVITY_QINGYANG_NEW 协议
sendMessage(MsgDoc.OP_CLIENT_ACTIVITY_QINGYANG_NEW.send, ActivityData.active_id, arr);

// arr[0] = 协议命令 (String)
// arr[1] = 参数1 (int)
// arr[2] = 参数2 (int)
// ...

// 示例：发送游戏结束协议
sendMessage(MsgDoc.OP_CLIENT_ACTIVITY_QINGYANG_NEW.send, 770, ["end_game", checkcode, score]);
```

## 五、校验码计算

### 5.1 校验码算法

```actionscript
// 游戏结束时计算校验码（GameView.as:399-402，已核实）
var uid:uint = uint(GameData.instance.playerData.userId);
var temp1:int = ActOperation.operat(5, uid, 100);   // 5 = 取模
var temp2:int = ActOperation.operat(3, score, ActivityData.instance.checkCode);  // 3 = 乘
var temp3:int = ActOperation.operat(1, ActivityData.instance.checkCode, temp1);  // 1 = 加
var checkcode:int = ActOperation.operat(1, temp2, temp3);
```

`ActOperation.operat`（`swf_cache/decompiled/Act770Entrance/scripts/act_2026/Act770/main/ActOperation.as:10`）编码：1=加、2=减、3=乘、4=除、5=取模、6=按位与。
### 5.2 校验码字段来源

| 字段 | 来源 |
|------|------|
| uid | 玩家卡布号 |
| score | 游戏分数 |
| checkCode | 服务端下发的校验种子 (open_ui时获取) |

## 六、兑换系统

### 6.1 兑换列表

| 名称 | ID | 类型 | 数量 | 价格(勋章) |
|------|-----|------|------|-----------|
| 陆小果 | 3063 | 妖怪 | 1 | 720 |
| 天香丸 | 100034 | 道具 | 1 | 120 |
| 洗髓丹 | 100013 | 道具 | 1 | 250 |
| 天地灵气 | 300028 | 材料 | 4 | 160 |
| 自动战斗加时器 | 100209 | 道具 | 1 | 450 |
| 黄金葫芦 | 100004 | 道具 | 1 | 500 |
| 体力抗性洗髓丹 | 100154 | 道具 | 1 | 580 |

### 6.2 妖怪数据

```actionscript
public static const PET_DATA:Object = {
    "pet_id": 3063,          // 陆小果ID
    "super_pet_id": 3064,    // 超进化后ID
    "pet_name": "陆小果",
    "price": 720,            // 勋章价格
    "kbCoin": 98             // 卡布币价格
};
```

## 七、游戏参数

| 参数 | 值 | 说明 |
|------|-----|------|
| passScore | 500 | 通关分数阈值 |
| simpleMaxScore | 1000 | 简单模式最高分 |
| max_score | 1000 | 最高分数 |
| playCount | 2 | 默认游玩次数 |
| _endTime | 91000 | 游戏时长(毫秒) = 91秒 |
| 发射间隔 | 400ms | 能量弹发射冷却 |
| 碰撞冷却 | 2000ms | 扣血冷却 |
| 最大恶灵数 | 20 | 场上最大恶灵数量 |

## 八、实现自动化建议

### 8.1 自动化流程

```
1. 发送 open_ui 获取游戏信息和checkCode
2. 发送 start_game 开始游戏
3. 等待游戏开始响应
4. 模拟游戏操作:
   - 自动瞄准最近的恶灵
   - 自动发射能量弹
   - 自动按空格击飞冻结恶灵
5. 达到目标分数后发送 end_game
6. 领取奖励
```

### 8.2 封包构造示例

```
// 打开活动界面
OP_CLIENT_ACTIVITY_QINGYANG_NEW.send, 770, ["open_ui"]

// 开始游戏
OP_CLIENT_ACTIVITY_QINGYANG_NEW.send, 770, ["start_game"]

// 结束游戏
OP_CLIENT_ACTIVITY_QINGYANG_NEW.send, 770, ["end_game", checkcode, score]

// 兑换奖励
OP_CLIENT_ACTIVITY_QINGYANG_NEW.send, 770, ["exchange", itemId]

// 扫荡
OP_CLIENT_ACTIVITY_QINGYANG_NEW.send, 770, ["sweep"]
```

## 九、注意事项

1. **校验码必须正确**: 服务端会验证校验码，防止作弊
2. **checkCode来源**: 必须先调用 open_ui 获取 checkCode
3. **分数上限**: 最高1000分，超过可能被判定作弊
4. **通关判定**: 分数>=500才算通关
5. **次数限制**: 每日默认2次，可购买额外次数

## 十、文件位置

| 内容 | 路径 |
|------|------|
| SWF文件 | `swf_cache/Act770Entrance.swf` |
| AS3源码 | `swf_cache/decompiled/Act770Entrance/scripts/act_2026/Act770/` |
| 本文档 | `swf_cache/decompiled/Act770Entrance/docs/Act770_analysis.md` |
