# AS3 源码分析指南

## 核心分析方法论

### 从封包到代码的完整分析流程

```
1. 获取封包数据（十六进制）
   ↓
2. 解析封包头部（Magic + Length + Opcode + Params）
   ↓
3. 查找 Opcode 对应的协议定义
   ↓
4. 定位发送/接收代码
   ↓
5. 分析 Body 结构和字段含义
   ↓
6. 交叉验证（易语言、SWF、实际测试）
   ↓
7. 确定关键参数（奖励、校验码、次数限制）
   ↓
8. 编写 C++ 实现
```

---

## 一、如何分析 AS3 代码

### 1.1 定位协议定义文件

**优先级排序**：
1. `MsgDoc.as` - 全局协议定义（最全）
2. 各模块的 `MsgDoc.as` / `*MsgDoc.as` - 模块局部协议定义
3. Model 类中的静态常量 - 某些活动定义在 Model 中

**当前项目映射**：主游戏协议表在 `swf_cache/decompiled/MainGame/com/game/locators/MsgDoc.as`（另有 `MainGame_remote/`、`MainGame_2026020502/`、`WindEvolveMain/` 三份副本，数值一致，行号略有偏移）；活动子包协议在各自的 `*MsgDoc.as`。协议对象形如 `public static const OP_XXX:Object = {"send":...,"back":...};`（back=0 表示无回包）。

**搜索技巧**（以当前项目反编译根 `swf_cache/decompiled/` 为准；其他工具链按其 AS3 根目录替换）：
```powershell
# 按 opcode 数值搜索
rg "1317120" swf_cache/decompiled/ -g "*.as"

# 按协议名搜索
rg "OP_CLIENT_SPIRIT_EQUIP_ALL" swf_cache/decompiled/ -g "*.as"

# 按活动名称搜索
rg "火风试炼" swf_cache/decompiled/ -g "*.as"
rg "FireWind" swf_cache/decompiled/ -g "*.as"
```

### 1.2 定位发送代码

**特征关键词**：
- `sendCmd(opcode, params, body)`
- `sendMessage(MsgDoc.XXX.send, params, body)`
- `GreenSocket.instance.sendCmd(...)`

**分析步骤**：
```actionscript
// 示例：火风试炼开始游戏
sendMessage(MsgDoc.OP_CLIENT_ACTIVITY_QINGYANG.send, 142, [24]);

// 分析要点：
// 1. Opcode: MsgDoc.OP_CLIENT_ACTIVITY_QINGYANG.send
// 2. Params: 142（活动ID）
// 3. Body: [24]（protocolId，区分具体操作）
```

### 1.3 定位接收代码（解析器）

**特征关键词**：
- `attachSocketListener(opcode, callback)`
- `onDataBack`, `onServerDataBackHandler`
- `parse(msg:MsgPacket)`

**分析步骤**：
```actionscript
// 找到监听注册
con.attachSocketListener(1324097, this.onDataBack);

// 查看回调函数
private function onDataBack(evt:MsgEvent):void {
    var protocolId:int = evt.msg.body.readInt();  // 读取 protocolId
    switch(protocolId) {
        case 24:
            this.decodeBeginGame(evt.msg.body);
            break;
    }
}

// 分析要点：
// 1. 响应 Opcode: 1324097
// 2. Body 结构: 先读 protocolId，再根据值分发到不同函数
// 3. 具体解析逻辑在 decodeBeginGame 中
```

### 1.4 分析 Body 读取顺序

**方法对照表**：

| AS3 方法 | 数据类型 | C++ 对应 | 大小 | 示例 |
|----------|----------|----------|------|------|
| `body.readInt()` | int32 | int32_t | 4B | `count = body.readInt()` |
| `body.readUnsignedInt()` | uint32 | uint32_t | 4B | `userId = body.readUnsignedInt()` |
| `body.readShort()` | int16 | int16_t | 2B | `level = body.readShort()` |
| `body.readUTF()` | UTF字符串 | wstring | 2B+len | `name = body.readUTF()` |
| `body.readBoolean()` | bool | bool | 1B | `flag = body.readBoolean()` |
| `body.readFloat()` | float | float | 4B | `value = body.readFloat()` |
| `body.readByte()` | int8 | int8_t | 1B | `type = body.readByte()` |

