#include "CombatEquipOverridePolicy.h"

#include "PluginSettings.h"
#include "ContainerMenuUtil.h"

#include "ActorScope.h"

namespace FEC::CombatEquipOverride::Policy
{
	bool ShouldConsiderEquip(RE::Actor* a_actor, bool a_aiDriven, bool a_drawn)
	{
		if (!a_aiDriven) {
			return false;
		}
		if (!a_actor) {
			return false;
		}
		if (ContainerMenuUtil::IsContainerMenuOpen()) {
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("CombatOverride: skip equip actor {:08X} reason=container_menu_open", a_actor->GetFormID());
			}
			return false;
		}
		if (!ActorScope::IsAffectedFollower(a_actor)) {
			return false;
		}

		if (!a_drawn && !a_actor->IsInCombat()) {
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("CombatOverride: skip equip actor {:08X} reason=not_drawn_not_combat", a_actor->GetFormID());
			}
			return false;
		}

		return true;
	}

	bool ShouldConsiderUnequip(RE::Actor* a_actor, bool a_aiDriven)
	{
		const auto& ce = PluginSettings::Get().combatEquipEnforcement;
		if (!ce.enableAmmoPreference && !ce.enableMeleeEnforcement) {
			return false;
		}
		if (!a_aiDriven) {
			return false;
		}
		if (!a_actor) {
			return false;
		}
		if (ContainerMenuUtil::IsContainerMenuOpen()) {
			return false;
		}
		if (!ActorScope::IsAffectedFollower(a_actor)) {
			return false;
		}

		return true;
	}
}
