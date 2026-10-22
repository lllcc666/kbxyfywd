#pragma once

#include <windows.h>

// 小游戏总线。
//
// 协议出处：AS3 MsgDoc.as:1012 OP_CLIENT_LITTLE_GAME（send=1184789 / back=1315861），
// 报文构造在 LittlegameControl.as:57/63/78。
//
// params 是 gameType，body 只有两种形态：
//   开始 [0,0,0,0,0,0]（PK 局是 [0,0,0,0,playerid,playerScore]）
//   结束 [4, level-1, score, sessionId, 0, 0]
//
// 开始回包是 (act, result) 二元组，语义见 littlegameView.as:137-150：
//   act == -1 服务端关连接；act == 0 时 result 是 sessionId；
//   act == 1 最好成绩；act == 4 时 result 是结算修为（负数归 0）。
//
// 客户端自己不知道「还能玩几局」——DeepHole.as 里玩满一局就直接派发
// littleGameOver 结算，能不能再开完全由服务端 littleGameStart 事件决定。
// 所以自动化的轮数要么来自 open_ui 查询回来的次数，要么由用户显式指定。

// 发开始包（不等回包）。拿到 sessionId 的模块请自己注册 LITTLE_GAME_BACK 回调。
BOOL SendLittleGameStart(int gameType);

// 发开始包的精简形态：body 只有 4 字节 [0]。
// gameType 10003 的开局包是 n=4 / body=00000000，
// 而 gameType 12 的开局包是 n=24。同一个 opcode 不同 gameType 的 body 长度并不统一，
// 所以两种形态都得留着，别拿一个去套另一个。
BOOL SendLittleGameStartCompact(int gameType);

// 发结束包，6 个 int32。需要回传 sessionId 的局用这个。
// level 传 1 表示服务端拿到 level-1 == 0；score 由玩法决定
// （深度挖掘一局 4 个宝箱，发 4 才能命中 DeepHole.as:172 的 >0 发奖判断）。
BOOL SendLittleGameEnd(int gameType, int level, int score, int sessionId);

// 发结束包，4 个 int32 的紧凑形态：body = [4, level-1, score, sessionId]。
// 知识小天地这类纯挂机局的发包就是 [4,0,0,0]。
BOOL SendLittleGameEndCompact(int gameType, int level, int score, int sessionId);

// 知识小天地（gameType 10003）：开局 → 挂机 holdMs → 结算，循环 roundCount 轮。
// 走后台受管线程，可在 UI 上停止。
void StartKnowledgeGardenAsync(int roundCount, int holdMs);