**数组读取模式**：
```actionscript
// 模式1：先读长度，再循环
var count:int = body.readInt();
for (var i:int = 0; i < count; i++) {
    var id:int = body.readInt();
    var name:String = body.readUTF();
    // ...
}

// 模式2：固定长度数组
for (var i:int = 0; i < 6; i++) {
    body.readInt();
}
```

---

## 二、如何确定封包用途

### 2.1 通过 opcode 数值定位

**快速分类**：
```powershell
# 战斗相关 (1317120-1317130)
rg "13171[0-2][0-9]" swf_cache/decompiled/ -g "*.as"

# 日常活动 (1184770-1185533)
rg "1184[0-9][0-9][0-9]" swf_cache/decompiled/ -g "*.as"

# 妖怪背包 (1318401)
rg "1318401" swf_cache/decompiled/ -g "*.as"
```

### 2.2 通过 protocolId 区分

**同一个 opcode，不同操作**：
```actionscript
// 火风试炼示例
// 发送 opcode: 1184833, params: 142
// 通过 Body 中的 protocolId 区分功能

switch(protocolId) {
    case 21:  // 查询基础信息
        break;
    case 22:  // 兑换奖励
        break;
    case 23:  // 进化
        break;
    case 24:  // 开始游戏
        break;
    case 25:  // 结束游戏
        break;
    case 26:  // 加速
        break;
}
```

### 2.3 通过 params 值区分

**示例：商城/试炼系统**
```actionscript
// opcode: 1184833
// params: 142 = 试炼活动
// params: 其他 = 商城等其他功能
```

### 2.4 通过封包方向判断

| 类型 | 查找方法 | 典型位置 |
|------|----------|----------|
| 发送包 | 查找 `sendCmd`, `sendMessage` | Control/Controller 类 |
| 接收包 | 查找 `attachSocketListener`, `parse` | Model/Parse 类 |

---

## 三、如何确定奖励数量

### 3.1 从响应 Body 直接读取

**方法1：固定值**
```actionscript
// decodeEndGame - 结束游戏响应
var result:int = data.readInt();
if (result == 0) {
    var flame:int = data.readInt();  // ← 奖励数量
    DataCenter.instance.setTarget("awardNum", flame);
}
```

**方法2：从配置文件读取**
```actionscript
// 从 sprite.xml 或其他配置获取
var award:int = ConfigManager.instance.getAwardById(id);
```

### 3.2 从发送封包 Body 推断

**易语言代码交叉验证**：
```易语言
' 火风试炼结束封包
send文本("...C2010000...")  // 0x1C2 = 450
' 结论：奖励是 450 印记
```

**封包字节数值对照**：
| 字节 | 十六进制 | 十进制 | 含义 |
|------|---------|--------|------|
| C2 01 00 00 | 0x1C2 | 450 | 火风/风暴试炼奖励 |
| 5E 01 00 00 | 0x15E | 350 | 火焰试炼奖励 |
| 04 00 00 00 | 0x4 | 4 | 深度挖宝宝藏数量 |

### 3.3 从 AS3 常量定义

```actionscript
// 配置文件中的奖励定义
public static const FIRE_WIND_REWARD:int = 450;
public static const FIRE_REWARD:int = 350;
```

### 3.4 实际测试验证

1. 发送结束封包，使用不同的奖励值
2. 观察服务器响应
3. 如果 result=1 表示校验错误，result=0 表示成功
4. 成功后查看实际获得的印记数量

---

## 四、如何确定校验码算法

### 4.1 从 AS3 源码直接读取

