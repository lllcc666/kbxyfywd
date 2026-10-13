#!/usr/bin/env python3
"""从 taskinfo.xml 与 taskprop_*.xml 生成主线剧情任务的节点配方表。

背景（与八卦灵盘的区别）：
- 八卦灵盘有 AS3 内置自动模块，执行顺序以 AutoBaGuaView 的状态为准，是固定配方。
- 主线/魔晶战记走普通任务窗口，是服务端驱动：点击 NPC -> 服务端下发 dialogId ->
  客户端回 TASK_TALK(dialogId) -> 服务端下发下一个 dialogId。所以 native 只需要
  「每个 dialogId 该怎么回应」+「子任务进入哪个场景、点哪个 NPC」这两张表。

输入：
    data/taskinfo.xml              任务骨架（子任务 -> 目标场景）
    swf_cache/taskprop_<group>.xml 节点目录（dialogId -> desc/alert/flash/battle/choose）

输出：
    include/activities/story_task_recipe.h

节点 kind 语义（与 TaskXMLParser 的节点分类对应）：
    0 Desc   普通对白，回 TASK_TALK 推进
    1 Alert  纯提示，AS3 不回包，native 也不发包
    2 Flash  过场动画，回 TASK_TALK 推进
    3 Battle 战斗节点，发 CLICK_NPC(sid)
    4 Choose 选项节点（flag 只是按钮编号，不作为发包参数）
    5 Popup  其它弹窗（otherpopup），回 TASK_TALK 推进

选项的 accept flag：taskprop 里 choose 的 flag 语义为 1=接受、2=拒绝、3=继续说。
accept 取「非 2」中优先级最高的一个（1 > 3 > 其它最小值）。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

KIND_DESC = 0
KIND_ALERT = 1
KIND_FLASH = 2
KIND_BATTLE = 3
KIND_CHOOSE = 4
KIND_POPUP = 5


def read_text(path):
    raw = Path(path).read_bytes()
    for enc in ("utf-8", "gbk"):
        try:
            return raw.decode(enc)
        except Exception:
            continue
    return raw.decode("utf-8", errors="replace")


def parse_taskinfo(path):
    """返回 {subtaskId: {'name':str, 'scene':int, 'taskId':int, 'taskName':str}}"""
    text = read_text(path)
    result = {}
    for tm in re.finditer(r'<task id="(\d+)" name="([^"]*)"[^>]*>(.*?)</task>', text, re.S):
        task_id = int(tm.group(1))
        task_name = tm.group(2)
        body = tm.group(3)
        for sm in re.finditer(r'<subtask id="(\d+)" name="([^"]*)"[^>]*>(.*?)</subtask>', body, re.S):
            sub_id = int(sm.group(1))
            scene = 0
            tgt = re.search(r'<targets>\s*<target>(\d+)</target>', sm.group(3))
            if tgt:
                scene = int(tgt.group(1))
            result[sub_id] = {
                "name": sm.group(2),
                "scene": scene,
                "taskId": task_id,
                "taskName": task_name,
            }
    return result


def classify(body):
    has_choose = "<choose" in body
    if "<battle" in body:
        return KIND_BATTLE
    if has_choose:
        return KIND_CHOOSE
    if "<alert" in body:
        return KIND_ALERT
    if "<otherpopup" in body:
        return KIND_POPUP
    if "<flash" in body:
        return KIND_FLASH
    return KIND_DESC


def parse_taskprop(path):
    """返回 [node dict]；节点所属子任务由调用方按 dialogId // 100 回填。"""
    text = read_text(path)
    nodes = []
    for m in re.finditer(r'<dialog id="(\d+)"[^>]*>(.*?)</dialog>', text, re.S):
        dialog_id = int(m.group(1))
        body = m.group(2)
        npc_ids = [int(x) for x in re.findall(r'<desc id="(-?\d+)"', body)]
        npcs = [x for x in npc_ids if x > 0]
        scene = re.search(r'targetScene="(\d+)"', body)
        battle = re.search(r'<battle id="(-?\d+)"', body)
        battle_level = 0
        bl = re.search(r'<battle [^>]*level="(\d+)"', body)
        if bl:
            battle_level = int(bl.group(1))
        nodes.append({
            "dialogId": dialog_id,
            "subtaskId": 0,  # 由调用方按 dialogId // 100 统一回填
            "npc": npcs[0] if npcs else 0,
            "kind": classify(body),
            "targetScene": int(scene.group(1)) if scene else 0,
            "battleId": int(battle.group(1)) if battle else 0,
            "battleLevel": battle_level,
        })
    return nodes


def parse_npc_maps(path):
    """返回 {npcId: mapid}，用于挑出真正停在该子任务场景里的入口 NPC。"""
    text = read_text(path)
    maps = {}
    for m in re.finditer(r'<npc id="(\d+)"[^>]*>(.*?)</npc>', text, re.S):
        mid = re.search(r'<mapid>(\d+)</mapid>', m.group(2))
        if mid:
            maps[int(m.group(1))] = int(mid.group(1))
    return maps


