# 小游戏模块

## Opcode 定义

```cpp
#define OPCODE_LITTLE_GAME_SEND 1184789  // 小游戏发送 (0x00121415)
#define OPCODE_LITTLE_GAME_BACK 1315861  // 小游戏响应
```

## 状态：⚠️ 部分实现

- ✅ 深度挖宝 (gameType=12) 已实现
- ❌ 其他小游戏需要时再分析

---

## 小游戏ID映射表

| gameType | 名称 | 状态 |
|----------|------|------|
| 1 | 推箱子 | ❌ 未分析 |
| 2 | 连连看 | ❌ 未分析 |
| 3 | 排行榜游戏 | ❌ 未分析 |
| 4 | 钓鱼 | ❌ 未分析 |
| 6 | 特殊游戏 | ❌ 未分析 |
| **12** | **深度挖宝** | ✅ 已实现 |
| >1000 | 任务小游戏 | ❌ 未分析 |

---

## 通用协议格式

### 发送封包

```
44 53 LL 00 15 14 12 00 GG 00 00 00 [Body]
│    │    │         │              └─ Body: 6个int32
│    │    │         └─ Params: gameType (游戏类型ID)
│    │    └─ Opcode: 1184789
│    └─ Length: Body长度 (通常24字节)
└─ Magic: "SD"
```

### Body 结构 (6个int32)

```
[act, param1, param2, param3, param4, param5]
```

| act | 说明 |
|-----|------|
| 0 | 开始游戏 |
| 4 | 结束游戏（提交分数） |

---

## 深度挖宝 (gameType=12)

### 流程

```
1. 发送开始封包: [0, 0, 0, 0, 0, 0]
2. 接收响应: [0, sessionId, ...]
3. 延迟500ms
4. 发送结束封包: [4, 0, score, sessionId, 0, 0]
```

### 次数限制与查询

**重要发现**：小游戏次数由服务器控制，不是客户端硬编码！

#### 查询剩余次数

```cpp
// 发送查询封包
// Opcode: 1185429 (OP_CLIENT_ACTIVITY_QINGYANG_NEW.send)
// Params: 668 (活动ID)
// Body: ["open_ui"]

BYTE packet[21] = {
    0x44, 0x53,                    // Magic: "SD"
    0x09, 0x00,                    // Length: 9字节
    0x15, 0x15, 0x12, 0x00,        // Opcode: 1185429
    0x9C, 0x02, 0x00, 0x00,        // Params: 668
    0x07, 0x00,                    // UTF长度: 7
    'o', 'p', 'e', 'n', '_', 'u', 'i'  // "open_ui"
};
```

#### 解析查询响应

```cpp
// 响应 Opcode: 1316501 (OP_CLIENT_ACTIVITY_QINGYANG_NEW.back)
// 响应 Params: 668
// Body格式: ["open_ui", digNum, badgeNum, mapNum, ...]

void ProcessDeepDigQueryResponse(const BYTE* pData, DWORD dwSize) {
    const BYTE* body = pData + 12;
    size_t offset = 0;
    
    // 读取 oper (UTF字符串)
    uint16_t strLen = body[offset] | (body[offset + 1] << 8);
    offset += 2 + strLen;  // 跳过字符串
    
    // 读取 digNum (剩余次数)
    int32_t digNum = body[offset] | (body[offset+1] << 8) |
                     (body[offset+2] << 16) | (body[offset+3] << 24);
    
    // digNum 就是剩余可玩次数
    g_deepDigRemainingCount = digNum;
}
```

#### AS3源码定位

```actionscript
// ActivityModel.as:1026 - 解析响应
public function onAct668Update(evt:MsgEvent) : void
{
   var oper:String = evt.msg.body.readUTF();  // "open_ui"
   var digNum:int = evt.msg.body.readInt();   // 剩余次数
   var badgeNum:int = evt.msg.body.readInt(); // 徽章数
   var mapNum:int = evt.msg.body.readInt();   // 地图数
   // ...
   CacheData.instance.digNumByAct668 = digNum;  // 存储到缓存
}

// CacheData.as:90 - 缓存定义
public var digNumByAct668:int;  // 服务器下发的次数
```

### 冷却时间分析

**结论**：深度挖宝没有冷却时间，但有以下限制：

| 限制类型 | 说明 |
|----------|------|
| 每日次数 | 服务器控制（通常2次/天） |
| 请求间隔 | 开始→结束需要500ms延迟（模拟游戏过程） |
| 循环间隔 | 多次游戏之间建议800ms+延迟 |
| 服务器验证 | sessionId 必须从响应中获取 |

### 完整实现示例

```cpp
// 一键完成深度挖宝（自动执行剩余次数）
void ExecuteDeepDigAuto() {
    // 1. 查询剩余次数
    SendQueryDeepDigCountPacket();
    Sleep(500);  // 等待响应
    
    // 2. 循环执行
    for (int i = 0; i < g_deepDigRemainingCount; i++) {
        // 发送开始封包
        BYTE startPacket[36] = { /* ... */ };
        SendPacket(0, startPacket, sizeof(startPacket));
        
        // 等待响应获取sessionId
        Sleep(500);
        
        // 发送结束封包（score=4满额）
        BYTE endPacket[36] = { /* ... */ };
        // 填充 sessionId
        SendPacket(0, endPacket, sizeof(endPacket));
        
        // 间隔
        Sleep(800);
    }
}
```

### 已实现的C++代码

详见 [module-daily-task.md](./module-daily-task.md) 中的深度挖宝部分。

### SWF分析

**下载地址**：`http://enter.wanwan4399.com/bin-debug/assets/littlegame/game12.swf`

**关键类**：
- `DeepHole` - 主类
- `game.MyMap` - 地图管理
- `game.TuDui` - 土堆（宝藏）