**标准模式**：
```actionscript
// decodeBeginGame - 开始游戏响应
var result:int = data.readInt();
if (result == 0) {
    var checkCode:int = data.readInt();  // 原始校验码
    var userId:uint = GameData.instance.playerData.userId;
    checkCode = checkCode * 56 + userId % 100000;  // 计算公式
    DataCenter.instance.setTarget("checkCode", checkCode);
}
```

**关键点**：
1. 从响应获取 `rawCheckCode`
2. 使用 `userId` 参与计算
3. 系数可能不同（56、72等）

### 4.2 从易语言代码验证

```易语言
' 火风试炼
send文本("..." + 文本_反转(checkCode × 56 ＋ 用户uid ％ 100000, 真))

' 风暴试炼（系数不同）
send文本("..." + 文本_反转(checkCode × 72 ＋ 用户uid ％ 100000, 真))
```

### 4.3 常见校验码模式

| 活动 | 系数 | 模运算 | 公式 |
|------|-----|--------|------|
| 火风试炼 | 56 | % 100000 | `raw * 56 + userId % 100000` |
| 火焰试炼 | 56 | % 100000 | `raw * 56 + userId % 100000` |
| 风暴试炼 | 72 | % 100000 | `raw * 72 + userId % 100000` |
| 深度挖宝 | 无 | sessionId | 直接使用 sessionId |

### 4.4 反向推导技巧

如果不知道公式，可以：
1. 记录多个 `rawCheckCode` 和 `userId` 对
2. 观察规律
3. 尝试常见系数（56、72、100）
4. 对比易语言代码

---

## 五、如何确定次数限制和冷却

### 5.1 从查询响应读取

**标准响应格式**：
```actionscript
// decodeBasicInfo - 查询基础信息
var count:int = data.readInt();      // 剩余次数
var restTime:int = data.readInt();  // 冷却时间（秒）
var flame:int = data.readInt();     // 印记数量
```

### 5.2 从错误提示推断

```actionscript
// decodeBeginGame 错误码
if (result == 1) {
    // "正在游戏中" - 次数消耗但未完成
}
if (result == 2) {
    // "今天的可玩次数消耗完了" - 次数用完
}
if (result == 3) {
    // "冷却时间还没到" - 冷却中
}
```

### 5.3 服务器控制模式

**重要发现**：次数由服务器控制，不是客户端硬编码

```actionscript
// ActivityModel.as - 从服务器获取次数
CacheData.instance.onlinelist.setPlayTimes(ACTIVITYID, count);

// 客户端只负责查询，不负责验证
```

### 5.4 查询次数的通用方法

**查询封包模板**：
```actionscript
// Opcode: 1185429, Params: 活动ID
// Body: ["open_ui"]

sendMessage(1185429, 668, ["open_ui"]);
```

**响应格式**：
```actionscript
var oper:String = body.readUTF();   // "open_ui"
var digNum:int = body.readInt();   // 剩余次数
var badgeNum:int = body.readInt(); // 徽章数
```

---

## 六、如何分析 SWF 文件

### 6.1 下载 SWF

**常见路径模式**：
```
http://enter.wanwan4399.com/bin-debug/assets/system/{activity}/{name}.swf

示例：
- assets/system/variousEvolution/fireWindTrain/FireWindTrain.swf
- assets/system/variousEvolution/fireTrain/FireTrain.swf
- assets/system/variousEvolution/windTrain/WindEvolveMain.swf
```

### 6.2 反编译工具

**使用 FFDec (JPEXS) - 直接执行命令：**

```bash
python scripts/analyze_swf.py --decompile-game {id}
```

### 6.3 SWF 分析要点

**1. 定位 Model 类**
```
路径：scripts/com/game/{activity}/model/{Activity}Model.as
作用：处理协议、存储数据
```

**2. 定位 Control 类**
```
路径：scripts/com/game/{activity}/control/{Activity}Control.as
作用：用户交互、发送封包
```

