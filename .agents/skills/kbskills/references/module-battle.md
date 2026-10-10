# 战斗模块

## Opcode 定义（与 AS3 `MsgDoc.as` 一一对应）

```cpp
#define OPCODE_BATTLE_START  1317120  // = MsgDoc.OP_GATEWAY_BATTLE_START.back（该常量只有 back，无 send；send 侧是 OP_GATEWAY_BATTLE_READY=1186049）
#define OPCODE_BATTLE_ROUND  1317122  // = MsgDoc.OP_GATEWAY_BATTLE_ROUND_RESULE.back（注意 AS3 拼写是 RESULE）
#define OPCODE_BATTLE_END    1317125  // = MsgDoc.OP_GATEWAY_BATTLE_END.back（send=1186323，见下）
```

**重要勘误**：AS3 中 `1317130` 不是"战斗结束"，而是 `MsgDoc.OP_GATEWAY_BATTLE_NEWEXP`（"战斗新经验"），携带胜负标记（`win` 无符号，胜负判定 `win & 0x80000000`）、掉落、属性、精灵经验/升级数据，C++ 侧应把 1317130 当"新经验结算包"解析。真正的战斗结束回包是 `1317125`（`OP_GATEWAY_BATTLE_END.back`），其 body 为 `[param, num, (sid, res)*num]`（param==1 时）。对应的收口发包是 `sendMessage(MsgDoc.OP_GATEWAY_BATTLE_END.send)`，send=1186323。

战斗中发送操作的包是 `MsgDoc.OP_CLIENT_USER_OP`（send=1186050，back=0，`MsgDoc.as:279-282`），mParams=cmdtype：0=普攻/技能 body=`[target, skillId]`；1=切宠 body=`[spiritid]`；2=道具 body=`[packcode, position, sid]`；3=逃跑 body 为空（`BattleControl.onBattleOpration` :381-412）。回合演出播完准备下一回合时发 `MsgDoc.OP_CLIENT_BATTLE_PLAY_OVER.send`（1186056）。

旧版误写（仅供定位参考，勿再使用）：`#define OPCODE_BATTLE_END 1317130 // 战斗结束` 的注释是错的，1317130 的语义见上段。

---

## 数据结构

```cpp
struct BattleSkill {
    uint32_t id;        // 技能ID
    int32_t pp;         // 当前PP值
    int32_t maxPp;      // 最大PP值
    int32_t time;       // 时间
    int32_t maxTime;    // 最大时间
    std::wstring name;  // 技能名称（从skill.xml获取）
};

struct BattleEntity {
    int32_t sid;            // 阵营标识 (1=我方, 2=敌方)
    int32_t groupType;      // 组类型 (1=我方, 2=敌方)
    int32_t userId;         // 玩家ID
    int32_t spiritId;       // 妖怪ID（用于查名称）
    int32_t uniqueId;       // 唯一ID
    int32_t elem;           // 系别
    int32_t state;          // 状态 (1=出战, 2=当前出战)
    int32_t hp;             // 当前血量
    int32_t maxHp;          // 最大血量
    int32_t level;          // 等级
    int32_t skillNum;       // 技能数量
    std::vector<BattleSkill> skills;  // 技能列表
    std::wstring name;      // 妖怪名称（从sprite.xml获取）
    bool mySpirit;          // 是否我方
};

struct BattleData {
    std::vector<BattleEntity> myPets;      // 我方妖怪
    std::vector<BattleEntity> otherPets;   // 敌方妖怪
    int32_t myActiveIndex;                 // 当前出战索引（我方）
    int32_t otherActiveIndex;              // 当前出战索引（敌方）
    int32_t battleType;                    // 战斗类型
    int32_t escape;                        // 是否可逃跑
    int32_t round;                         // 回合数
};
```

---

## 封包解析（已验证）

### 战斗开始 (1317120)

