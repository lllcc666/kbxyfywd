# 试炼活动模块

## 协议概览

三个试炼活动使用相同的Opcode，通过Body中的protocolId区分不同操作：

| 参数 | 值 | 说明 |
|------|-----|------|
| 发送 Opcode | 1184833 (0x00121441) | `OP_CLIENT_ACTIVITY_QINGYANG.send` |
| 发送 Params | 142 | 活动ID |
| 响应 Opcode | 1324097 (0x00142E41) | `OP_CLIENT_ACTIVITY_QINGYANG.back` |
| 响应 Params | 142 | 活动ID |

---

## 状态：✅ 已实现

基于易语言代码和AS3源码分析完成实现。

---

## 三个试炼活动对比

| 活动 | 开始 protocolId | 结束 protocolId | 查询 protocolId | 奖励印记 | 校验码系数 |
|------|----------------|----------------|----------------|---------|-----------|
| 火风试炼 | 24 | 25 | 21 | 450 | **56** |
| 火焰试炼 | 4 | 5 | 1 | 350 | **56** |
| 风暴试炼 | 14 | 15 | 11 | 450 | **72** ⚠️ |

**重要**：风暴试炼的校验码系数是72，不同于其他两个的56！

---

## 协议详细分析

### 查询基础信息

查询当前状态：游戏次数、冷却时间、印记数量

| 活动 | protocolId | Body (发送) | Body (响应) |
|------|------------|-------------|-------------|
| 火风试炼 | 21 | `[21]` | `[21, count, restTime, flame]` |
| 火焰试炼 | 1 | `[1]` | `[1, count, restTime, flame]` |
| 风暴试炼 | 11 | `[11]` | `[11, leftPlay, restTime, brandCount]` |

**响应字段说明**：
- `count / leftPlay`: 剩余游戏次数
- `restTime`: 冷却时间（秒）
- `flame / brandCount`: 当前印记数量

### 开始游戏

发送开始游戏请求，服务器返回校验码

| 活动 | protocolId | Body (发送) | Body (响应) |
|------|------------|-------------|-------------|
| 火风试炼 | 24 | `[24]` | `[24, result, checkCode?]` |
| 火焰试炼 | 4 | `[4]` | `[4, result, checkCode?]` |
| 风暴试炼 | 14 | `[14]` | `[14, result, checkcode?]` |

**result 值说明**：
- `0`: 成功，可以开始游戏
- `1`: 正在游戏中
- `2`: 今日次数已用完
- `3`: 冷却时间未到

**checkCode 计算**：
```cpp
// 从响应获取原始 checkCode
int rawCheckCode = body.readInt();

// 计算真正的校验码
int checkCode;
if (火风试炼 || 火焰试炼) {
    checkCode = rawCheckCode * 56 + userId % 100000;
} else if (风暴试炼) {
    checkCode = rawCheckCode * 72 + userId % 100000;  // 注意系数是72！
}
```

### 结束游戏

发送结束游戏封包，提交游戏结果

| 活动 | protocolId | Body (发送) | Body (响应) |
|------|------------|-------------|-------------|
| 火风试炼 | 25 | `[25, result, awardCount, checkCode]` | `[25, result, flame?]` |
| 火焰试炼 | 5 | `[5, result, awardCount, checkCode]` | `[5, result, flame?]` |
| 风暴试炼 | 15 | `[15, exitStatus, brand, checkCode]` | `[15, result, ?]` |

**结束封包字段说明**：
- `result / exitStatus`: 游戏结果，`-1` 表示退出
- `awardCount / brand`: 获得的印记数量（固定值：火风450，火焰350，风暴450）
- `checkCode`: 计算后的校验码

**响应 result 值说明**：
- `0`: 成功，获得印记
- `1`: 校验码错误
- `2`: 游戏时间作弊
- `3`: 印记数量作弊
- `4`: 时间数量数值错误

---

## 封包格式示例

### 火风试炼完整流程

```
1. 查询信息:
   发送: 44 53 04 00 41 14 12 00 8E 00 00 00 15 00 00 00
   响应: [...protocolId=21, count, restTime, flame...]

2. 开始游戏:
   发送: 44 53 04 00 41 14 12 00 8E 00 00 00 18 00 00 00
   响应: [...protocolId=24, result=0, checkCode...]

3. 结束游戏 (延时300ms后):
   发送: 44 53 10 00 41 14 12 00 8E 00 00 00 
         19 00 00 00    // protocolId=25
         FF FF FF FF    // result=-1
         C2 01 00 00    // awardCount=450
         XX XX XX XX    // checkCode (计算后)
   
4. 查询信息刷新:
   发送: 44 53 04 00 41 14 12 00 8E 00 00 00 15 00 00 00
```