**3. 查找协议定义**
```actionscript
// 在 SWF 的 MsgDoc 中
public static const ACTIVITYID:uint = 142;
public static const OP_CLIENT_ACTIVITY_QINGYANG:Object = {
    "send": 1184833,
    "back": 1324097
};
```

**4. 分析校验码计算**
```actionscript
// 在 Model 的 decodeBeginGame 中
checkCode = data.readInt();
userId = uint(GameData.instance.playerData.userId);
checkCode = checkCode * 56 + userId % 100000;
```

### 6.4 SWF 与 AS3 源码对比

| 内容 | AS3 源码 | SWF 反编译 |
|------|----------|-----------|
| 完整性 | ✅ 完整 | ⚠️ 可能混淆 |
| 可读性 | ✅ 清晰 | ⚠️ 变量名丢失 |
| 协议定义 | ✅ MsgDoc.as | ✅ 可找到 |
| 业务逻辑 | ✅ 详细 | ✅ 可找到 |
| 更新速度 | ✅ 最快 | ❌ 需重新下载 |

**建议**：优先使用 AS3 源码，SWF 作为补充

---

## 七、如何进行交叉验证

### 7.1 易语言 → AS3

**易语言代码**：
```易语言
send文本("44531000411412008E00000019000000FFFFFFFFC2010000" + 文本_反转(checkCode × 56 ＋ 用户uid ％ 100000, 真))
```

**解析为 AS3**：
```actionscript
// 44 53 = Magic "SD"
// 10 00 = Length 16
// 41 14 12 00 = Opcode 1184833
// 8E 00 00 00 = Params 142
// 19 00 00 00 = protocolId 25
// FF FF FF FF = result -1
// C2 01 00 00 = awardCount 450
// XX XX XX XX = checkCode

// 结论：发送火风试炼结束封包，奖励450，校验码系数56
```

### 7.2 封包抓包 → AS3

**抓包数据**：
```
44 53 04 00 41 14 12 00 8E 00 00 00 18 00 00 00
```

**分析步骤**：
1. 提取 Opcode: `41 14 12 00` → 1184833
2. 提取 Params: `8E 00 00 00` → 142
3. 提取 Body: `18 00 00 00` → protocolId 24
4. 查 AS3: protocolId 24 = 开始游戏
5. 定位到 `FireWindTrainModel.as` 的 `decodeBeginGame`

### 7.3 实际测试验证

**测试流程**：
1. 发送开始封包
2. 记录响应中的 `rawCheckCode`
3. 计算最终 `checkCode`
4. 发送结束封包
5. 观察服务器响应
6. 如果失败，调整参数重试

---

## 八、完整分析案例：试炼活动

### 8.1 获取信息来源

**来源1：易语言代码**
```易语言
' 火风试炼
tpye ＝ packetbody.readInt ()  // 读取 protocolId
.如果真 (tpye ＝ 24)
    checkcode ＝ packetbody.readInt ()
    启动线程_任意整数参 (&火风试炼, checkcode)

.子程序 火风试炼
    send文本 ("..." ＋ 文本_反转(checkCode × 56 ＋ 用户uid ％ 100000, 真))
    send文本 ("44530400411412008E00000015000000")
```

**来源2：AS3 源码**（`swf_cache/decompiled/FireWindTrain/scripts/com/game/variousEvo/train/fireWind/data/FireWindTrainModel.as`；`decodeBeginGame` 实际位于 :114-124，`:49` 处的 `case 24` 分发到这里）
```actionscript
// FireWindTrainModel.as
case 24:
    this.decodeBeginGame(evt.msg.body);   // :49 分发
    break;

private function decodeBeginGame(data:ByteArray):void {  // 实际 :114
    var result:int = data.readInt();
    if (result == 0) {
        var checkCode:int = data.readInt();
        var useId:uint = uint(GameData.instance.playerData.userId);
        checkCode = checkCode * 56 + useId % 100000;  // :123
        DataCenter.instance.setTarget("checkCode", checkCode);
    }
}
```

