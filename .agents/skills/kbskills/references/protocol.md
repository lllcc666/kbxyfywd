# 协议族速查

## 活动与小游戏

| 协议族 | send | back | AS3 常量名 | 说明 |
|---|---:|---:|---|---|
| 新活动协议 | 1185429 | 1316501 | `OP_CLIENT_ACTIVITY_QINGYANG_NEW` | 多数周活动、部分小游戏 |
| 旧活动协议 | 1184833 | 1324097 | `OP_CLIENT_ACTIVITY_QINGYANG` | 老活动、试炼、部分特殊活动 |
| 小游戏固定 gameType | 1184789 | 1315861 | `OP_CLIENT_LITTLE_GAME` | 深度挖宝等 |

## 常见系统

| 模块 | send | back | AS3 常量名 | 说明 |
|---|---:|---:|---|---|
| 进入世界 | 1183750 | 1314822 | `OP_CLIENT_REQ_ENTER_WORLD` | 登录完成后的世界数据 |
| 灵玉查询 | 1185809 | 1316881 | `OP_CLIENT_SPIRIT_EQUIP_ALL` | 灵玉列表 |
| 灵玉分解 | 1185814 | 1316886 | `OP_RESOLVE_SPIRIT_EQUIP` | 单个分解 |
| 灵玉批量分解 | 1185819 | 1316891 | `OP_BATCH_RESOLVE_SPIRIT_EQUIP` | 批量分解 |
| 妖怪背包 | 1187329 | 1318401 | `OP_CLIENT_REQ_SPIRIT_LIST` | 妖怪列表 |
| 跳舞大赛 | 1187375 | 1318447 | `OP_CLIENT_DANCE_ACTIVITY` | 游戏过程 |

## 使用方式

先用这里判协议族，再去看对应模块文档。
