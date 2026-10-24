#pragma once

#include <cstddef>
#include <cstdint>

namespace PacketProtocol {

constexpr uint16_t MAGIC_NORMAL = 0x5344;
constexpr uint16_t MAGIC_COMPRESSED = 0x5343;

constexpr size_t HEADER_SIZE = 12;

}  // namespace PacketProtocol

namespace Opcode {

constexpr uint32_t ENTER_WORLD = 1314822;
constexpr uint32_t ENTER_SCENE_BACK = 1315395;

constexpr uint32_t BATTLE_START = 1317120;
constexpr uint32_t BATTLE_ROUND_START = 1317121;
constexpr uint32_t BATTLE_ROUND = 1317122;
constexpr uint32_t BATTLE_BUF_DIS = 1317123;
constexpr uint32_t BATTLE_BUF = 1317124;
constexpr uint32_t BATTLE_END = 1317125;
constexpr uint32_t BATTLE_CHANGE_SPIRIT_ROUND = 1317126;
constexpr uint32_t BATTLE_NEWEXP = 1317130;
constexpr uint32_t BATTLE_BUFS = 1317131;
constexpr uint32_t COMBAT_NET_PROBE_REQ = 1317132;
constexpr uint32_t COMBAT_NET_REPORT = 1317133;
constexpr uint32_t BATTLE_WITH = 1317136;
constexpr uint32_t COMBAT_SITE_OPTION = 1316481;
constexpr uint32_t COMBAT_SITE_EFFECT = 1316482;
constexpr uint32_t BATTLE_FSPK = 1317154;
constexpr uint32_t SPIRIT_LEVEL_UP = 1317216;
constexpr uint32_t BATTLE_REQ_ON_SKILL = 1317218;

constexpr uint32_t SPIRIT_EQUIP_ALL_SEND = 1185809;
constexpr uint32_t SPIRIT_EQUIP_ALL_BACK = 1316881;
constexpr uint32_t RESOLVE_SPIRIT_SEND = 1185814;
constexpr uint32_t RESOLVE_SPIRIT_BACK = 1316886;
constexpr uint32_t BATCH_RESOLVE_SPIRIT_SEND = 1185819;
constexpr uint32_t BATCH_RESOLVE_BACK = 1316891;
constexpr uint32_t LINGYU_LIST = SPIRIT_EQUIP_ALL_BACK;
constexpr uint32_t DECOMPOSE_RESPONSE = RESOLVE_SPIRIT_BACK;

constexpr uint32_t MONSTER_LIST = 1318401;

// 副本跳层（跳关）请求/回包。副本跳层与双台谷走同一条协议。
constexpr uint32_t JUMP_LAYER_SEND = 1186180;
constexpr uint32_t JUMP_LAYER_BACK = 1317252;


// 小游戏总线（AS3 MsgDoc.as:1012 OP_CLIENT_LITTLE_GAME，send=1184789 / back=1315861）。
// params=gameType，body 由 gameType 决定（LittlegameControl.as:57/63/78）：
//   开始 [0,0,0,0,0,0]（PK 局 [0,0,0,0,playerid,playerScore]）
//   结束 [4, level-1, score, sessionId, 0, 0]
// 开始回包是 (act, result) 二元组：act==0 时 result 就是 sessionId，act==4 时是结算修为。
// 原值 1185525 在 MsgDoc.as 里根本不存在，此处以 OP_CLIENT_LITTLE_GAME.send 为准。
constexpr uint32_t DEEP_DIG_SEND = 1184789;
constexpr uint32_t DEEP_DIG_BACK = 1315861;
constexpr uint32_t LITTLE_GAME_SEND = DEEP_DIG_SEND;
constexpr uint32_t LITTLE_GAME_BACK = DEEP_DIG_BACK;

// 小游戏 bus 的 params（gameType）。AS3 侧没有枚举表，只能按服务端
// littleGameStart 事件里的 gameid 识别。
constexpr int GAME_TYPE_DEEP_DIG = 12;      // 深度挖掘（DeepHole.as：一局 4 宝箱 / 100 秒）
constexpr int GAME_TYPE_KNOWLEDGE = 10003;  // 知识小天地

constexpr uint32_t DANCE_STAGE_SEND = 1187368;
constexpr uint32_t DANCE_STAGE_BACK = 1318440;
constexpr uint32_t DANCE_ACTIVITY_SEND = 1187375;
constexpr uint32_t DANCE_ACTIVITY_BACK = 1318447;

constexpr uint32_t TRAIN_INFO_SEND = 1184772;
constexpr uint32_t TRAIN_INFO_BACK = 1316142;

constexpr uint32_t USER_TASK_LIST_SEND = 1184777;
constexpr uint32_t USER_TASK_LIST_BACK = 1315849;

// 点击 NPC 后服务端下发的 NPC 任务列表（对话入口）。
// AS3 MsgDoc.OP_CLIENT_REQ_NPCTASKLIST.back：params=npcid，body 首个 int 是 dialogId。
// 普通任务窗口由它拿到首个 dialogId，再由 taskDialogComplete 回 TASK_TALK。
constexpr uint32_t NPCTASKLIST_BACK = 1315858;

// 接受任务（AS3 MsgDoc.OP_CLIENT_REQ_ACCEPTTASK）。
// 服务端在 NPCTASKLIST_BACK 里用 type=1 给出可接任务列表，客户端点「接受」后
// 由 TaskControl.onAcceptTask() 以 params=subtaskId 发送，主线任务必须靠它接上。
constexpr uint32_t ACCEPT_TASK_SEND = 1184774;
constexpr uint32_t ACCEPT_TASK_BACK = 1315846;

// 子任务完成通知（AS3 MsgDoc.OP_CLIENT_REQ_TASKFINISHED.back）。
constexpr uint32_t TASK_FINISHED_BACK = 1315847;

constexpr uint32_t TASK_TALK_SEND = 1184788;
constexpr uint32_t TASK_TALK_BACK = 1315860;
constexpr uint32_t TASK_SINGLE_INFO_SEND = 1184787;
constexpr uint32_t TASK_SINGLE_INFO_BACK = 1315859;
constexpr uint32_t TASK_DAILY_SEND = 1184791;
constexpr uint32_t TASK_DAILY_BACK = 1315863;
// 活动向每日任务上报进度用的另一个协议（AS3 的 MsgDoc.OP_CLIENT_DAILYTASK）。
// 各活动 SWF 在结算后用 sendMessage(OP_CLIENT_DAILYTASK.send, 3, [3,4039001,0])
// 上报，与上面的 TASK_DAILY_SEND(1184791) 不是同一个 opcode，不要混用。
constexpr uint32_t TASK_DAILY_REPORT_SEND = 1184812;
constexpr uint32_t TASK_DAILY_REPORT_BACK = 1315884;
constexpr uint32_t TASK_ARCHIVES_SEND = 1184811;
constexpr uint32_t TASK_ARCHIVES_BACK = 1315883;


constexpr uint32_t COLLECT_STATUS_SEND = 1187106;
constexpr uint32_t COLLECT_STATUS_BACK = 1318178;
constexpr uint32_t CHECK_ACCOUNT_SEND = 1183744;
constexpr uint32_t ENTER_SCENE_SEND = 1184313;
constexpr uint32_t EXIT_WORLD_SEND = 1184002;   // 退出世界回到个人空间（params=userId）

constexpr uint32_t BATTLE_READY = 1186049;
constexpr uint32_t BATTLE_PLAY_OVER = 1186056;
constexpr uint32_t BATTLE_END_SEND = 1186323;
constexpr uint32_t CLICK_NPC = 1186048;

// NPC 状态上报（AS3 MsgDoc.OP_CLIENT_NPC_STATUS，back=1315843）。
// 客户端每加载一个 NPC 精灵都会上报一次（Npc.as:186 派发 REQUESTNPCSTATE），
// 服务端据此推进当前任务。params=npcId，body 为空。
constexpr uint32_t NPC_STATUS_SEND = 1184771;
constexpr uint32_t NPC_STATUS_BACK = 1315843;

// 场景玩家列表（AS3 MsgDoc.as:75 OP_CLIENT_REQ_SCENE_PLAYER_LIST，back=1315086）。
// 进场景之后客户端发的第一条包：0x12123D params=0 空 body。
constexpr uint32_t PLAYER_LIST_SEND = 1184317;
constexpr uint32_t PLAYER_LIST_BACK = 1315086;

// 场景 NPC 列表（AS3 MsgDoc.OP_CLIENT_NPC_LIST，back=1315854）。
// 抓包证实：每次进入新场景，客户端先发 NPC_LIST(params=0, 空 body)，
// 服务端回 body=[count, npcId...]，客户端再对列表里每个 NPC 发 NPC_STATUS。
// 任务推进依赖的 NPC 常是服务端动态生成的任务 NPC（如 21319 不在 npc.xml 里），
// 只能从这条回包里拿到 id，不能靠本地配方猜。
constexpr uint32_t NPC_LIST_SEND = 1184782;
constexpr uint32_t NPC_LIST_BACK = 1315854;

// 场景动作（AS3 MsgDoc.OP_CLIENT_SEND_ACTION）。
// 用法（MapControl.onAction:231）：params=1, body=[userId, actionId, destX, destY, flag, destId]。
// 主线里的「用【粒子光线】射击【封印】」就是这条：actionId 来自服务端下发的法术列表
// （GETMAGICINDEX_BACK），坐标是目标对象在场景里的位置。
constexpr uint32_t SEND_ACTION_SEND = 1184784;
constexpr uint32_t SEND_ACTION_BACK = 1316124;

// 已学法术列表（AS3 MsgDoc.OP_CLIENT_GETMAGICINDEX，back=1316125）。
// 回包 body = magicCount 组 [id, type, state, num]，params=magicCount。
// 主线射击类节点只能从这里取 actionId，本地配置里没有映射。
constexpr uint32_t GET_MAGIC_INDEX_SEND = 1184797;
constexpr uint32_t GET_MAGIC_INDEX_BACK = 1316125;

constexpr uint32_t BATTLE_MD5_CHECK = 1317264;

// 新活动总线（AS3 MsgDoc.as:1580 OP_CLIENT_ACTIVITY_QINGYANG_NEW，send=1185429 / back=1316501）。
// params=活动ID，body="u16 长度 + ASCII 动作名"。动作名表在各活动 SWF 的 Define 类里，
// 例如障碍赛跑 Act810Define.as 有 open_ui / start_game / sweep_info / sweep / reward / exchange。
// 障碍赛跑(810)、金平府祈愿(688)、灵兽采集(754)、坐骑大赛(665) 全部走这一条。
//
// 下面这段是「新活动总线」的权威取值：本文件后面出现的所有同值名字都是指它，
// 只是按模块叫法起的别名（坐骑大赛/采集/草莓…) 。别名直接引用权威常量，
// 保证协议值只在这里写一次——此前同一个魔数被硬编码了 5 遍。
constexpr uint32_t ACTIVITY_ACTION_SEND = 1185429;
constexpr uint32_t ACTIVITY_ACTION_BACK = 1316501;

// 旧活动总线（AS3 MsgDoc.as:1464 OP_CLIENT_ACTIVITY_QINGYANG，send=1184833 / back=1324097）。
// params 是活动子命令码（900/902/325/154/182/142/332…），body 是参数列表。
constexpr uint32_t ACTIVITY_QINGYANG_SEND = 1184833;
constexpr uint32_t ACTIVITY_QINGYANG_BACK = 1324097;

// 下面两个是上面两条活动总线回包的模块别名。定义放在总线之后，
// 因为 constexpr 别名必须引用已声明的常量（此前写在文件前部会编译失败）。
constexpr uint32_t ACTIVITY_QUERY_BACK = ACTIVITY_ACTION_BACK;
constexpr uint32_t TRIAL_BACK = ACTIVITY_QINGYANG_BACK;

// 活动 ID（新总线的 params）与相关场景、NPC
constexpr int ACT_RUN_COMPETITION = 810;   // 障碍赛跑 Act810RunMain
constexpr int ACT_JINPINGFU_WISH = 688;    // 金平府祈愿
constexpr int ACT_GOMOKU = 22003;          // 五子棋大作战
constexpr int SCENE_GOMOKU = 1010;         // 五子棋所在场景
constexpr int NPC_GOMOKU = 18004;          // 五子棋 NPC
constexpr int SCENE_JINPINGFU = 22004;     // 金平府场景

constexpr uint32_t ACTIVITY_QINGYANG_NEW_SEND = ACTIVITY_ACTION_SEND;
constexpr uint32_t ACTIVITY_LUA_V3_SEND = 1185432;
constexpr uint32_t ACTIVITY_LUA_V3_BACK = 1316503;

constexpr uint32_t HORSE_COMPETITION_SEND = ACTIVITY_ACTION_SEND;
constexpr uint32_t HORSE_COMPETITION_BACK = ACTIVITY_ACTION_BACK;
constexpr uint32_t HORSE_GAME_CMD_SEND = ACTIVITY_LUA_V3_SEND;
constexpr int HORSE_COMPETITION_ACT_ID = 665;

constexpr uint32_t HEAVEN_FURUI_SEND = ACTIVITY_QINGYANG_SEND;
constexpr uint32_t HEAVEN_FURUI_BACK = ACTIVITY_QINGYANG_BACK;

constexpr uint32_t SPIRIT_PRESURES_SEND = 1187124;
constexpr uint32_t SPIRIT_PRESURES_BACK = 1330484;
constexpr uint32_t SPIRIT_SEND_SPIRIT_BACK = 1330489;
constexpr uint32_t SPIRIT_PLAYER_INFO_SEND = 1187585;
constexpr uint32_t SPIRIT_PLAYER_INFO_BACK = 1318657;
constexpr uint32_t SPIRIT_COLLECT_BACK = ACTIVITY_ACTION_BACK;
constexpr int SPIRIT_COLLECT_ACT_ID = 754;

constexpr uint32_t BUY_GOODS_SEND = 1183760;
constexpr uint32_t BUY_GOODS_BACK = 1314832;
constexpr uint32_t REQ_PACKAGE_DATA_SEND = 1183761;
constexpr uint32_t REQ_PACKAGE_DATA_BACK = 1314833;
constexpr uint32_t USER_OP_SEND = 1186050;
constexpr uint32_t USE_PROPS_SEND = 1184310;
constexpr uint32_t USE_PROPS_BACK = 1315382;

constexpr uint32_t BATTLESIX_COMBAT_INFO_SEND = 1184260;
constexpr uint32_t BATTLESIX_COMBAT_INFO_BACK = 1315344;
constexpr uint32_t BATTLESIX_MATCH_SEND = 1184262;
constexpr uint32_t BATTLESIX_MATCH_BACK = 1315347;
constexpr uint32_t BATTLESIX_CANCEL_MATCH_SEND = 1184266;
constexpr uint32_t BATTLESIX_CANCEL_MATCH_BACK = 1315351;
constexpr uint32_t BATTLESIX_REQ_START_SEND = 1184268;
constexpr uint32_t BATTLESIX_REQ_START_BACK = 1315353;
constexpr uint32_t BATTLESIX_OTHER_ESCAPED_BACK = 1315354;
constexpr uint32_t BATTLESIX_PREPARE_COMBAT_BACK = 1315355;
constexpr uint32_t BATTLESIX_COMBAT_OVER_BACK = 1315356;
constexpr uint32_t BATTLESIX_SELF_BAN_SPIRIT_BACK = 1315357;
constexpr uint32_t BATTLESIX_FINISH_FIRST_SPIRIT_BACK = 1315358;
// 与通用战斗操作同一条协议，保留别名只是为了调用处读起来贴合六强赛语境。
constexpr uint32_t BATTLESIX_USER_OP_SEND = USER_OP_SEND;

}  // namespace Opcode
