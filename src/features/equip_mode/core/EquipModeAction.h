// Strict equip-mode actions driven by ContainerMenu selection context.

#pragma once

#include "PCH.h"

#include "Common.h"

namespace FEC::EquipMode::Core
{
	// Uses the selected ContainerMenu row to resolve one actor-owned instance.
	// Falls back to base-only equip for plain stacks, but otherwise fails closed on ambiguity.
	[[nodiscard]] bool PerformEquipModeActionStrictFromMenu(
		RE::ContainerMenu* a_menu,
		RE::Actor* a_target,
		Hand a_hand,
		bool a_equipOnly);

	// Verifies the target actor still owns the exact xList before equipping.
	[[nodiscard]] bool PerformEquipOnlyStrictByXList(
		RE::Actor* a_target,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_xList,
		Hand a_hand);

	// Base-only fallback for post-transfer equip paths where no stable xList can be captured.
	// Does not guarantee instance targeting.
	[[nodiscard]] bool PerformEquipOnlyBase(
		RE::Actor* a_target,
		RE::TESBoundObject* a_object,
		Hand a_hand);

	// Corpse toggle path: bypasses the normal IsDead() guard for armor, weapons, and ammo.
	[[nodiscard]] bool PerformCorpseEquipToggleFromMenu(
		RE::ContainerMenu* a_menu,
		RE::Actor* a_target,
		Hand a_hand);

	// Corpse post-transfer equip path using an xList when available, otherwise base fallback.
	[[nodiscard]] bool PerformCorpseEquipOnly(
		RE::Actor* a_target,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_xList,
		Hand a_hand);
}
