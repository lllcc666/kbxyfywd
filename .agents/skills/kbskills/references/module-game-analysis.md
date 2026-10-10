# 小游戏与活动通用分析指南

## 统一分析顺序

1. 明确活动名 / 活动 ID / 现象。
2. 在当前项目中定位已有实现。
3. 定位本地 SWF / AS3 / 现有反编译结果。
4. 先用本机已有 FFDec / JPEXS 导出 AS3，不要默认下载新工具。
5. 再提取 opcode / params / 命令字符串。
6. 再提取 `checkCode` / `point` / `rank` / `reward`。
7. 形成结论和下一步建议。

## 反编译优先级

- 先用本机已有 FFDec / JPEXS / `ffdec-cli` / `ffdec.jar`。
- 本机已有可用工具时，禁止为了分析再下载新工具。
- 用户明确要求不要下载时，严格遵守。

## AS3 优先级

先找：

- `MsgDoc.as`
- `*Define.as`
- `*MessageConst.as`
- `*Data.as`
- `*Model.as`

再找：

- `Control.as`
- `MainView.as`
- `GameView.as`
- `OverView.as`
- `ResultView.as`
- `ExchangeView.as`

## 常见协议族

| 协议族 | send | back | AS3 常量名 |
|---|---:|---:|---|
| 新活动协议 | 1185429 | 1316501 | `OP_CLIENT_ACTIVITY_QINGYANG_NEW` |
| 旧活动协议 | 1184833 | 1324097 | `OP_CLIENT_ACTIVITY_QINGYANG` |
| 小游戏 | 1184789 | 1315861 | `OP_CLIENT_LITTLE_GAME` |
| 跳舞大赛 | 1187375 | 1318447 | `OP_CLIENT_DANCE_ACTIVITY` |
## “无奖励”排查

1. 找结束响应。
2. 看是否只是 `point` / `medal` 更新。
3. 看是否还要 `exchange`。
4. 看是否有每日上限或排名门槛。
5. 仍不清楚就先打原始响应。

## 脚本

### SWF 分析

```bash
python scripts/analyze_swf.py --game 12
python scripts/analyze_swf.py --decompile-game 12
python scripts/analyze_swf.py --main
```

### Opcode 工具

```bash
python scripts/calc_opcode.py
```

当前 `calc_opcode.py` 没有参数化 CLI。
