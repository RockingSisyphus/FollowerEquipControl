// Ephemeral per-actor combat-equip override state.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <optional>

namespace FEC::CombatEquipOverride::State
{
	// Ammo override bookkeeping.
	void RememberLastAmmoOverride(RE::FormID a_actorID, RE::FormID a_ammoBaseID);
	[[nodiscard]] std::optional<RE::FormID> GetLastAmmoOverride(RE::FormID a_actorID);
	
	// Clear all ammo overrides for save/load boundaries.
	void Clear();
	
	// Erase ammo override state for an actor leaving the world.
	void EraseActor(RE::FormID a_actorID);
}
