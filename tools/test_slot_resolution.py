#!/usr/bin/env python3
"""test_slot_resolution.py —— FEC 手部槽位解析（红/绿测试）

问题背景（2026-09-13 现场日志实证）
-----------------------------------
Follower Equip Control 2.1.0 的 equip-mode 路径用 EditorID 查表取手部槽位：

    src/features/equip_mode/core/StrictPickUtil.cpp
        GetLeftHandSlot()  -> TESForm::LookupByEditorID<BGSEquipSlot>("LeftHand")
        GetRightHandSlot() -> TESForm::LookupByEditorID<BGSEquipSlot>("RightHand")

在用户环境里这两个查表返回 **null**。于是 WeaponMode 里

    if (out.slot == Core::GetLeftHandSlot()) ...        // nullptr == nullptr 成立
    const bool isHandSlotItem = (out.slot == GetLeftHandSlot()) || (out.slot == GetRightHandSlot());
    if (!isHandSlotItem) return std::nullopt;           // 误判为 true，不返回

把 `slot == nullptr` 当成"合法手部槽位"，最终

    ActorEquipManager::EquipObject(..., a_slot = nullptr, ...)

引擎收到 null 槽位就回退到主手（右手）。日志实证：
    EquipGate: EquipObject call ... slot=0x0        ← 指针为 null
    CEPR: capture equip ... (OneHandRight) hand=right   ← 永远右手
    左键 3 次 + 右键 3 次，6/6 全部落到右手，左手从未被装备

同样的槽位在别处是**正确**取的（BGSDefaultObjectManager）：
    src/features/combat_equip/scoring/CombatEquipScoreEquipSlotCache.cpp
    src/features/combat_equip/override/CombatEquipOverrideUtil.h
    src/features/equip_suppression/equip/HandItemRestore.cpp

本测试用纯逻辑（无引擎依赖）锁定修复后的契约。
"""

import re
import sys
from pathlib import Path

# Windows runners default stdout to a legacy code page (cp1252) which cannot encode
# the non-ASCII labels used below. Force UTF-8 so the test behaves the same everywhere.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except (AttributeError, ValueError):
    pass

HERE = Path(__file__).resolve().parent
# 可移植：既能在仓库内（CI）运行，也能在本工作区里针对 tmp/fec_src 运行
_REL = "src/features/equip_mode/core/StrictPickUtil.cpp"
_CANDIDATES = [HERE.parent / _REL,                       # 仓库内（CI checkout）
               HERE.parent / "tmp/fec_src" / _REL,       # DSH 工作区
               HERE.parent.parent / _REL]                # 兼容其他层级
SRC = next((c for c in _CANDIDATES if c.exists()), _CANDIDATES[0])
WS = SRC.parents[4] if len(SRC.parents) > 4 else HERE.parent

passed, failed = 0, 0


def check(name, ok, detail=""):
    global passed, failed
    if ok:
        passed += 1
    else:
        failed += 1
    print(f"[{'PASS' if ok else 'FAIL'}] {name}" + (f"  | {detail}" if detail else ""))


# ---------------------------------------------------------------- 纯逻辑模型
# 修复后的解析契约：按优先级依次尝试，返回第一个「有效」槽位；绝不返回 null。
#   1) 默认对象管理器（BGSDefaultObjectManager）—— 全项目其它地方用的就是这条，
#      不依赖 EditorID 字符串表，是引擎的权威引用
#   2) EditorID 查表（保留原有行为作为回退）
#   3) 已知 FormID 硬编码兜底（LeftHand=0x00013F43, RightHand=0x00013F42）
def resolve_slot(kind, dom_lookup, editorid_lookup, byid_lookup):
    """kind: 'left' | 'right'。三个 lookup 都是可调用对象，返回 form 或 None。"""
    for lookup in (dom_lookup, editorid_lookup, byid_lookup):
        form = lookup(kind)
        if is_valid_slot(form):
            return form
    return None


def is_valid_slot(form):
    """有效槽位 = 非 null 且 FormID 非 0（GetFormID()==0 的槽位引擎不会认）。"""
    return form is not None and form.get("formid", 0) != 0


LEFT = {"formid": 0x00013F43, "name": "LeftHand"}
RIGHT = {"formid": 0x00013F42, "name": "RightHand"}


def mk(available):
    """按可用性生成三个 lookup（位置参数顺序：dom, editorid, byid）。"""
    return (
        lambda k: available.get(("dom", k)),
        lambda k: available.get(("editorid", k)),
        lambda k: available.get(("byid", k)),
    )


print("=== 1. 解析优先级与回退（纯逻辑）===")

# 1.1 默认对象管理器可用时优先使用它
s = resolve_slot("left", *mk({("dom", "left"): LEFT}))
check("left: dom 可用时取 dom", s is LEFT, f"{s}")