**来源3：SWF 反编译**（`swf_cache/decompiled/WindEvolveMain/scripts/com/game/Active20131017WindEvolve/model/WindEvolveModel.as`；case 14 实际在 :57-63，读 checkcode 在 :61）
```actionscript
// WindEvolveModel.as
case 14:                                  // :57
    param.result = body.readInt();
    if (param.result == 0) {
        ServerData.instance.checkcode = body.readInt();  // :61
    }
    break;
```

### 8.2 提取关键信息

| 项目 | 火风试炼 | 火焰试炼 | 风暴试炼 |
|------|---------|---------|---------|
| 开始 protocolId | 24 | 4 | 14 |
| 结束 protocolId | 25 | 5 | 15 |
| 查询 protocolId | 21 | 1 | 11 |
| 奖励数量 | 450 | 350 | 450 |
| 校验码系数 | 56 | 56 | 72 ⚠️ |

### 8.3 构建封包

**火风试炼开始**：
```
44 53 04 00 41 14 12 00 8E 00 00 00 18 00 00 00
     └─ Magic     └─ Opcode   └─ Params └─ protocolId
```

**火风试炼结束**：
```
44 53 10 00 41 14 12 00 8E 00 00 00
19 00 00 00     ← protocolId 25
FF FF FF FF     ← result -1
C2 01 00 00     ← awardCount 450
XX XX XX XX     ← checkCode (raw * 56 + userId % 100000)
```

### 8.4 C++ 实现

```cpp
// 火风试炼 - 开始
BOOL SendFireWindTrialPacket() {
    return SendTrialPacket({24});
}

// 火风试炼 - 结束
BOOL SendFireWindEndPacket(int result, int awardCount, int checkCode) {
    return SendTrialPacket({25, result, awardCount, checkCode});
}

// 响应处理
void ProcessTrialResponse(const BYTE* pData, DWORD dwSize) {
    int protocolId = ReadInt32(body, 0);
    int result = ReadInt32(body, 4);
    
    if (protocolId == 24 && result == 0) {
        int rawCheckCode = ReadInt32(body, 8);
        int checkCode = rawCheckCode * 56 + g_userId % 100000;
        
        // 启动自动完成线程
        CreateThread(..., FireWindTrialThreadProc, checkCode, ...);
    }
}
```

---

## 九、常见问题与解决

### Q1: 找不到 opcode 定义？

**可能原因**：
1. 协议在 Model 类中定义，而非 MsgDoc.as
2. 动态生成的 opcode
3. SWF 独有协议

**解决方法**：
```powershell
# 广泛搜索
rg "1317120" swf_cache/decompiled/ -g "*.as"

# 搜索发送代码
rg "sendCmd.*142" swf_cache/decompiled/ -g "*.as"

# 查看 Model 类
rg "class.*Model.*extends" swf_cache/decompiled/ -g "*.as"
```

### Q2: 解析器找不到？

**可能原因**：
1. 直接在回调函数处理
2. 使用通用解析器
3. 协议处理在 View 层

**解决方法**：
```powershell
# 搜索回调函数
rg "attachSocketListener.*opcode" swf_cache/decompiled/ -g "*.as"

# 搜索 onServerDataBack
rg "onServerDataBack" swf_cache/decompiled/ -g "*.as"

# 搜索 switch 语句
rg "switch.*protocolId" swf_cache/decompiled/ -g "*.as"
```

### Q3: Body 结构不匹配？

**可能原因**：
1. 压缩包（Magic=SC）需要解压
2. 条件分支（params 值不同）
3. 版本更新导致变化
4. 有隐式字段（如标志位）

**解决方法**：
```actionscript
// 检查压缩
if (magic == 0x5343) {  // "SC"
    decompress();
}

// 检查条件
if (evt.msg.mParams == 142) {
    // 特定处理
}

// 检查版本
var version:int = body.readInt();
switch(version) {
    case 1: /* 旧版本 */
    case 2: /* 新版本 */
}
```

