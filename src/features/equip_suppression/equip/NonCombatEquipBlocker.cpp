#include "NonCombatEquipBlocker.h"

#include "ActorScope.h"
#include "ContainerMenuUtil.h"
#include "PluginSettings.h"
#include "WeaponBound.h"

namespace FEC::NonCombatEquipBlocker
{
	bool ShouldSuppressEquip(RE::Actor* a_actor, RE::TESBoundObject* a_object, bool a_aiDriven, bool a_drawn) noexcept
	{
		const auto& settings = PluginSettings::Get();
		if (!settings.autoEquipBlocking.enableNonCombatEquipBlocker) {
			return false;
		}

		// Only suppress vanilla or AI-driven equip calls.
		if (!a_aiDriven) {
			return false;
		}

		// Do not interfere with ContainerMenu trade or equip-mode flows.
		if (ContainerMenuUtil::IsContainerMenuOpen()) {
			return false;
		}

		if (!a_actor || a_actor->IsPlayerRef() || !a_object) {
			return false;
		}

		if (!ActorScope::IsAffectedFollower(a_actor)) {
			return false;
		}

		if (a_drawn || a_actor->IsInCombat()) {
			return false;
		}

		const auto formType = a_object->GetFormType();
		const bool isWeapon = a_object->IsWeapon();
		const bool isShield = [&]() {
			auto* armor = a_object->As<RE::TESObjectARMO>();
			return armor && armor->IsShield();
		}();
		const bool isAmmo = (formType == RE::FormType::Ammo);
		const bool isScroll = (formType == RE::FormType::Scroll);
		if (!isWeapon && !isShield && !isAmmo && !isScroll) {
			return false;
		}

		// Allow refresh equips for already equipped hand items.
		if (isWeapon || isShield || isScroll) {
			const auto* right = a_actor->GetEquippedObject(false);
			const auto* left = a_actor->GetEquippedObject(true);
			if (a_object == right || a_object == left) {
				return false;
			}

			// Bound weapons are transient; do not suppress their equip lifecycle.
			if (isWeapon && WeaponBound::IsWeaponAndBound(a_object)) {
				return false;
			}
		} else {
			if (auto* currentAmmo = a_actor->GetCurrentAmmo(); currentAmmo && currentAmmo == a_object) {
				return false;
			}
		}

		return true;
	}
}