### 火焰试炼完整流程

```
1. 查询信息:
   发送: 44 53 04 00 41 14 12 00 8E 00 00 00 01 00 00 00

2. 开始游戏:
   发送: 44 53 04 00 41 14 12 00 8E 00 00 00 04 00 00 00

3. 结束游戏:
   发送: 44 53 10 00 41 14 12 00 8E 00 00 00
         05 00 00 00    // protocolId=5
         FF FF FF FF    // result=-1
         5E 01 00 00    // awardCount=350
         XX XX XX XX    // checkCode

4. 查询信息:
   发送: 44 53 04 00 41 14 12 00 8E 00 00 00 01 00 00 00
```

### 风暴试炼完整流程

```
1. 查询信息:
   发送: 44 53 04 00 41 14 12 00 8E 00 00 00 0B 00 00 00

2. 开始游戏:
   发送: 44 53 04 00 41 14 12 00 8E 00 00 00 0E 00 00 00

3. 结束游戏 (系数72！):
   发送: 44 53 10 00 41 14 12 00 8E 00 00 00
         0F 00 00 00    // protocolId=15
         FF FF FF FF    // exitStatus=-1
         C2 01 00 00    // brand=450
         XX XX XX XX    // checkCode (系数72计算)

4. 查询信息:
   发送: 44 53 04 00 41 14 12 00 8E 00 00 00 0B 00 00 00
```

---

## 次数与冷却

### 每日次数限制

根据AS3代码分析，次数由服务器控制：

```actionscript
// decodeBeginGame 响应分析
if (result == 2) {
    // "今天的可玩次数消耗完了"
}
if (result == 3) {
    // "冷却时间还没到"
}
```

**建议**：执行前先查询基础信息获取剩余次数。

### 冷却时间

从查询响应中获取 `restTime`（冷却时间，单位秒）：

```actionscript
// decodeBasicInfo
var count:int = data.readInt();     // 剩余次数
var restTime:int = data.readInt();  // 冷却时间（秒）
var flame:int = data.readInt();     // 印记数量
```

---

## 校验码解密

### 算法分析

从AS3源码和易语言代码确认：

```actionscript
// FireWindTrainModel.as（swf_cache/decompiled/FireWindTrain/scripts/com/game/variousEvo/train/fireWind/data/FireWindTrainModel.as）
// decodeBeginGame 实际在 :114-124（文档旧行号 :486-494 有误）
private function decodeBeginGame(data:ByteArray) : void
{
   var result:int = data.readInt();
   if (result == 0) {
      checkCode = data.readInt();
      checkCode = checkCode * 56 + useId % 100000;  // :123
      DataCenter.instance.setTarget("checkCode",checkCode);
   }
}
```

```易语言
' 火风试炼
send文本("..." + 文本_反转(checkCode × 56 ＋ 用户uid ％ 100000, 真))

' 风暴试炼 (注意系数是72)
send文本("..." + 文本_反转(checkCode × 72 ＋ 用户uid ％ 100000, 真))
```

### 校验码计算表

| 活动 | 系数 | 公式 |
|------|-----|------|
| 火风试炼 | 56 | `checkCode = rawCheckCode * 56 + userId % 100000` |
| 火焰试炼 | 56 | `checkCode = rawCheckCode * 56 + userId % 100000` |
| 风暴试炼 | **72** | `checkCode = rawCheckCode * 72 + userId % 100000` |

---

## 奖励分析

### 印记获取

从易语言代码中确认的固定奖励值：

| 活动 | 印记奖励 | 封包中的值 |
|------|---------|-----------|
| 火风试炼 | 450 印记 | `C2 01 00 00` (0x1C2) |
| 火焰试炼 | 350 印记 | `5E 01 00 00` (0x15E) |
| 风暴试炼 | 450 印记 | `C2 01 00 00` (0x1C2) |

**注意**：这些是发送封包中的 `awardCount` 值，实际获得可能由服务器验证。

### 印记用途

印记用于兑换奖励和进化妖怪：

```actionscript
// decodeExchangeAward - 兑换奖励
if (result == 1) {
    // "火风印记不足"
}
if (result == 2) {
    // "该物品今天兑换达到上限了"
}

// decodeEvolution - 进化妖怪
if (result == 2) {
    // "今天进化次数达到上限了，明天再来吧"
}
```

---

## C++ 实现

### 常量定义

