# 灵玉系统模块

本文件总结灵玉相关协议和解析模式，不绑定某个项目的代码布局。

## 关键 opcode（AS3 常量名已核实）

- 查询列表 send：`1185809`（`OP_CLIENT_SPIRIT_EQUIP_ALL.send`）
- 查询列表 back：`1316881`（`OP_CLIENT_SPIRIT_EQUIP_ALL.back`）
- 单个分解 send：`1185814`（`OP_RESOLVE_SPIRIT_EQUIP.send`）
- 单个分解 back：`1316886`（`OP_RESOLVE_SPIRIT_EQUIP.back`）
- 批量分解 send：`1185819`（`OP_BATCH_RESOLVE_SPIRIT_EQUIP.send`）
- 批量分解 back：`1316891`（`OP_BATCH_RESOLVE_SPIRIT_EQUIP.back`）

## 常见流程

### 1. 查询灵玉列表

常见做法是分别查询：

- 背包灵玉
- 已装备灵玉

分析时重点确认：

- `params` 是否是 `userId`
- body 是否是 `[1]` / `[2]` 这样的类型数组
- 响应第一字段是否区分背包 / 已装备

### 2. 分解灵玉

重点确认：

- 使用的是 `symmIndex` 还是别的 ID
- 单个分解还是批量分解
- 分解时是否需要阻止并发查询

## 响应结构常见关注点

灵玉列表响应里通常要找：

- backFlag
- 数量
- `symmId`
- `symmIndex`
- 装备妖怪名
- 灵玉类型
- 属性列表

## 分析顺序

1. 找查询发送逻辑。
2. 找 `1316881` 的解析逻辑。
3. 确认 UI 使用的是哪个字段作为分解唯一键。
4. 再看分解响应是否只返回成功码还是会返回更新后的列表。
