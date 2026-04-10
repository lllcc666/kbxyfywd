#pragma once

#define KB_PACKET_PROTOCOL_SHARED 1

#include <cstddef>
#include <cstdint>

namespace PacketProtocol {

constexpr uint16_t MAGIC_NORMAL = 0x5344;
constexpr uint16_t MAGIC_COMPRESSED = 0x5343;

constexpr size_t HEADER_SIZE = 12;
constexpr size_t MIN_PACKET_SIZE = HEADER_SIZE;

}  // namespace PacketProtocol

namespace Opcode {

constexpr uint32_t ENTER_WORLD = 1314822;
constexpr uint32_t ENTER_SCENE_BACK = 1315395;

constexpr uint32_t BATTLE_START = 1317120;
constexpr uint32_t BATTLE_ROUND_START = 1317121;
constexpr uint32_t BATTLE_ROUND = 1317122;
constexpr uint32_t BATTLE_END = 1317125;
constexpr uint32_t BATTLE_CHANGE_SPIRIT_ROUND = 1317126;

constexpr uint32_t SPIRIT_EQUIP_ALL_SEND = 1185809;
constexpr uint32_t SPIRIT_EQUIP_ALL_BACK = 1316881;
constexpr uint32_t RESOLVE_SPIRIT_SEND = 1185814;
constexpr uint32_t RESOLVE_SPIRIT_BACK = 1316886;
constexpr uint32_t BATCH_RESOLVE_SPIRIT_SEND = 1185819;
constexpr uint32_t BATCH_RESOLVE_BACK = 1316891;
constexpr uint32_t LINGYU_LIST = SPIRIT_EQUIP_ALL_BACK;
constexpr uint32_t DECOMPOSE_RESPONSE = RESOLVE_SPIRIT_BACK;

constexpr uint32_t MONSTER_LIST = 1318401;

constexpr uint32_t DEEP_DIG_BACK = 1315861;
constexpr uint32_t ACTIVITY_QUERY_BACK = 1316501;

constexpr uint32_t PLAYER_ENTER_SCENE = 1315075;
constexpr uint32_t PLAYER_LEAVE_SCENE = 1315328;
constexpr uint32_t PLAYER_MOVING = 1315076;
constexpr uint32_t CHAT_MESSAGE = 1315083;
constexpr uint32_t FAMILY_CHAT = 1316376;
constexpr uint32_t BEIBEI_HEAL_SEND = 1186818;

constexpr uint32_t DANCE_STAGE_SEND = 1187368;
constexpr uint32_t DANCE_STAGE_BACK = 1318440;
constexpr uint32_t DANCE_ACTIVITY_SEND = 1187375;
constexpr uint32_t DANCE_ACTIVITY_BACK = 1318447;

constexpr uint32_t TRAIN_INFO_SEND = 1184772;
constexpr uint32_t TRAIN_INFO_BACK = 1316142;

constexpr uint32_t USER_TASK_LIST_SEND = 1184777;
constexpr uint32_t USER_TASK_LIST_BACK = 1315849;

constexpr uint32_t TASK_TALK_SEND = 1184788;
constexpr uint32_t TASK_TALK_BACK = 1315860;
constexpr uint32_t TASK_SINGLE_INFO_SEND = 1184787;
constexpr uint32_t TASK_SINGLE_INFO_BACK = 1315859;
constexpr uint32_t TASK_DAILY_SEND = 1184791;
constexpr uint32_t TASK_DAILY_BACK = 1315863;
constexpr uint32_t TASK_ARCHIVES_SEND = 1184811;
constexpr uint32_t TASK_ARCHIVES_BACK = 1315883;

constexpr uint32_t MALL_SEND = 1184833;
constexpr uint32_t MALL_BACK = 1316515;
constexpr uint32_t TRIAL_BACK = 1324097;

constexpr uint32_t COLLECT_STATUS_SEND = 1187106;
constexpr uint32_t COLLECT_STATUS_BACK = 1318178;
constexpr uint32_t CHECK_ACCOUNT_SEND = 1183744;
constexpr uint32_t ENTER_SCENE_SEND = 1184313;

constexpr uint32_t BATTLE_READY = 1186049;
constexpr uint32_t BATTLE_LOOK_READY = 1186233;
constexpr uint32_t BATTLE_PLAY_OVER = 1186056;
constexpr uint32_t TOWER_BATTLE_START = 1184788;
constexpr uint32_t CLICK_NPC = 1186048;

constexpr uint32_t BATTLE_MD5_CHECK = 1317264;
constexpr uint32_t BATTLE_MD5_SEND = 1186193;
constexpr uint32_t BATTLE_MD5_FAIL = 1317266;

constexpr uint32_t ACTIVITY_QINGYANG_NEW_SEND = 1185429;
constexpr uint32_t ACTIVITY_LUA_V3_SEND = 1185432;
constexpr uint32_t ACTIVITY_LUA_V3_BACK = 1316503;
constexpr uint32_t STRAWBERRY_SEND = 1185429;
constexpr uint32_t STRAWBERRY_BACK = 1316501;

constexpr uint32_t HORSE_COMPETITION_SEND = 1185429;
constexpr uint32_t HORSE_COMPETITION_BACK = 1316501;
constexpr uint32_t HORSE_GAME_CMD_SEND = 1185432;
constexpr int HORSE_COMPETITION_ACT_ID = 665;

constexpr uint32_t HEAVEN_FURUI_SEND = 1184833;
constexpr uint32_t HEAVEN_FURUI_BACK = 1324097;

constexpr uint32_t SPIRIT_PRESURES_SEND = 1187124;
constexpr uint32_t SPIRIT_PRESURES_BACK = 1330484;
constexpr uint32_t SPIRIT_SEND_SPIRIT_SEND = 1187129;
constexpr uint32_t SPIRIT_SEND_SPIRIT_BACK = 1330489;
constexpr uint32_t SPIRIT_PLAYER_INFO_SEND = 1187585;
constexpr uint32_t SPIRIT_PLAYER_INFO_BACK = 1318657;
constexpr uint32_t SPIRIT_COLLECT_SEND = 1185429;
constexpr uint32_t SPIRIT_COLLECT_BACK = 1316501;
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
constexpr uint32_t BATTLESIX_PREPARE_COMBAT_BACK = 1315355;
constexpr uint32_t BATTLESIX_BATTLE_START_BACK = 1317120;
constexpr uint32_t BATTLESIX_BATTLE_ROUND_START_BACK = 1317121;
constexpr uint32_t BATTLESIX_BATTLE_ROUND_RESULT_BACK = 1317122;
constexpr uint32_t BATTLESIX_BATTLE_END_BACK = 1317125;
constexpr uint32_t BATTLESIX_USER_OP_SEND = 1186050;

}  // namespace Opcode