```cpp
// 试炼活动参数（从AS3代码分析）
#define TRIAL_FIREWIND_AWARD   450  // 火风试炼奖励
#define TRIAL_FIREWIND_FACTOR  56   // 火风试炼系数
#define TRIAL_FIRE_AWARD       350  // 火焰试炼奖励
#define TRIAL_FIRE_FACTOR      56   // 火焰试炼系数
#define TRIAL_STORM_AWARD      450  // 风暴试炼奖励
#define TRIAL_STORM_FACTOR     72   // 风暴试炼系数（不同！）

// Opcode 定义
#define OPCODE_TRIAL_SEND      1184833
#define OPCODE_TRIAL_BACK      1324097
```

### 发送封包函数

```cpp
// 通用试炼封包发送
static BOOL SendTrialPacket(const std::vector<int32_t>& bodyValues) {
    size_t bodyLen = bodyValues.size() * 4;
    std::vector<BYTE> packet(12 + bodyLen);
    
    // Magic: "SD"
    packet[0] = 0x44; packet[1] = 0x53;
    
    // Length
    packet[2] = static_cast<BYTE>(bodyLen & 0xFF);
    packet[3] = static_cast<BYTE>((bodyLen >> 8) & 0xFF);
    
    // Opcode: 1184833 (小端序)
    packet[4] = 0x41; packet[5] = 0x14;
    packet[6] = 0x12; packet[7] = 0x00;
    
    // Params: 142
    packet[8] = 0x8E; packet[9] = 0x00;
    packet[10] = 0x00; packet[11] = 0x00;
    
    // Body (小端序)
    for (size_t i = 0; i < bodyValues.size(); i++) {
        size_t offset = 12 + i * 4;
        packet[offset] = bodyValues[i] & 0xFF;
        packet[offset + 1] = (bodyValues[i] >> 8) & 0xFF;
        packet[offset + 2] = (bodyValues[i] >> 16) & 0xFF;
        packet[offset + 3] = (bodyValues[i] >> 24) & 0xFF;
    }
    
    return SendPacket(0, packet.data(), packet.size());
}

// 火风试炼 - 开始游戏
BOOL SendFireWindTrialPacket() {
    return SendTrialPacket({24});
}

// 火风试炼 - 结束游戏
BOOL SendFireWindEndPacket(int result, int awardCount, int checkCode) {
    return SendTrialPacket({25, result, awardCount, checkCode});
}
```

### 自动完成线程

```cpp
// 火风试炼线程
DWORD WINAPI FireWindTrialThreadProc(LPVOID lpParam) {
    int checkCode = (int)(INT_PTR)lpParam;
    
    // 延时模拟游戏
    Sleep(300);
    
    // 发送结束封包
    SendFireWindEndPacket(-1, TRIAL_FIREWIND_AWARD, checkCode);
    
    // 刷新状态
    Sleep(100);
    SendFireWindQueryPacket();
    
    return 0;
}
```

### 响应处理

```cpp
void ProcessTrialResponse(const BYTE* pData, DWORD dwSize) {
    // 解析 body
    int32_t protocolId = ReadInt32(body, 0);
    int32_t result = ReadInt32(body, 4);
    
    switch (protocolId) {
        case 24:  // 火风试炼开始响应
            if (result == 0) {
                int rawCheckCode = ReadInt32(body, 8);
                int checkCode = rawCheckCode * 56 + g_userId % 100000;
                
                // 启动自动完成线程
                CreateThread(nullptr, 0, FireWindTrialThreadProc, 
                    (LPVOID)(INT_PTR)checkCode, 0, nullptr);
            }
            break;
        // ... 其他 protocolId 处理
    }
}
```

---

## AS3 源码定位

### 火风试炼

**SWF路径**: `http://enter.wanwan4399.com/bin-debug/assets/system/variousEvolution/fireWindTrain/FireWindTrain.swf`

**关键类**:
- `com.game.variousEvo.train.fireWind.data.FireWindTrainModel` - 协议处理

### 火焰试炼

**SWF路径**: `http://enter.wanwan4399.com/bin-debug/assets/system/variousEvolution/fireTrain/FireTrain.swf`

**关键类**:
- `com.game.variousEvo.train.fire.data.FireTrainModel` - 协议处理

### 风暴试炼

**SWF路径**: `http://enter.wanwan4399.com/bin-debug/assets/system/variousEvolution/windTrain/WindEvolveMain.swf`

**关键类**:
- `com.game.Active20131017WindEvolve.model.WindEvolveModel` - 协议处理
- `com.game.Active20131017WindEvolve.event.WindEvolveMsgDoc` - Opcode定义

---

## 注意事项

1. **校验码系数不同**：风暴试炼是72，其他两个是56
2. **延时控制**：开始到结束之间需要300ms延迟
3. **次数查询**：执行前先查询确认有剩余次数
4. **冷却时间**：检查 `restTime` 是否为0
5. **userId 获取**：从进入世界封包获取卡布号
