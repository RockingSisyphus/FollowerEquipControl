// Applies preference-based score bias for combat inventory items.

#pragma once

#include "PCH.h"

namespace FEC::CombatEquipScoreApplier
{
	// Intentionally large to dominate vanilla scoring in ties/near-ties.
	constexpr float kPreferredBonus = 600.0f;
	constexpr float kPreferredBonusLeft = 400.0f;

	[[nodiscard]] float MaybeApplyPreferredBonus(
		RE::Actor* a_actor,
		RE::TESForm* a_form,
		const RE::BGSEquipSlot* a_slot,
		float a_score);
}
