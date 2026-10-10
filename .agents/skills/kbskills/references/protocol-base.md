# 基础协议结构

## 封包格式

```text
Magic(2) + Length(2) + Opcode(4) + Params(4) + Body(变长)
```

## 字段说明

- `Magic`
  - `0x5344` = 普通包 `"SD"`
  - `0x5343` = 压缩包 `"SC"`
- `Length`
  - Body 长度，不含 12 字节头部
- `Opcode`
  - 操作码
- `Params`
  - 活动 ID、状态码、子路由参数
- `Body`
  - 具体业务数据

## 常见编码规则

- 数值字段通常是小端序
- `UTF` 字符串通常是 2 字节长度 + 内容
- 数组常见模式是先写长度，再写元素

## 常见协议族

| 协议族 | send | back | AS3 常量名 |
|---|---:|---:|---|
| 新活动协议 | 1185429 | 1316501 | `OP_CLIENT_ACTIVITY_QINGYANG_NEW` |
| 旧活动协议 | 1184833 | 1324097 | `OP_CLIENT_ACTIVITY_QINGYANG` |
| 小游戏 | 1184789 | 1315861 | `OP_CLIENT_LITTLE_GAME` |
| 灵玉 | 1185809 | 1316881 | `OP_CLIENT_SPIRIT_EQUIP_ALL` |
| 妖怪背包 | 1187329 | 1318401 | `OP_CLIENT_REQ_SPIRIT_LIST` |

## 调试建议

1. 先确认 opcode / params。
2. 再确认 body 是 UTF 协议还是纯 int 数组。
3. 最后对照 AS3 的 `readXXX()` 顺序。