### Q4: 校验码计算错误？

**排查步骤**：
1. 确认 userId 获取正确
2. 确认系数正确（56、72）
3. 确认模运算（% 100000）
4. 使用 Wireshark 抓包对比
5. 打印中间值调试

### Q5: 次数限制如何绕过？

**重要说明**：次数由服务器控制，客户端无法绕过

**正确做法**：
```cpp
// 先查询剩余次数
SendQueryPacket();
Sleep(500);

// 根据剩余次数循环
for (int i = 0; i < remainingCount; i++) {
    SendStartPacket();
    Sleep(300);
    SendEndPacket();
    Sleep(800);
}
```

---

## 十、分析工具链

### 10.1 搜索工具

```powershell
# rg (ripgrep) - 最快
rg "opcode" swf_cache/decompiled/ -g "*.as"

# 替代方案
select-string "opcode" -Path swf_cache\decompiled\ -Include *.as -Recurse
```

### 10.2 SWF 分析

**直接执行命令（无需读取脚本文件）：**

```bash
# 下载并反编译
python scripts/analyze_swf.py --decompile-game 12

# 按类名在反编译结果里搜索
rg "FireWindTrainModel" swf_cache/decompiled/ -g "*.as"
```
### 10.3 封包计算

**直接执行命令（无需读取脚本文件）：**

```bash
# 当前脚本没有参数化 CLI，直接运行查看示例
python scripts/calc_opcode.py
```

---

## 十一、总结

### 分析流程图

```
┌─────────────┐
│  获取封包    │ → 十六进制数据
└──────┬──────┘
       ↓
┌─────────────┐
│  解析头部    │ → Opcode, Params, Body
└──────┬──────┘
       ↓
┌─────────────┐
│  查找协议    │ → MsgDoc.as / Model
└──────┬──────┘
       ↓
┌─────────────┐
│  定位代码    │ → 发送/接收函数
└──────┬──────┘
       ↓
┌─────────────┐
│  分析结构    │ → Body 字段顺序
└──────┬──────┘
       ↓
┌─────────────┐
│  提取关键    │ → 奖励、校验码、次数
└──────┬──────┘
       ↓
┌─────────────┐
│  交叉验证    │ → 易语言/SWF/测试
└──────┬──────┘
       ↓
┌─────────────┐
│  编写实现    │ → C++ 代码
└─────────────┘
```

### 关键要点

1. **优先使用 AS3 源码**，SWF 作为补充
2. **交叉验证重要**，易语言代码很有参考价值
3. **服务器控制次数**，客户端无法绕过
4. **校验码系数可能不同**，要仔细区分
5. **实际测试验证**，理论分析要结合实践

### 快速参考

| 需求 | 搜索关键词 | 典型位置 |
|------|-----------|----------|
| 查找 opcode | `1317120` | MsgDoc.as |
| 查找发送代码 | `sendCmd` | Control 类 |
| 查找接收代码 | `attachSocketListener` | Model 类 |
| 查找协议定义 | `OP_XXX` | MsgDoc.as |
| 查找奖励值 | `award`, `reward` | decodeEndGame |
| 查找校验码 | `checkCode` | decodeBeginGame |
| 查找次数限制 | `count`, `leftPlay` | decodeBasicInfo |
| 查找冷却时间 | `restTime`, `coolTime` | decodeBasicInfo |

## 附录：快速速查（内容均已在正文覆盖，此处仅汇总）

### 目录结构（相对包路径，实际根为 `swf_cache/decompiled/MainGame/`）

```text
com/game/
├── locators/MsgDoc.as      ← 协议定义（核心，另有 MainGame_remote/ 等副本，值一致）
├── modules/control/        ← 控制器（回调函数、发包）
├── modules/model/          ← 模型（收包解析）
├── modules/view/           ← 视图层
├── modules/parse/          ← 解析器
└── manager/
org/green/server/
├── core/GreenSocket.as     ← 网络核心（sendCmd :93 / attachSocketListener :65 / dispatch :247）
├── data/CmdPacket.as       ← 发送封包（encode 序列化 :106-125）
├── data/MsgPacket.as       ← 接收封包
└── interfaces/IParser.as   ← 解析器接口
```

