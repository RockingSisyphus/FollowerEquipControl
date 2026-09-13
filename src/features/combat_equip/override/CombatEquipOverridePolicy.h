// Shared eligibility checks for combat-equip override decisions.

#pragma once

#include "PCH.h"

namespace FEC::CombatEquipOverride::Policy
{
	[[nodiscard]] bool ShouldConsiderEquip(
		RE::Actor* a_actor,
		bool a_aiDriven,
		bool a_drawn);

	[[nodiscard]] bool ShouldConsiderUnequip(
		RE::Actor* a_actor,
		bool a_aiDriven);
}
