// Decision logging for combat-equip overrides.

#pragma once

#include "CombatEquipOverride.h"

namespace FEC::CombatEquipOverride::Telemetry
{
	[[nodiscard]] bool IsEnabled() noexcept;

	void LogDecision(
		const Decision& a_decision,
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		const RE::BGSEquipSlot* a_slot,
		bool a_aiDriven);

	void LogAmmoReequipAttempt(RE::Actor* a_actor, RE::FormID a_ammoBaseID, const char* a_reason);
}