### 协议定义文件（MsgDoc.as）

格式：`public static var OP_XXX:Object = {"send": xxx, "back": xxx};`，`back = 0` 表示无回包。已核实示例：

```actionscript
public static var OP_GATEWAY_BATTLE_START:Object = {"back": 1317120};   // 只有 back，没有 send
public static var OP_GATEWAY_BATTLE_END:Object = {"send": 1186323, "back": 1317125};
public static var OP_CLIENT_REQ_ENTER_WORLD:Object = {"send": 1183750, "back": 1314822};
public static var OP_CLIENT_SPIRIT_EQUIP_ALL:Object = {"send": 1185809, "back": 1316881};
public static var OP_CLIENT_ACTIVITY_QINGYANG:Object = {"send": 1184833, "back": 1324097};
public static var OP_CLIENT_ACTIVITY_QINGYANG_NEW:Object = {"send": 1185429, "back": 1316501};
```

### 网络核心（GreenSocket / CmdPacket / MsgPacket）

发送入口是 `GreenSocket.sendCmd(mOpcode:uint, mParams:int = 0, body:Array = null)`（`GreenSocket.as:93-97`），内部 `CmdPacket.build(mOpcode, mParams, body)` -> `sendpacket(packet)`。`ViewConLogic.as:18` 的 `sendMessage(opcode, param, body)` 是各模块通用的套接字发送封装（转调 `con.sendCmd`）；`Channel.as:31` 的同名方法是应用内事件，不是网络包。

接收：`attachSocketListener(eventID:int, callBack:Function, pharse:Class = null)`（:65-69），收包分发 `dispatch`（:247-267）以 `packet.mOpcode` 为事件名派发 `MsgEvent`，即 back opcode 字符串与注册一致。

`CmdPacket.encode()`（:106-125）序列化为 `writeShort(0x5344) + writeShort(bodyLen) + writeUnsignedInt(opcode) + writeUnsignedInt(params) + body`，与 AGENTS.md 封包布局一致。

### 从十六进制封包定位代码（速查）

1. 解析前 12 字节：Magic + Length + Opcode + Params
2. 在 `MsgDoc.as` 搜 opcode 值，确定是 `send` 还是 `back`
3. 搜 `attachSocketListener(opcode, ...)` 找回调/解析器
4. 按 `parse(msg:MsgPacket)` 中 `body.readXXX()` 顺序还原字段

### 示例：分析灵玉列表封包（1316881）

`1316881` = `OP_CLIENT_SPIRIT_EQUIP_ALL.back`。注册：`attachSocketListener(1316881, ...)`；解析函数里按 `readInt`/`readUTF` 顺序读字段（backFlag、count、循环 id/index/flag/name……）。字段顺序以实际解析器（`SpiritParse` 或对应 model）为准，速查代码不保证精确字段。

### 常见问题（速查）

- 找不到 opcode：协议可能在 Model 中定义、动态生成或 SWF 独有 → 用数值搜 `swf_cache/decompiled/ -g "*.as"`
- 解析器找不到：直接在回调里处理、通用解析器、或在 View 层 → 搜回调函数名而非解析器类名
- Body 不匹配：压缩包（Magic=SC）需 zlib 解压、params 条件分支、版本差异、隐式标志位

### 注意事项

1. **压缩包处理**：Magic 为 `0x5343`("SC") 时，Body 需要 zlib 解压
2. **字符串格式**：`readUTF()` 读取的字符串前 2 字节为长度
3. **数组格式**：通常是先 4 字节长度，再循环读取元素
4. **动态字段**：某些封包根据 params 值有不同结构
5. **版本差异**：游戏更新可能导致协议变化
