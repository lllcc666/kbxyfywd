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

# 子任务级前置动作类型（与生成的头文件 PreActionKind 对应）
PRE_CLICK = 0
PRE_SHOOT = 1


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


# 主线里「对场景对象施法」的提示措辞。这类节点不是点一下就好，必须发
# SEND_ACTION(1184784) 才对（AS3 MapControl.onAction:231）：
#   params=1, body=[userId, actionId, destX, destY, flag=1, destId]
# 抓包/日志证实纯点击对这类对象完全无效。
SHOOT_VERBS = ("射击", "射一下", "射射", "粒子光线", "粒子射线", "瞄准")

# alert 正文里的动作措辞。命中才允许「退化为全文找对象名」，避免剧情提示乱点人。
# 只收「对着某个对象做动作」的措辞：移动类（走过去/前往/看看）只说明要去哪儿，
# 正文里出现的名字往往是旁白人物（「快去黑松林看看吧」里没有要点的对象），
# 按名字点会把叮叮博士这类叙述者点掉；「问问/去问」后面跟的才是要找的人。
ACTION_VERBS = ("点击", "点一下", "点它", "点住", "拔", "打开", "对着", "使用", "射击",
                "找到", "问问", "去问", "问一下", "询问")

# 任务提示里的称谓与 npc.xml 的 name 不完全一致，必须显式对齐，不能靠模糊匹配。
# 200100504「瞄准【封印】…射击封印」→ 2003 场景里承受射击的是 21108「发光结界」。
SHOOT_ALIASES = {
    "封印": ("发光结界",),
    "结界": ("发光结界",),
    "陶瓷罐": ("陶罐",),
    "云栈洞": ("妖洞",),
    "八戒": ("猪八戒",),
}

# 【】里除了目标，还会点名**法术名**（【粒子光线】【魔晶射线】），这些不是射击目标。
SHOOT_MAGIC_TOKENS = ("粒子光线", "粒子射线", "魔晶射线", "魔晶光线")

# 法术名 → actionId（SEND_ACTION body 的第二个字段）。只收封包核实过的：
#   2001005：SEND_ACTION(actionId=1)，正文点名【粒子光线】；
#   2002004：SEND_ACTION(actionId=2)，正文点名【魔晶射线】（200200408）。
# actionId 是全局法术 ID，不是账号快捷栏位置，可以静态写死。
MAGIC_NAME_IDS = {"粒子光线": 1, "魔晶射线": 2}

# 「对着【某地】的【对象】使用【法术名】」这类提示：目标有时根本不是场景对象
# （200200408 的【五行阵】在 npc.xml 里不存在），但用的法术是确定的。
CAST_VERBS = ("使用", "施放", "释放", "对着", "射出")

# 单个 alert 最多带多少个候选目标：表越大 native 试得越久，超出部分基本不会再命中。
ALERT_TARGETS_PER_NODE = 12


def find_cast_magic(alert_text):
    """从 alert 正文里找出要用哪门法术，返回 actionId（找不到返回 0）。"""
    if not alert_text:
        return 0
    if not any(v in alert_text for v in CAST_VERBS):
        return 0
    for token in re.findall(r'【([^】]*)】', alert_text):
        if token in MAGIC_NAME_IDS:
            return MAGIC_NAME_IDS[token]
    for name, magic_id in MAGIC_NAME_IDS.items():
        if name in alert_text:
            return magic_id
    return 0


def _expand_shoot_token(token):
    return (token,) + SHOOT_ALIASES.get(token, ())


