#include "CombatEquipOverride.h"

#include "CombatEquipOverrideAmmo.h"
#include "InstanceAlign.h"
#include "CombatEquipOverrideMelee.h"
#include "CombatEquipOverridePolicy.h"
#include "CombatEquipOverrideTelemetry.h"
#include "CombatEquipOverrideUtil.h"

#include "EquipGate.h"
#include "PluginSettings.h"

namespace FEC::CombatEquipOverride
{
	bool IsEnabled() noexcept
	{
		const auto& enf = PluginSettings::Get().combatEquipEnforcement;
		const auto& pref = PluginSettings::Get().combatEquipPreference;
		return enf.enableAmmoPreference || enf.enableMeleeEnforcement || pref.enableInstanceAlign;
	}

	std::optional<Decision> DecideEquipObject(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count,
		bool a_aiDriven,
		bool a_drawn)
	{
		if (!Policy::ShouldConsiderEquip(a_actor, a_aiDriven, a_drawn)) {
			return std::nullopt;
		}
		if (!a_object) {
			return std::nullopt;
		}

		if (PluginSettings::Get().combatEquipPreference.enableInstanceAlign) {
			if (auto d = InstanceAlign::DecideInstanceAlign(a_actor, a_object, a_extraData, a_slot, a_count, a_aiDriven, a_drawn); d.has_value()) {
				Telemetry::LogDecision(*d, a_actor, a_object, a_slot, a_aiDriven);
				return d;
			}
		}

		const auto cls = Util::ClassifyEquipClass(a_object);
		if (spdlog::should_log(spdlog::level::trace)) {
			logger::trace("CombatOverride: actor {:08X} AI equip {:08X} class={}",
				a_actor->GetFormID(), a_object->GetFormID(), static_cast<int>(cls));
		}

		if (cls == EquipClass::kAmmo && PluginSettings::Get().combatEquipEnforcement.enableAmmoPreference) {
			if (auto d = Ammo::DecideAmmoEquipSwap(a_actor, a_object, a_extraData, a_slot, a_count); d.has_value()) {
				Telemetry::LogDecision(*d, a_actor, a_object, a_slot, a_aiDriven);
				return d;
			}
			return std::nullopt;
		}

		if (cls == EquipClass::kMelee && PluginSettings::Get().combatEquipEnforcement.enableMeleeEnforcement) {
			if (auto d = Melee::DecideMeleeEquipOverride(a_actor, a_object, a_extraData, a_slot, a_count, a_aiDriven, a_drawn); d.has_value()) {
				Telemetry::LogDecision(*d, a_actor, a_object, a_slot, a_aiDriven);
				return d;
			}
			return std::nullopt;
		}

		// Non-melee queued equips can leave IsRangedWeaponEquipped() false long enough for
		// the melee unequip guard to block vanilla. Non-shield armor does not use hand slots.
		if (cls == EquipClass::kUnknown && !Util::IsNonShieldArmor(a_object) &&
			PluginSettings::Get().combatEquipEnforcement.enableMeleeEnforcement) {
			auto* rightForm = a_actor->GetEquippedObject(false);
			auto* rightObj = rightForm ? rightForm->As<RE::TESBoundObject>() : nullptr;
			auto* leftForm = a_actor->GetEquippedObject(true);
			auto* leftObj = leftForm ? leftForm->As<RE::TESBoundObject>() : nullptr;

			const bool rightIsMelee = rightObj && rightObj != a_object &&
				Util::ClassifyEquipClass(rightObj) == EquipClass::kMelee;
			const bool leftIsMelee = leftObj && leftObj != a_object &&
				Util::ClassifyEquipClass(leftObj) == EquipClass::kMelee;

			if (rightIsMelee || leftIsMelee) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("CombatOverride: actor {:08X} non-melee {:08X} pre-clear melee right={} left={}",
						a_actor->GetFormID(), a_object->GetFormID(), rightIsMelee, leftIsMelee);
				}
				EquipGate::ScopedBypass gateBypass;
				if (rightIsMelee) {
					Util::ForceUnequipRight(a_actor);
				}
				if (leftIsMelee) {
					Util::ForceUnequipLeft(a_actor);
				}
			}
		}

		return std::nullopt;
	}

	std::optional<UnequipDecision> DecideUnequipObject(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count,
		bool a_aiDriven)
	{
		if (!Policy::ShouldConsiderUnequip(a_actor, a_aiDriven)) {
			return std::nullopt;
		}
		if (!a_object) {
			return std::nullopt;
		}

		(void)a_extraData;
		(void)a_count;

		const auto cls = Util::ClassifyEquipClass(a_object);

		if (cls == EquipClass::kMelee && PluginSettings::Get().combatEquipEnforcement.enableMeleeEnforcement) {
			if (auto d = Melee::DecideMeleeUnequipOverride(a_actor, a_object, a_extraData, a_slot, a_count, a_aiDriven); d.has_value()) {
				return d;
			}
		}

		return std::nullopt;
	}

	void OnUnequipObject(RE::Actor* a_actor, RE::TESBoundObject* a_object, bool a_aiDriven)
	{
		if (!Policy::ShouldConsiderUnequip(a_actor, a_aiDriven)) {
			return;
		}
		if (!a_object) {
			return;
		}

		if (a_object->GetFormType() == RE::FormType::Ammo && PluginSettings::Get().combatEquipEnforcement.enableAmmoPreference) {
			Ammo::OnAmmoUnequip(a_actor, a_object);
		}
	}
}