```cpp
void PacketParser::ProcessBattlePacket(const GamePacket& packet) {
    if (packet.opcode == OPCODE_BATTLE_START) {
        size_t offset = 0;
        const uint8_t* data = packet.body.data();
        size_t size = packet.body.size();
        
        // 读取第一个状态值
        int state = ReadInt32LE(data, offset);
        
        // 循环读取妖怪数据，直到 state == -1
        while (state != -1 && offset + 4 <= size) {
            BattleEntity pet;
            if (state == 2) pet.state = 1;
            else pet.state = state;
            
            pet.sid = ReadInt32LE(data, offset);
            pet.groupType = ReadInt32LE(data, offset);
            pet.hp = ReadInt32LE(data, offset);
            pet.maxHp = ReadInt32LE(data, offset);
            pet.level = ReadInt32LE(data, offset);
            pet.elem = ReadInt32LE(data, offset);
            pet.spiritId = ReadInt32LE(data, offset);
            pet.uniqueId = ReadInt32LE(data, offset);
            pet.userId = ReadInt32LE(data, offset);
            pet.skillNum = ReadInt32LE(data, offset);
            
            // 读取技能列表
            for (int j = 0; j < pet.skillNum; ++j) {
                BattleSkill skill;
                skill.id = ReadInt32LE(data, offset);
                skill.pp = ReadInt32LE(data, offset);
                skill.maxPp = ReadInt32LE(data, offset);
                pet.skills.push_back(skill);
            }
            
            // 判断我方/敌方
            bool isMy = (pet.sid == 1 || pet.groupType == 1);
            if (isMy) {
                if (state == 1 || state == 2) 
                    g_currentBattle.myActiveIndex = g_currentBattle.myPets.size();
                g_currentBattle.myPets.push_back(pet);
            } else {
                if (state == 1 || state == 2) 
                    g_currentBattle.otherActiveIndex = g_currentBattle.otherPets.size();
                g_currentBattle.otherPets.push_back(pet);
            }
            
            state = ReadInt32LE(data, offset);
        }
    }
}
```

### 回合结算 (1317122)

```cpp
if (packet.opcode == OPCODE_BATTLE_ROUND) {
    int cmdType = packet.params;  // 命令类型
    
    if (cmdType == 0) {  // 普通回合
        int haveBattle = ReadInt32LE(data, offset);
        if (haveBattle == 1) {
            int atkid = ReadInt32LE(data, offset);    // 攻击者阵营 (1=我方, 2=敌方)
            int skillId = ReadInt32LE(data, offset);  // 使用技能
            int defid = ReadInt32LE(data, offset);    // 防御者阵营
            int miss = ReadInt32LE(data, offset);     // 是否闪避
            
            if (miss == 0) {
                ReadInt32LE(data, offset);  // brust
                int atk_hp = ReadInt32LE(data, offset);   // 攻击者剩余HP
                int def_hp = ReadInt32LE(data, offset);   // 防御者剩余HP
                ReadInt32LE(data, offset);  // rebound_hp
                
                // 更新HP
                if (atkid == 1) {
                    g_currentBattle.myPets[g_currentBattle.myActiveIndex].hp = atk_hp;
                    g_currentBattle.otherPets[g_currentBattle.otherActiveIndex].hp = def_hp;
                } else {
                    g_currentBattle.otherPets[g_currentBattle.otherActiveIndex].hp = atk_hp;
                    g_currentBattle.myPets[g_currentBattle.myActiveIndex].hp = def_hp;
                }
            }
        }
    }
}
```

---

## Body 结构详解

### 战斗开始 Body

```
[state, sid, groupType, hp, maxHp, level, elem, spiritId, uniqueId, userId, skillNum, 
  [skillId, pp, maxPp] * skillNum, ... (循环直到 state=-1), escape]
```

| 偏移 | 字段 | 类型 | 说明 |
|-----|------|------|------|
| 0 | state | int32 | 状态 (0=后备, 1=出战, 2=当前出战, -1=结束) |
| 4 | sid | int32 | 阵营ID (1=我方, 2=敌方) |
| 8 | groupType | int32 | 组类型 |
| 12 | hp | int32 | 当前血量 |
| 16 | maxHp | int32 | 最大血量 |
| 20 | level | int32 | 等级 |
| 24 | elem | int32 | 系别 |
| 28 | spiritId | int32 | 妖怪ID |
| 32 | uniqueId | int32 | 唯一ID |
| 36 | userId | int32 | 玩家ID |
| 40 | skillNum | int32 | 技能数量 |
| 44+ | skills | array | 技能列表 [id, pp, maxPp] * skillNum |
| ... | state | int32 | 下一个妖怪或 -1 结束 |

### 回合结算 Body

```
[haveBattle, atkid, skillId, defid, miss, brust?, atk_hp, def_hp, rebound_hp, ...]
```

| 偏移 | 字段 | 类型 | 说明 |
|-----|------|------|------|
| 0 | haveBattle | int32 | 是否有战斗 (1=有) |
| 4 | atkid | int32 | 攻击者阵营 (1=我方, 2=敌方) |
| 8 | skillId | int32 | 使用技能ID |
| 12 | defid | int32 | 防御者阵营 |
| 16 | miss | int32 | 是否闪避 (0=命中) |
| 20 | brust | int32 | 暴击? |
| 24 | atk_hp | int32 | 攻击者剩余HP |
| 28 | def_hp | int32 | 防御者剩余HP |
| 32 | rebound_hp | int32 | 反弹伤害? |

---