// Best-effort alignment of combat equip attempts to a preferred inventory instance.
// Validates the attempted base item before resolving the preferred instance.

#pragma once

#include "CombatEquipOverride.h"

#include <optional>

namespace FEC::CombatEquipOverride::InstanceAlign
{
	[[nodiscard]] std::optional<Decision> DecideInstanceAlign(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count,
		bool a_aiDriven,
		bool a_drawn);
}
