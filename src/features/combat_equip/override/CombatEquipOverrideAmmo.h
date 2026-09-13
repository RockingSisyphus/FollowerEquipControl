// Ammo preference override decisions for combat equip.

#pragma once

#include "CombatEquipOverride.h"

#include <optional>

namespace FEC::CombatEquipOverride::Ammo
{
	[[nodiscard]] std::optional<Decision> DecideAmmoEquipSwap(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count);

	void OnAmmoUnequip(RE::Actor* a_actor, RE::TESBoundObject* a_object);
}