def find_shoot_target(alert_text, scene, npc_maps):
    """从 alert 正文里找出「要射击的场景对象」。

    返回 (npcId, x, y, special)；找不到返回 (0,0,0,0)。
    规则：正文必须含射击类动词；目标优先取正文里被【…】点名的同场景任务对象
    （「瞄准【双叉岭】大树上的【封印】，使用法术中的【粒子光线】射击封印」里，
    要射的是【封印】，不是先出现的【双叉岭】）。没有【】可依时，退化为正文里
    最后出现的同场景任务对象名。
    """
    if not alert_text or scene == 0:
        return (0, 0, 0, 0)
    if not any(v in alert_text for v in SHOOT_VERBS):
        return (0, 0, 0, 0)

    def pick_from(tokens, exact):
        best_id = 0
        best_pos = -1
        for npc_id, meta in npc_maps.items():
            if meta["mapid"] != scene or meta["istask"] <= 0 or not meta["name"]:
                continue
            for tok in tokens:
                if not tok:
                    continue
                hit = (tok == meta["name"]) if exact else (meta["name"] in tok)
                if not hit:
                    continue
                pos = tok.rfind(meta["name"])
                if pos > best_pos:
                    best_pos = pos
                    best_id = npc_id
        return best_id

    # 【】里点名的才可能是目标，但要先剔除其中的法术名（【粒子光线】不是目标）。
    bracketed = [t for t in re.findall(r'【([^】]*)】', alert_text)
                 if t not in SHOOT_MAGIC_TOKENS]
    expanded = [alias for tok in bracketed for alias in _expand_shoot_token(tok)]
    best_id = pick_from(expanded, exact=True)
    if best_id == 0:
        best_id = pick_from(bracketed, exact=False)
    if best_id == 0:
        best_id = pick_from([alert_text], exact=False)
    if best_id == 0:
        return (0, 0, 0, 0)
    meta = npc_maps[best_id]
    return (best_id, meta["x"], meta["y"], meta["special"])


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
        # alert 的正文里用【…】点名玩家要去点的场景对象，是这类节点唯一可用的
        # 「该点谁」线索（alert 自身没有 desc id，拿不到 npc）。
        alert_match = re.search(r'<alert[^>]*>(.*?)</alert>', body, re.S)
        alert_text = ""
        if alert_match:
            alert_text = re.sub(
                r'<[^>]+>', '',
                re.sub(r'<!\[CDATA\[|\]\]>', '', alert_match.group(1)))
        battle_level = 0
        bl = re.search(r'<battle [^>]*level="(\d+)"', body)
        if bl:
            battle_level = int(bl.group(1))

        # AS3 TaskDialog.spliceView 按 index 逐格推进：同一格上可能同时挂 otherpopup /
        # flash / battle / desc。下面这四项把「这个节点本地要停几次」量化出来，
        # native 才能像 TaskDialog 一样一句一拍，而不是收到 dialogId 就立刻回包。
        #   descCount     = <desc> 数（含带 <choose> 的）
        #   flashMask     = 所有 <flash@position> 的位掩码（第 n 位=1 表示第 n 格是动画）
        #   battleMask    = 所有 <battle@position> 的位掩码
        #   flashPosition = 掩码里最低的那一格（兼容旧字段，-1 = 无动画）
        #   battlePosition= 掩码里最低的那一格（-1 = 无战斗）
        #   afterFlash    = <battle afterflash="1">：同一格先播动画再开打
        # 一个节点可能有多个动画格（7 个节点如此：200100601 是 [0,8,10]，
        # 201000301 是 [8,12]），只存第一个会漏掉后面的动画拍。
        desc_count = len(re.findall(r'<desc ', body))
        flash_positions = [int(x) for x in re.findall(r'<flash [^>]*position="(-?\d+)"', body)]
        battle_positions = [int(x) for x in re.findall(r'<battle [^>]*position="(-?\d+)"', body)]
        flash_mask = 0
        for p in flash_positions:
            if 0 <= p < 32:
                flash_mask |= 1 << p
        battle_mask = 0
        for p in battle_positions:
            if 0 <= p < 32:
                battle_mask |= 1 << p
        after_flash = re.search(r'<battle [^>]*afterflash="1"', body)
        # <otherpopup> 是玩家要点的弹出按钮（开商店/交物品），position 同 desc。
        popup_positions = sorted(
            int(x) for x in re.findall(r'<otherpopup [^>]*position="(-?\d+)"', body))

        nodes.append({
            "dialogId": dialog_id,
            "subtaskId": 0,  # 由调用方按 dialogId // 100 统一回填
            "npc": npcs[0] if npcs else 0,
            "kind": classify(body),
            "targetScene": int(scene.group(1)) if scene else 0,
            "battleId": int(battle.group(1)) if battle else 0,
            "battleLevel": battle_level,
            "alertText": alert_text,
            "descCount": desc_count,
            "flashMask": flash_mask,
            "battleMask": battle_mask,
            "flashPosition": min(flash_positions) if flash_positions else -1,
            "battlePosition": min(battle_positions) if battle_positions else -1,
            "afterFlash": 1 if after_flash else 0,
            "popupPositions": popup_positions,
        })
    return nodes