def main():
    info = parse_taskinfo(ROOT / "data" / "taskinfo.xml")
    npc_maps = parse_npc_maps(ROOT / "data" / "npc.xml")
    cache = ROOT / "swf_cache"
    groups = sorted(
        int(m.group(1))
        for p in cache.glob("taskprop_*.xml")
        for m in [re.match(r"taskprop_(\d+)\.xml", p.name)]
        if m
    )
    # 只保留主线段（taskinfo 中 type=2 的剧情任务，组号 = task id）
    mainline = sorted({v["taskId"] for v in info.values() if 2000000 <= v["taskId"] < 2100000})
    groups = [g for g in groups if g in mainline]

    all_nodes = []
    subtask_entries = {}
    for g in groups:
        path = cache / f"taskprop_{g}.xml"
        if not path.exists():
            print(f"跳过（缺文件）: {path.name}", file=sys.stderr)
            continue
        for node in parse_taskprop(path):
            # 节点所属子任务 = dialogId / 100（200100101 -> 2001001）
            node["subtaskId"] = node["dialogId"] // 100
            all_nodes.append(node)

    # 子任务入口：优先挑「就停在该子任务场景里」的 NPC。
    # taskinfo 的 targets 与 taskprop 首个 <desc id> 不总是同一张地图（例如 2002003 目标是双叉岭
    # 2003，中间节点却在五指山 2004），只按 dialogId 取首个 NPC 会点不到人。
    by_sub = {}
    for node in all_nodes:
        by_sub.setdefault(node["subtaskId"], []).append(node)
    for sub_id, nodes in by_sub.items():
        nodes.sort(key=lambda n: n["dialogId"])
        scene = info.get(sub_id, {}).get("scene", 0)
        if scene == 0:
            for n in nodes:
                if n["targetScene"] != 0:
                    scene = n["targetScene"]
                    break
        entry_npc = 0
        for n in nodes:
            if n["npc"] != 0 and npc_maps.get(n["npc"]) == scene:
                entry_npc = n["npc"]
                break
        if entry_npc == 0:
            # 该场景里没有登记过的任务 NPC：退回首个带 NPC 的节点，并在生成日志里点名。
            for n in nodes:
                if n["npc"] != 0:
                    entry_npc = n["npc"]
                    print(
                        f"警告: 子任务 {sub_id} 在场景 {scene} 找不到匹配 NPC，退回 {entry_npc}"
                        f"（其地图为 {npc_maps.get(entry_npc, '?')}）",
                        file=sys.stderr,
                    )
                    break
        subtask_entries[sub_id] = {
            "scene": scene,
            "entryNpc": entry_npc,
            "taskId": info.get(sub_id, {}).get("taskId", 0),
            "name": info.get(sub_id, {}).get("name", ""),
            "taskName": info.get(sub_id, {}).get("taskName", ""),
            "firstDialog": nodes[0]["dialogId"],
        }

    print(f"主线任务组: {groups}")
    print(f"节点数: {len(all_nodes)}  子任务数: {len(subtask_entries)}")

    out = ROOT / "include" / "activities" / "story_task_recipe.h"
    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write("// 本文件由 scripts/gen_story_recipe.py 生成，请勿手工编辑。\n")
        f.write("// 数据来源: data/taskinfo.xml + swf_cache/taskprop_<group>.xml\n")
        f.write("#pragma once\n\n#include <cstdint>\n\n")
        f.write("namespace StoryTaskRecipe {\n\n")
        f.write("// 节点类型\n")
        f.write("enum NodeKind : uint8_t {\n")
        f.write("    KIND_DESC = 0,    // 普通对白：回 TASK_TALK 推进\n")
        f.write("    KIND_ALERT = 1,   // 纯提示：AS3 不回包，native 也不发包\n")
        f.write("    KIND_FLASH = 2,   // 过场动画：回 TASK_TALK 推进\n")
        f.write("    KIND_BATTLE = 3,  // 战斗节点：发 CLICK_NPC(sid)\n")
        f.write("    KIND_CHOOSE = 4,  // 选项节点：flag 只是按钮编号，不作为发包参数\n")
        f.write("    KIND_POPUP = 5,   // 其它弹窗：回 TASK_TALK 推进\n")
        f.write("};\n\n")

        f.write("struct Node {\n")
        f.write("    uint32_t dialogId;\n")
        f.write("    uint32_t subtaskId;\n")
        f.write("    uint32_t npcId;\n")
        f.write("    uint32_t targetScene;\n")
        f.write("    int32_t battleId;     // -1 表示用当前 NPC 作为战斗目标（AS3 哨兵值）\n")
        f.write("    uint32_t battleLevel;\n")
        f.write("    uint8_t kind;\n")
        f.write("};\n\n")

        f.write("struct Subtask {\n")
        f.write("    uint32_t subtaskId;\n")
        f.write("    uint32_t taskId;\n")
        f.write("    uint32_t scene;\n")
        f.write("    uint32_t entryNpc;\n")
        f.write("    uint32_t firstDialog;\n")
        f.write("    const char* taskName;     // UTF-8，任务组名（如 皇城危机）\n")
        f.write("    const char* subtaskName;  // UTF-8，子任务名\n")
        f.write("};\n\n")

        f.write("static const Node NODES[] = {\n")
        for n in sorted(all_nodes, key=lambda x: x["dialogId"]):
            f.write("    {%u, %u, %u, %u, %u, %u, %u},\n" % (
                n["dialogId"], n["subtaskId"], n["npc"], n["targetScene"],
                n["battleId"], n["battleLevel"], n["kind"]))
        f.write("};\n\n")

        f.write("static const Subtask SUBTASKS[] = {\n")
        for sub_id in sorted(subtask_entries):
            e = subtask_entries[sub_id]
            f.write('    {%u, %u, %u, %u, %u, "%s", "%s"},\n' % (
                sub_id, e["taskId"], e["scene"], e["entryNpc"],
                e["firstDialog"], e["taskName"].replace('"', ''), e["name"].replace('"', '')))
        f.write("};\n\n")

        f.write("static const uint32_t NODE_COUNT = sizeof(NODES) / sizeof(NODES[0]);\n")
        f.write("static const uint32_t SUBTASK_COUNT = sizeof(SUBTASKS) / sizeof(SUBTASKS[0]);\n")
        f.write("\n}  // namespace StoryTaskRecipe\n")

    print(f"已生成: {out.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