**关键方法**：
- `onClickStartSure` - 开始游戏
- `gameOver` - 游戏结束
- `getGood` - 获取奖励

**关键发现**：
- `treasureNum = 4` - 需要找到4个宝藏
- `score` 是找到的宝藏数量（最多4个）

---

## AS3代码定位

### 控制器层

### 控制器层（`MainGame/com/game/modules/control/LittlegameControl.as`）

```actionscript
// LittlegameControl.as:63 - 发送开始游戏[0,0,0,0,0,0]
sendMessage(MsgDoc.OP_CLIENT_LITTLE_GAME.send, gameType, [0,0,0,0,0,0]);
// :57 变体带玩家信息：[0,0,0,0,littlegameplayerid,littlegameplayerScore]

// LittlegameControl.as:78 - 发送结束游戏
sendMessage(MsgDoc.OP_CLIENT_LITTLE_GAME.send, gameType, [4, gamelevel-1, gamescore, sessionId, 0, 0]);
```

（另有 `GameHallControl.as:85/97` 同款发送：开始 `[0]`、结束 `[4,0,0,sessionid]`——公开的对局入口走 GameHallControl，活动入口走 LittlegameControl；两者都发 `MsgDoc.OP_CLIENT_LITTLE_GAME.send`=1184789。文档中"Body 结构(6个int32)"描述准确，但要注意 LittlegameControl.as:57 实为带玩家信息的变体，纯零开始包在 :63。）

### 模型层回调

```actionscript
// MainModel.as:150 - 注册监听
registerListener(MsgDoc.OP_CLIENT_LITTLE_GAME.back.toString(), this.littleGameOverBack);

// MainModel.as:2054 - 回调函数
private function littleGameOverBack(event:MsgEvent) : void
{
   var act_id:int = event.msg.body.readInt();    // act
   var result_id:int = event.msg.body.readInt(); // result/sessionId
   
   if (act_id == 0) {
       // result_id 是 sessionId
       sessionId = result_id;
   }
   if (act_id == 4) {
       // result_id 是修炼值奖励
       gamexiuxing = result_id;
   }
}
```

### 弹窗控制

```actionscript
// WindowLayerControl.as:542
case "littegame":
   if(params.opdata.popid == 12)  // popid=12 是深度挖宝
   {
      this.dispatch(EventConst.SHOW_LITTLE_GAME_START, 12);
      return;
   }
```

---

## 分析新小游戏的步骤

### 1. 定位游戏ID

```powershell
# 搜索 popid 判断
rg "popid\s*==\s*\d+" --type as src/

# 搜索游戏启动
rg "SHOW_LITTLE_GAME_START" --type as src/
```

### 2. 下载并反编译SWF

**直接执行命令（无需读取脚本文件）：**

```bash
python scripts/analyze_swf.py --decompile-game {ID}
```

### 3. 分析游戏逻辑

- 找主类（通常与游戏名相关）
- 找 `disport()` 或游戏结束相关方法
- 关注 `BattleEvent.littleGameOver` 事件派发

### 4. 确定封包参数

- 查看 `LittlegameControl.as` 中的封包构建
- 注意 `sessionId` 的获取和传递

### 5. 实现自动化

- 发送开始封包 → 等待响应获取sessionId → 发送结束封包
- 结束封包的 `score` 参数决定奖励

---

## SWF分析工具

**直接执行命令（无需读取脚本文件）：**

```bash
# 下载并分析指定游戏
python scripts/analyze_swf.py --game 12

# 反编译小游戏
python scripts/analyze_swf.py --decompile-game 12

# 下载主程序
python scripts/analyze_swf.py --main
```

---

## 注意事项

1. **sessionId 必须正确**：从响应中提取，用于结束封包
2. **score 影响奖励**：不同游戏有不同的分数计算方式
3. **gameType 决定游戏**：Params 字段传入游戏类型
4. **延迟控制**：开始到结束之间需要适当延迟
5. **多关卡游戏**：`level-1` 参数表示关卡索引
6. **次数限制**：小游戏次数由服务器控制，需查询活动接口获取
7. **冷却时间**：大多数小游戏无冷却，但需要合理延迟避免被服务器拒绝

---

## 通用模式：次数查询

很多活动/小游戏都有次数限制，通用查询方法：

### 查询封包模板

```cpp
// Opcode: 1185429 (OP_CLIENT_ACTIVITY_QINGYANG_NEW.send)
// Params: 活动ID (如 668, 667, 702 等)
// Body: ["open_ui"]

// 示例：查询活动668
BYTE queryPacket[] = {
    0x44, 0x53,
    0x09, 0x00,
    0x15, 0x15, 0x12, 0x00,  // Opcode: 1185429
    0x9C, 0x02, 0x00, 0x00,  // Params: 668
    0x07, 0x00,
    'o', 'p', 'e', 'n', '_', 'u', 'i'
};
```

### 解析响应模板

```actionscript
// AS3 通用模式 (ActivityModel.as)
var oper:String = evt.msg.body.readUTF();   // 操作类型
var count:int = evt.msg.body.readInt();     // 剩余次数/状态
// ... 根据活动不同有不同字段
```

### 已知活动ID映射

| 活动ID | 名称 | 说明 |
|--------|------|------|
| 668 | 深度挖宝 | 每日2次，无冷却 |
| 667 | ? | 待分析 |
| 702 | ? | 待分析 |

---

## 待分析游戏

如需分析其他小游戏，请提供：
1. 游戏名称或ID
2. 游戏触发场景（任务/活动）
3. 具体问题或需求

分析时会：
1. 定位 AS3 源码中的相关代码
2. 下载并反编译 SWF
3. 确定封包格式和参数
4. 提供 C++ 实现参考