def parse_npc_maps(path):
    """返回 {npcId: {'mapid': int, 'istask': int, 'name': str}}。

    istask 是 npc.xml 里的任务标记（Npc.as:186 用它给 NPC 加任务状态角标）。
    子任务若在 taskprop 里找不到说话 NPC（如 2001002/2004002 全是自白与 alert），
    就靠 mapid + istask 挑场景里的可点任务对象；name 用于和子任务名做匹配。
    """
    text = read_text(path)
    maps = {}
    for m in re.finditer(r'<npc id="(\d+)"([^>]*)>(.*?)</npc>', text, re.S):
        body = m.group(3)
        mid = re.search(r'<mapid>(\d+)</mapid>', body)
        if not mid:
            continue
        it = re.search(r'<istask>(-?\d+)</istask>', body)
        nm = re.search(r'<name>([^<]*)</name>', body)
        sp = re.search(r'<special>(-?\d+)</special>', body)
        xy = re.search(r'x="(-?[\d.]+)"\s+y="(-?[\d.]+)"', m.group(2))
        maps[int(m.group(1))] = {
            "mapid": int(mid.group(1)),
            "istask": int(it.group(1)) if it else 0,
            "name": nm.group(1) if nm else "",
            "special": int(sp.group(1)) if sp else 0,
            "x": int(float(xy.group(1))) if xy else 0,
            "y": int(float(xy.group(2))) if xy else 0,
        }
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
    #
    # taskprop 里所有 <desc id> 都是 -1/0（自己或选项）时根本取不到说话 NPC，例如
    # 2001002「奇怪的声音」和 2004002「通过大裂口」。它们其实是**点场景对象**触发：
    # 抓包实测 2001002 的入口是 CLICK_NPC(21101)（双叉岭的「大树」，npc.xml 里
    # mapid=2003 istask=1）。这类子任务退回「该场景内 istask==1 且 id 最小的可点任务 NPC」，
    # 并把猜测结果打印出来，需真机复核。
    by_sub = {}
    for node in all_nodes:
        by_sub.setdefault(node["subtaskId"], []).append(node)

    def alert_named_npc(alert_text, scene, allowed_scenes=None):
        """alert 正文点名要去点的场景对象。

        匹配顺序，都要落在「istask>0」的对象上：
          1. 【…】里精确等于同场景对象名（「点击城墙边上的【皇榜】」→ CLICK_NPC(21203)）；
          2. 【…】里的词是同场景对象名的**前缀**（200100308「【城门】那里有人」→
             21202「城门口」）。只认前缀不认任意子串：【长安城】这种地名会命中
             「教师节-长安城马」之类的活动道具，那是乱点；
          3. 上两条在本场景都不成立、但本子任务会去的其它场景里有唯一命中时，取那条，
             运行期先切图再点（200100308 的城门在 2001，而此刻已随 200100304 进了 2002）；
          4. 正文含动作动词时，退化为「正文里最后出现的、同场景的对象名」。
             200100502「去把…大树上的叉子拔下来（点击叉子上面的木牌）」的目标被
             <font> 包着而不是【】，只认【】会把这一步整个漏掉。
        取「最后出现」是因为中文提示层层修饰，越靠后越具体（大树 → 叉子）。
        """
        if not alert_text or scene == 0:
            return 0
        tokens = re.findall(r'【([^】]*)】', alert_text)
        allowed = set(allowed_scenes or ())
        allowed.add(scene)

        def exact_hits(sc):
            return sorted(
                npc_id for npc_id, meta in npc_maps.items()
                if meta["mapid"] == sc and meta["istask"] > 0 and meta["name"] in tokens)

        def prefix_hit(sc):
            for token in reversed(tokens):
                if len(token) < 2:
                    continue
                matched = sorted(
                    npc_id for npc_id, meta in npc_maps.items()
                    if meta["mapid"] == sc and meta["istask"] > 0 and meta["name"]
                    and meta["name"] != token and meta["name"].startswith(token)
                )
                if len(matched) == 1:
                    return matched[0]
            return 0

        hits = exact_hits(scene)
        if hits:
            return hits[0]
        hit = prefix_hit(scene)
        if hit:
            return hit
        # 3 级：只在本子任务会去的场景里找，且全表唯一命中才认。
        other_scenes = sorted(allowed - {scene})
        cross = set()
        for sc in other_scenes:
            cross.update(exact_hits(sc))
        if len(cross) == 1:
            return cross.pop()
        cross_prefix = {prefix_hit(sc) for sc in other_scenes} - {0}
        if len(cross_prefix) == 1:
            return cross_prefix.pop()
        if not any(v in alert_text for v in ACTION_VERBS):
            return 0
        best_id = 0
        best_pos = -1
        for npc_id in sorted(npc_maps):
            meta = npc_maps[npc_id]
            if meta["mapid"] != scene or meta["istask"] <= 0 or not meta["name"]:
                continue
            pos = alert_text.rfind(meta["name"])
            if pos > best_pos:
                best_pos = pos
                best_id = npc_id
        return best_id

    def alert_targets(alert_text):
        """提示正文点到的**全部**场景对象（收集类提示会有好几个，还分散在多个场景）。

        抓包 200200302「在双叉岭，大唐，五指山这几个地方找到唐三藏丢失的四页经书残页吧」：
        真正要去点的是四页经书残页，npc.xml 里叫「经书残页一/二/三/四」，分别落在
        2001/2003/2004，而提示里只写统称「经书残页」。只认【…】精确名会一个都匹配不上，
        于是这一整类「跑腿收集」任务在 native 里退化成点一下就干等。
        判据放宽成「出现即算」，但仍然只认 istask>0 的任务交互物：
          - 对象名整串出现在正文里（「唐三藏」）；
          - 正文里任意 >=3 字的片段是对象名的前缀（「经书残页」→「经书残页一」）。
        """
        if not alert_text:
            return []
        tokens = set(re.findall(r'【([^】]*)】', alert_text))
        scored = []
        for npc_id in sorted(npc_maps):
            meta = npc_maps[npc_id]
            if meta["istask"] <= 0 or not meta["name"] or meta["mapid"] == 0:
                continue
            name = meta["name"]
            # 名字以法术名开头的多半是特效/演出物（「魔晶射线解救沙僧」），
            # 提示里写「使用【魔晶射线】」会把它们当成要跑腿的目标，必须排掉。
            if any(name.startswith(magic) for magic in SHOOT_MAGIC_TOKENS):
                continue
            score = 0
            if name in tokens:
                score = 5
            elif name in alert_text:
                score = 3
            else:
                for length in range(min(len(name), 8), 2, -1):
                    if name[:length] in alert_text:
                        # 前缀越短越可能是地名/绰号撞车（「城门」能对到「城门口」，
                        # 但 3 字前缀也常命中无关物件），只把 4 字以上的当目标。
                        # 4 字以上前缀命中往往就是「同一系列」的收集物（经书残页一/二/三），
                        # 比正文里顺带提到的角色名更具体，所以排在角色名之前。
                        score = 4 if length >= 4 else 0
                        break
            if score:
                scored.append((-score, meta["mapid"], npc_id))
        scored.sort()
        return [(sc, npc) for _, sc, npc in scored]

    def pick_scene_npc(nodes_in_scene, scene, sub_name):
        """挑该场景里该点的任务对象。

        优先级（都由真机抓包/AS3 判定顺序归纳）：
          1. 本子任务的 alert 正文**最后提到**的场景对象。中文提示层层修饰，
             越靠后的越具体：「大树上的叉子」要点的就是叉子（2001005 → 21103），
             而不是先出现的大树（21101，那是 2001002 的入口）。
          2. alert 正文【…】点名、且就在本场景的对象（200100303【皇榜】→ 21203、
             200100309【唐太宗】→ 2002 的 21301）；
          3. taskprop 里有 <desc id> 且该 id 的 mapid 就是本场景（普通说话 NPC）；
          4. 本场景 istask==1 的对象：名字出现在子任务名里优先，否则取最小 id；
          5. 本场景 istask>0 里最小 id。
        """
        if scene == 0:
            return 0
        # 0. 要施法射击的对象优先：200100504「瞄准【封印】…射击封印」→ 21108 发光结界。
        #    射击节点没有可点的说话 NPC，入口就是被射的那个对象。
        for n in nodes_in_scene:
            if n.get("shootNpc"):
                return n["shootNpc"]
        # 1. alert 正文里名字出现位置最靠后的场景对象。
        latest_npc = 0
        latest_pos = -1
        for n in nodes_in_scene:
            text = n.get("alertText", "")
            if not text:
                continue
            for npc_id, meta in npc_maps.items():
                if meta["mapid"] != scene or meta["istask"] <= 0 or not meta["name"]:
                    continue
                pos = text.rfind(meta["name"])
                if pos > latest_pos:
                    latest_pos = pos
                    latest_npc = npc_id
        if latest_npc != 0:
            return latest_npc
        for n in nodes_in_scene:
            if n.get("alertNpc") and npc_maps.get(n["alertNpc"], {}).get("mapid") == scene:
                return n["alertNpc"]
        for n in nodes_in_scene:
            if n["npc"] != 0 and npc_maps.get(n["npc"], {}).get("mapid") == scene:
                return n["npc"]
        cands = sorted(
            npc_id for npc_id, meta in npc_maps.items()
            if meta["mapid"] == scene and meta["istask"] == 1
        )
        if cands:
            named = [npc_id for npc_id in cands
                     if npc_maps[npc_id]["name"] and npc_maps[npc_id]["name"] in sub_name]
            return named[0] if named else cands[0]
        any_cands = sorted(
            npc_id for npc_id, meta in npc_maps.items()
            if meta["mapid"] == scene and meta["istask"] > 0
        )
        return any_cands[0] if any_cands else 0

    def scene_entry_candidates(primary, scene, limit=6):
        """该场景里值得依次尝试的入口对象：首选在前，其余本场景任务对象按 id 补齐。

        真机与抓包共同证明：服务端只对「当前子任务真正的触发对象」回包，
        点其它任何场景对象都完全静默。配方无法从静态数据百分百确定时，
        由 native 按这个顺序逐个点击，收到任意任务回包即停。
        """
        if scene == 0:
            return []
        cands = []
        if primary != 0:
            cands.append(primary)
        for npc_id, meta in sorted(npc_maps.items()):
            if meta["mapid"] != scene or meta["istask"] <= 0:
                continue
            if npc_id in cands:
                continue
            cands.append(npc_id)
            if len(cands) >= limit:
                break
        return cands

    def build_pre_actions(nodes):
        """子任务级前置动作。

        alert 节点是客户端本地展示的，服务端从头到尾不会下发（抓包：200100504 是
        当时的待执行步，全包零下发）。所以「射击封印」「点木牌」这类动作只能进子任务
        后本地主动做，不能挂在「收到该 dialogId 回包」上——那是永远走不到的死分支。
        射击优先于点击，同一目标只留一条。
        """
        actions = []
        seen = set()
        for n in nodes:
            if n.get("shootNpc"):
                key = ("shoot", n["shootNpc"], n.get("actionScene", 0))
                if key not in seen:
                    seen.add(key)
                    # shootSpecial 就是提示里点名的那门法术的 actionId（200100504 的
                    # 【粒子光线】→ 1，抓包 SEND_ACTION 的第二个字段正是 1），
                    # 用它发包比运行期猜「快捷栏第一个法术」可靠。
                    actions.append(("shoot", n["shootNpc"], n.get("shootX", 0),
                                    n.get("shootY", 0), n.get("actionScene", 0),
                                    n.get("shootSpecial", 0)))
            elif n.get("alertNpc"):
                key = ("click", n["alertNpc"], n.get("actionScene", 0))
                if key not in seen:
                    seen.add(key)
                    actions.append(("click", n["alertNpc"], 0, 0,
                                    n.get("actionScene", 0), 0))
        return actions

    for sub_id, nodes in by_sub.items():
        nodes.sort(key=lambda n: n["dialogId"])
        scene = info.get(sub_id, {}).get("scene", 0)
        if scene == 0:
            for n in nodes:
                if n["targetScene"] != 0:
                    scene = n["targetScene"]
                    break
        sub_name = info.get(sub_id, {}).get("name", "")
        # 每个 alert 节点该点哪个场景对象，供 native 在收到该 alert 时补点击。
        # 场景要按节点顺序滚动推进：200100304 的 targetScene=2002 之后，后续节点
        # （含 200100309「【唐太宗】」）都发生在 2002，不能拿子任务起始场景 2001 去配。
        running_scene = scene
        # 本子任务会去到的场景集合（跨场景 alert 目标只允许落在这些场景里）。
        subtask_scenes = {scene}
        subtask_scenes.update(
            n["targetScene"] for n in nodes if n.get("targetScene", 0) != 0)
        for n in nodes:
            n["alertNpc"] = alert_named_npc(
                n.get("alertText", ""), running_scene, subtask_scenes)
            shootNpc, shootX, shootY, shootSpecial = find_shoot_target(
                n.get("alertText", ""), running_scene, npc_maps)
            # 只点名了法术、没点名可点对象的提示（200200408「对着【五指山顶】的
            # 【五行阵】使用【魔晶射线】」）：法术 id 照样进配方，运行期发 SEND_ACTION。
            castMagic = find_cast_magic(n.get("alertText", ""))
            if castMagic != 0:
                shootSpecial = castMagic
            n["shootNpc"] = shootNpc
            n["shootX"] = shootX
            n["shootY"] = shootY
            n["shootSpecial"] = shootSpecial
            if shootNpc != 0 or shootSpecial != 0:
                # 射击/施法节点不再做「点击」推断：正文里的【双叉岭】【大树】都是修饰语，
                # 真正要打的是目标本身，按名字点只会点到旁边的大树。
                n["alertNpc"] = 0
            # 动作发生在「进入下一个 targetScene 之前」，所以用滚动后的当前场景记录，
            # 不能用子任务起始场景（2001003 的【唐太宗】就在 2002 而不是 2001）。
            n["actionScene"] = running_scene
            if n["targetScene"] != 0:
                running_scene = n["targetScene"]
            # alert 点名的对象可能在另一个场景（200100308「【长安城】的【城门】」→ 2001 的
            # 21202，而此刻已按 200100304 的计划进了 2002）。这里记**目标所在的绝对场景**，
            # 由运行期拿真实当前场景比对后决定切不切（同场景 SwitchStoryScene 直接返回，
            # 幂等）；不能记「与计划场景的差值」，否则计划被运行期切图改写后就再也不切了。
            n["alertScene"] = 0
            if n["kind"] == KIND_ALERT and n.get("alertNpc"):
                n["alertScene"] = npc_maps.get(n["alertNpc"], {}).get("mapid", 0)
            # 收集类提示会点名一批对象（「四页经书残页」分散在 2001/2003/2004），
            # 单个 alertNpc 放不下，另存一张多目标表交给 native 逐个试。
            n["alertTargets"] = []
            if n["kind"] == KIND_ALERT and shootNpc == 0 and shootSpecial == 0:
                seen_targets = set()
                for sc, npc_id in alert_targets(n.get("alertText", "")):
                    if (sc, npc_id) in seen_targets:
                        continue
                    seen_targets.add((sc, npc_id))
                    n["alertTargets"].append((sc, npc_id))
                n["alertTargets"] = n["alertTargets"][:ALERT_TARGETS_PER_NODE]
        # 入口 NPC：alert【…】点名的对象优先（如 200100303【皇榜】→ 21203），
        # 其次是本场景的说话 NPC，最后退回场景内可点任务物。
        entry_npc = pick_scene_npc(nodes, scene, sub_name)
        # 入口对象在入口循环里本来就会被点一次，把多目标表里同 id 的目标排到最后，
        # 免得收集阶段一上来就重复点唐三藏，把已经推进过的对白又排一次。
        for n in nodes:
            if n.get("alertTargets"):
                n["alertTargets"].sort(key=lambda t: t[1] == entry_npc)
        # 该子任务依次要走的场景，以及到该场景后要向谁「报到」。
        # 真机链路：2001 点皇榜(21203) → 回 200100304（desc targetScene=2002）→
        # ENTER_SCENE(2002) → NPC_STATUS(21301) → 服务端才下发 200100307。
        # 少了最后这步「报到」，切完场景就一直等不到下一个对话节点。
        stage_scenes = []
        if scene != 0:
            stage_scenes.append(scene)
        for n in nodes:
            if n["targetScene"] != 0 and n["targetScene"] not in stage_scenes:
                stage_scenes.append(n["targetScene"])
        stages = [(sc, pick_scene_npc(nodes, sc, sub_name)) for sc in stage_scenes]
        if entry_npc == 0:
            entry_npc = next((n["npc"] for n in nodes if n["npc"] != 0), 0)
            if entry_npc != 0:
                print(
                    f"警告: 子任务 {sub_id}「{sub_name}」在场景 {scene} 找不到匹配 NPC，"
                    f"退回 {entry_npc}（其地图为 {npc_maps.get(entry_npc, {}).get('mapid', '?')}）",
                    file=sys.stderr,
                )
            else:
                print(
                    f"警告: 子任务 {sub_id}「{sub_name}」场景 {scene} 内无可点任务对象，"
                    f"entryNpc 保持 0",
                    file=sys.stderr,
                )
        subtask_entries[sub_id] = {
            "scene": scene,
            "entryNpc": entry_npc,
            "taskId": info.get(sub_id, {}).get("taskId", 0),
            "name": info.get(sub_id, {}).get("name", ""),
            "taskName": info.get(sub_id, {}).get("taskName", ""),
            "firstDialog": nodes[0]["dialogId"],
            "stages": stages,
            # 每个场景里值得依次尝试的入口对象（首选在前）。服务端只对当前子任务真正的
            # 触发对象回包，点错完全静默，所以 native 用这个表逐个试。
            "entryCandidates": [
                (sc, npc) for sc in stage_scenes
                for npc in scene_entry_candidates(
                    next((n for s, n in stages if s == sc), 0), sc)
            ],
            # 入口对象本来就会被入口循环点一次，同目标的「点击」前置动作要去掉，
            # 否则同一秒内连发两次 CLICK_NPC，服务端会入队两条对白。
            # 只保留发生在子任务本场景的动作：运行期只在进图后跑一次前置动作，
            # 记在别的场景的行永远匹配不上，留在表里只会误导阅读。
            "preActions": [
                a for a in build_pre_actions(nodes)
                if a[4] == scene and (a[0] == "shoot" or a[1] != entry_npc)
            ],
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
        f.write("    uint32_t alertNpc;    // alert 正文【…】点名的场景对象（0=无）\n")
        f.write("    uint32_t shootNpc;    // 需施法射击的场景对象（0=无）\n")
        f.write("    uint32_t shootX;      // 射击目标场景坐标 X\n")
        f.write("    uint32_t shootY;      // 射击目标场景坐标 Y\n")
        f.write("    uint32_t shootSpecial;// 目标 special（必须与法术 actionId 对应）\n")
        f.write("    uint32_t alertScene;  // alert 点击目标所在场景（0=未知；与当前场景不同就先切图）\n")
        f.write("    // ── 本地推进节奏（AS3 TaskDialog.spliceView 的 index 语义）──\n")
        f.write("    // 一个 dialog 节点里的多句 desc 是客户端本地逐句点完的，末尾才发一次 TASK_TALK，\n")
        f.write("    // 所以 native 必须知道「这个节点本地要停几拍」，否则会一跳一发、多发对话。\n")
        f.write("    uint32_t descCount;     // <desc> 条数（含带 <choose> 的），0 = 无本地对白\n")
        f.write("    uint32_t flashMask;     // 动画格位掩码：第 n 位=1 表示第 n 格播动画\n")
        f.write("    uint32_t battleMask;    // 战斗格位掩码：第 n 位=1 表示第 n 格开战\n")
        f.write("    int32_t flashPosition;  // 掩码里最低的动画格（-1 = 无动画）\n")
        f.write("    int32_t battlePosition; // 掩码里最低的战斗格（-1 = 无战斗）\n")
        f.write("    uint8_t afterFlash;     // battle@afterflash=1：同格先播动画再开打\n")
        f.write("};\n\n")

        f.write("// 子任务依次要走的场景，以及每个场景里该交互的任务对象。\n")
        f.write("// 2001003 真机链路：2001 点皇榜(21203) -> 节点 200100304 带 targetScene=2002\n")
        f.write("// -> 到 2002 向唐太宗(21301)报到，服务端才继续下发 200100307。\n")
        f.write("struct Stage {\n")
        f.write("    uint32_t subtaskId;\n")
        f.write("    uint32_t scene;\n")
        f.write("    uint32_t npc;\n")
        f.write("};\n\n")

        f.write("// 子任务在某场景里值得依次尝试的入口对象（首选在前）。\n")
        f.write("// 服务端只对「当前子任务真正的触发对象」回包，点其它场景对象完全静默\n")
        f.write("// （抓包：2001003 点皇榜 21203 立刻回包；日志：2001005 点大树 21101 零回包），\n")
        f.write("// 所以 native 按本表逐个点击，收到任意任务回包即停，不再单点猜测。\n")
        f.write("struct EntryCandidate {\n")
        f.write("    uint32_t subtaskId;\n")
        f.write("    uint32_t scene;\n")
        f.write("    uint32_t npc;\n")
        f.write("};\n\n")

        f.write("// 子任务级前置动作。alert 节点服务端不会下发（抓包 200100504 全包零下发），\n")
        f.write("// 必须进子任务后主动做完，不能挂在「收到该 dialogId 回包」上。\n")
        f.write("enum PreActionKind : uint8_t {\n")
        f.write("    PRE_CLICK = 0,  // CLICK_NPC(npc)\n")
        f.write("    PRE_SHOOT = 1,  // GET_MAGIC_INDEX + SEND_ACTION(magic, x, y, npc)\n")
        f.write("};\n\n")
        f.write("struct PreAction {\n")
        f.write("    uint32_t subtaskId;\n")
        f.write("    uint32_t scene;\n")
        f.write("    uint32_t kind;   // PreActionKind\n")
        f.write("    uint32_t npc;    // 目标任务对象\n")
        f.write("    uint32_t x;      // 施法坐标（PRE_SHOOT 用）\n")
        f.write("    uint32_t y;\n")
        f.write("    uint32_t magic;  // 指定法术 id（0 = 取 GET_MAGIC_INDEX 返回的第一个）\n")
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
            f.write("    {%u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %u, %d, %d, %u},\n" % (
                n["dialogId"], n["subtaskId"], n["npc"], n["targetScene"],
                n["battleId"], n["battleLevel"], n["kind"],
                n.get("alertNpc", 0),
                n.get("shootNpc", 0), n.get("shootX", 0),
                n.get("shootY", 0), n.get("shootSpecial", 0),
                n.get("alertScene", 0),
                n.get("descCount", 0), n.get("flashMask", 0),
                n.get("battleMask", 0), n.get("flashPosition", -1),
                n.get("battlePosition", -1), n.get("afterFlash", 0)))
        f.write("};\n\n")

        # alert 的多目标表：收集类提示点名的对象有好几个、还分散在多个场景，
        # native 按表逐个切图点过去，谁先有回包就停（点错完全静默）。
        alert_rows = []
        for n in sorted(all_nodes, key=lambda x: x["dialogId"]):
            for sc, npc_id in n.get("alertTargets", []):
                alert_rows.append((n["dialogId"], sc, npc_id))
        if not alert_rows:
            alert_rows.append((0, 0, 0))  # 占位，避免生成零长数组
        f.write("struct AlertTarget {\n")
        f.write("    uint32_t dialogId;\n")
        f.write("    uint32_t scene;\n")
        f.write("    uint32_t npc;\n")
        f.write("};\n\n")
        f.write("static const AlertTarget ALERT_TARGETS[] = {\n")
        for dialog_id, sc, npc_id in alert_rows:
            f.write("    {%u, %u, %u},\n" % (dialog_id, sc, npc_id))
        f.write("};\n\n")

        f.write("static const Stage STAGES[] = {\n")
        for sub_id in sorted(subtask_entries):
            for sc, npc in subtask_entries[sub_id]["stages"]:
                f.write("    {%u, %u, %u},\n" % (sub_id, sc, npc))
        f.write("};\n\n")

        f.write("static const EntryCandidate ENTRY_CANDIDATES[] = {\n")
        for sub_id in sorted(subtask_entries):
            for sc, npc in subtask_entries[sub_id]["entryCandidates"]:
                f.write("    {%u, %u, %u},\n" % (sub_id, sc, npc))
        f.write("};\n\n")

        f.write("static const PreAction PRE_ACTIONS[] = {\n")
        for sub_id in sorted(subtask_entries):
            entry = subtask_entries[sub_id]
            for kind, npc, ax, ay, ascene, magic in entry.get("preActions", []):
                f.write("    {%u, %u, %u, %u, %u, %u, %u},\n" % (
                    sub_id, ascene,
                    PRE_SHOOT if kind == "shoot" else PRE_CLICK,
                    npc, ax, ay, magic))
        f.write("};\n\n")

        f.write("static const Subtask SUBTASKS[] = {\n")
        for sub_id in sorted(subtask_entries):
            e = subtask_entries[sub_id]
            f.write('    {%u, %u, %u, %u, %u, "%s", "%s"},\n' % (
                sub_id, e["taskId"], e["scene"], e["entryNpc"],
                e["firstDialog"], e["taskName"].replace('"', ''), e["name"].replace('"', '')))
        f.write("};\n\n")

        f.write("static const uint32_t NODE_COUNT = sizeof(NODES) / sizeof(NODES[0]);\n")
        f.write("static const uint32_t ALERT_TARGET_COUNT =\n")
        f.write("    sizeof(ALERT_TARGETS) / sizeof(ALERT_TARGETS[0]);\n")
        f.write("static const uint32_t STAGE_COUNT = sizeof(STAGES) / sizeof(STAGES[0]);\n")
        f.write("static const uint32_t ENTRY_CANDIDATE_COUNT =\n")
        f.write("    sizeof(ENTRY_CANDIDATES) / sizeof(ENTRY_CANDIDATES[0]);\n")
        f.write("static const uint32_t PRE_ACTION_COUNT =\n")
        f.write("    sizeof(PRE_ACTIONS) / sizeof(PRE_ACTIONS[0]);\n")
        f.write("static const uint32_t SUBTASK_COUNT = sizeof(SUBTASKS) / sizeof(SUBTASKS[0]);\n")
        f.write("\n}  // namespace StoryTaskRecipe\n")

    print(f"已生成: {out.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
