#include "CombatEquipOverrideTelemetry.h"

namespace FEC::CombatEquipOverride::Telemetry
{
	bool IsEnabled() noexcept
	{
		return true;
	}

	void LogDecision(
		const Decision& a_decision,
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		const RE::BGSEquipSlot* a_slot,
		bool a_aiDriven)
	{
		if (!IsEnabled() || !spdlog::should_log(spdlog::level::trace)) {
			return;
		}

		const auto actorID = a_actor ? a_actor->GetFormID() : 0;
		const auto objID = a_object ? a_object->GetFormID() : 0;
		const auto prefID = a_decision.preferred.item ? a_decision.preferred.item->GetFormID() : 0;

		logger::trace(
			"Override: decision action={} reason={} aiDriven={} actor={:08X} obj={:08X} pref={:08X} slot={:p}",
			static_cast<std::uint32_t>(a_decision.action),
			a_decision.reason ? a_decision.reason : "",
			a_aiDriven,
			actorID,
			objID,
			prefID,
			static_cast<const void*>(a_slot));
	}

	void LogAmmoReequipAttempt(RE::Actor* a_actor, RE::FormID a_ammoBaseID, const char* a_reason)
	{
		if (!IsEnabled() || !spdlog::should_log(spdlog::level::trace)) {
			return;
		}

		const auto actorID = a_actor ? a_actor->GetFormID() : 0;

		// Gather current ammo slot info for diagnostics.
		RE::FormID currentAmmoID = 0;
		if (a_actor) {
			auto* cur = a_actor->GetCurrentAmmo();
			if (cur) {
				currentAmmoID = cur->GetFormID();
			}
		}

		logger::trace(
			"Override: ammo_reequip actor={:08X} ammo={:08X} reason={} currentAmmo={:08X}",
			actorID,
			a_ammoBaseID,
			a_reason ? a_reason : "",
			currentAmmoID);
	}
}