# 1.2 dom 不可用 → 回退 EditorID
s = resolve_slot("left", *mk({("editorid", "left"): LEFT}))
check("left: dom 不可用时回退 editorid", s is LEFT, f"{s}")

# 1.3 dom + editorid 都不可用 → 回退硬编码 FormID（本次故障场景）
s = resolve_slot("left", *mk({("byid", "left"): LEFT}))
check("left: dom/editorid 都失败时用硬编码 FormID 兜底", s is LEFT, f"{s}")

# 1.4 只有 editorid 返回 FormID 0 的坏槽位 → 必须跳过它继续兜底
bad = {"formid": 0, "name": "LeftHand"}
s = resolve_slot("left", *mk({("editorid", "left"): bad, ("byid", "left"): LEFT}))
check("left: FormID=0 的坏槽位被判为无效并继续兜底", s is LEFT, f"{s}")

# 1.5 三种途径全失败 → 返回 None（调用方必须据此报错，而不是把 null 当有效槽位）
s = resolve_slot("left", *mk({}))
check("left: 全部失败时返回 None（不得伪造槽位）", s is None, f"{s}")

# 1.6 右手同样适用
s = resolve_slot("right", *mk({("byid", "right"): RIGHT}))
check("right: 兜底路径同样生效", s is RIGHT, f"{s}")


print("\n=== 2. 源码必须实现上述回退链（静态检查）===")
src = SRC.read_text(encoding="utf-8") if SRC.exists() else ""
check("源码文件存在", bool(src), str(SRC))


def func_body(text, signature):
    i = text.find(signature)
    if i < 0:
        return ""
    j = text.find("\n\t}", i)
    return text[i:j if j > 0 else i + 600]


resolve = func_body(src, "const RE::BGSEquipSlot* ResolveHandSlot(")
check("ResolveHandSlot 经默认对象管理器取槽位（权威来源，优先）",
      ("BGSDefaultObjectManager" in resolve or "LookupDefaultEquipSlot" in resolve)
      and "kLeftHandEquip" in resolve and "kRightHandEquip" in resolve,
      "解析链第一优先级必须是 kLeftHandEquip / kRightHandEquip")
check("ResolveHandSlot 保留 EditorID 回退", "LookupByEditorID" in resolve)
check("ResolveHandSlot 保留硬编码 FormID 兜底（LeftHand/RightHand）",
      "kLeftHandSlotFormID" in resolve and "kRightHandSlotFormID" in resolve)
check("硬编码 FormID 数值正确（0x00013F43 / 0x00013F42）",
      "0x00013F43" in src and "0x00013F42" in src)
check("无效槽位判定排除 null 与 FormID=0", "IsUsableEquipSlot" in src and "GetFormID() != 0" in src)

for name in ("GetLeftHandSlot", "GetRightHandSlot"):
    body = func_body(src, f"const RE::BGSEquipSlot* {name}()")
    check(f"{name} 统一走 ResolveHandSlot（不再直接查 EditorID）",
          "ResolveHandSlot" in body and "LookupByEditorID" not in body,
          body.strip().replace("\n", " ")[:90])

hdr = (SRC.parent / "StrictPickUtil.h").read_text(encoding="utf-8")
for fn in ("IsLeftHandSlot", "IsRightHandSlot", "IsHandSlot"):
    check(f"头文件导出 {fn}（null 安全判定）", fn in hdr)

print("\n=== 3. null 槽位不得被当成有效手部槽位（静态检查调用点）===")
mode_dir = SRC.parents[1]
bad_compare = re.compile(r"(out|a_slot)->?s?lot\s*==\s*Core::Get(Left|Right)HandSlot\(\)")
offenders = []
for f in sorted(mode_dir.rglob("*.cpp")):
    text = f.read_text(encoding="utf-8")
    for lineno, line in enumerate(text.splitlines(), 1):
        if bad_compare.search(line):
            offenders.append(f"{f.relative_to(mode_dir)}:{lineno}")
check("全模块已无「裸比较槽位」写法（null==null 会误判为合法手部槽位）",
      not offenders, "; ".join(offenders[:5]) if offenders else "")

for f in ("modes/WeaponMode.cpp", "modes/ScrollMode.cpp", "modes/TorchMode.cpp"):
    text = (mode_dir / f).read_text(encoding="utf-8")
    check(f"{f} 使用 Core::IsHandSlot / IsLeftHandSlot 做判定",
          "Core::IsHandSlot(" in text or "Core::IsLeftHandSlot(" in text)

print(f"\n合计 {passed + failed} 项，失败 {failed} 项")
sys.exit(1 if failed else 0)
