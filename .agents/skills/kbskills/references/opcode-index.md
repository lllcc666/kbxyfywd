# Opcode 索引

## 按模块快速定位

| Opcode / 范围 | AS3 常量名 | 模块 | 首选参考 |
|---|---|---|---|
| 1183750 / 1314822 | `OP_CLIENT_REQ_ENTER_WORLD` | 进入世界 | `module-enter-world.md` |
| 1184789 / 1315861 | `OP_CLIENT_LITTLE_GAME` | 小游戏 | `module-daily-task.md`, `module-little-game.md` |
| 1184833 / 1324097 | `OP_CLIENT_ACTIVITY_QINGYANG` | 旧活动 / 试炼 | `protocol.md`, `module-trial.md` |
| 1185429 / 1316501 | `OP_CLIENT_ACTIVITY_QINGYANG_NEW` | 新活动协议 | `protocol.md`, `module-game-analysis.md` |
| 1185809 / 1316881 | `OP_CLIENT_SPIRIT_EQUIP_ALL` | 灵玉查询 | `module-lingyu.md` |
| 1185814 / 1316886 | `OP_RESOLVE_SPIRIT_EQUIP` | 灵玉分解 | `module-lingyu.md` |
| 1187329 / 1318401 | `OP_CLIENT_REQ_SPIRIT_LIST` | 妖怪背包 | `module-monster-bag.md` |
| 1317120~1317126 | `OP_GATEWAY_BATTLE_*` | 战斗响应 | `module-battle.md` |
## 常见搜索方式

```powershell
rg "1314822|1185429|1316501|1185809|1316881" --type as src/
rg "open_ui|start_game|end_game|exchange" --type as src/
```

先定位协议族，再去对应模块文档。
