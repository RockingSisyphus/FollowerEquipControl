// Melee weapon preference decisions for combat equip and unequip.

#pragma once

#include "CombatEquipOverride.h"

#include <optional>

namespace FEC::CombatEquipOverride::Melee
{
	[[nodiscard]] std::optional<Decision> DecideMeleeEquipOverride(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count,
		bool a_aiDriven,
		bool a_drawn);

	[[nodiscard]] std::optional<UnequipDecision> DecideMeleeUnequipOverride(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count,
		bool a_aiDriven);
}
