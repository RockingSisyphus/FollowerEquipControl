#include "CombatEquipOverrideAmmo.h"

#include "CombatEquipOverrideState.h"
#include "CombatEquipOverrideTelemetry.h"
#include "CombatEquipOverrideUtil.h"

#include "CombatEquipPreference.h"
#include "EquipGate.h"
#include "WeaponBound.h"

namespace FEC::CombatEquipOverride::Ammo
{
	std::optional<Decision> DecideAmmoEquipSwap(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count)
	{
		(void)a_extraData;
		(void)a_slot;

		if (!a_actor || !a_object) {
			return std::nullopt;
		}

		auto* ammo = a_object->As<RE::TESAmmo>();
		if (!ammo) {
			return std::nullopt;
		}

		const bool trace = spdlog::should_log(spdlog::level::trace);

		// Bound Bow uses BoundArrow; overriding it can break the release animation.
		{
			auto* rightForm = a_actor->GetEquippedObject(false);
			if (WeaponBound::IsWeaponAndBound(rightForm ? rightForm->As<RE::TESBoundObject>() : nullptr)) {
				return std::nullopt;
			}
		}

		const auto actorID = a_actor->GetFormID();
		if (actorID == 0) {
			return std::nullopt;
		}

		using Cat = CombatEquipPreference::Category;
		const auto cat = ammo->IsBolt() ? Cat::kBolt : Cat::kArrow;
		const auto entry = CombatEquipPreference::GetEntry(actorID, cat);
		if (!entry.has_value() || entry->baseObjectID == 0) {
			return std::nullopt;
		}
		if (entry->baseObjectID == a_object->GetFormID()) {
			return std::nullopt;
		}

		auto* prefForm = RE::TESForm::LookupByID(entry->baseObjectID);
		auto* prefObj = prefForm ? prefForm->As<RE::TESBoundObject>() : nullptr;
		if (!prefObj || prefObj->GetFormType() != RE::FormType::Ammo) {
			return std::nullopt;
		}

		// If preferred ammo is missing, let vanilla equip the requested ammo.
		bool hasPreferredAmmo = false;
		std::int32_t prefInvCount = 0;
		{
			const auto prefID = prefObj->GetFormID();
			auto inv = a_actor->GetInventory([prefID](RE::TESBoundObject& a_item) {
				return a_item.GetFormID() == prefID;
			});
			if (!inv.empty() && inv.begin()->second.first > 0) {
				hasPreferredAmmo = true;
				prefInvCount = inv.begin()->second.first;
			}
		}
		if (!hasPreferredAmmo) {
			if (trace) {
				logger::trace(
					"Override: ammo_swap_skip actor={:08X} obj={:08X} ({}) pref={:08X} ({}) "
					"reason=pref_not_in_inventory prefInvCount={}",
					actorID,
					a_object->GetFormID(),
					a_object->GetName(),
					prefObj->GetFormID(),
					prefObj->GetName(),
					prefInvCount);
			}
			return std::nullopt;
		}

		if (trace) {
			logger::trace(
				"Override: ammo_swap_apply actor={:08X} obj={:08X} ({}) pref={:08X} ({}) "
				"prefInvCount={} count={}",
				actorID,
				a_object->GetFormID(),
				a_object->GetName(),
				prefObj->GetFormID(),
				prefObj->GetName(),
				prefInvCount,
				a_count);
		}

		Decision d;
		d.action = DecisionAction::kSwapToPreferred;
		d.preferred = PreferredItem{ prefObj, nullptr };
		d.slotToUse = nullptr;
		d.countToUse = (a_count > 0 ? a_count : 1);
		d.queueEquip = false;
		d.forceEquip = false;
		d.applyNow = true;
		d.reason = "ammo_preference";

		State::RememberLastAmmoOverride(actorID, entry->baseObjectID);
		return d;
	}

	void OnAmmoUnequip(RE::Actor* a_actor, RE::TESBoundObject* a_object)
	{
		if (!a_actor || !a_object) {
			return;
		}
		if (a_object->GetFormType() != RE::FormType::Ammo) {
			return;
		}

		const auto actorID = a_actor->GetFormID();
		if (actorID == 0) {
			return;
		}

		const bool trace = spdlog::should_log(spdlog::level::trace);

		const auto lastAmmo = State::GetLastAmmoOverride(actorID);
		if (!lastAmmo.has_value() || *lastAmmo != a_object->GetFormID()) {
			if (trace) {
				logger::trace(
					"Override: ammo_unequip_skip actor={:08X} obj={:08X} ({}) reason=no_override_match lastAmmo={}",
					actorID,
					a_object->GetFormID(),
					a_object->GetName(),
					lastAmmo.has_value() ? fmt::format("{:08X}", *lastAmmo) : "none");
			}
			return;
		}

		// Only re-equip ammo while the actor is still in ranged combat.
		auto* st = a_actor->AsActorState();
		const bool drawn = (st && st->IsWeaponDrawn());
		const bool inCombat = a_actor->IsInCombat();
		if (!drawn && !inCombat) {
			if (trace) {
				logger::trace(
					"Override: ammo_unequip_skip actor={:08X} obj={:08X} reason=not_ranged_combat drawn={} inCombat={}",
					actorID, a_object->GetFormID(), drawn, inCombat);
			}
			return;
		}

		auto* rightForm = a_actor->GetEquippedObject(false);
		auto* rightWeap = rightForm ? rightForm->As<RE::TESObjectWEAP>() : nullptr;
		if (!rightWeap) {
			if (trace) {
				logger::trace(
					"Override: ammo_unequip_skip actor={:08X} obj={:08X} reason=no_right_weapon rightForm={:08X}",
					actorID, a_object->GetFormID(),
					rightForm ? rightForm->GetFormID() : 0);
			}
			return;
		}
		const auto weapType = rightWeap->GetWeaponType();
		if (weapType != RE::WEAPON_TYPE::kBow && weapType != RE::WEAPON_TYPE::kCrossbow) {
			if (trace) {
				logger::trace(
					"Override: ammo_unequip_skip actor={:08X} obj={:08X} reason=not_ranged weapType={}",
					actorID, a_object->GetFormID(), static_cast<int>(weapType));
			}
			return;
		}
		if (WeaponBound::IsBoundWeapon(rightWeap)) {
			if (trace) {
				logger::trace(
					"Override: ammo_unequip_skip actor={:08X} obj={:08X} reason=bound_weapon",
					actorID, a_object->GetFormID());
			}
			return;
		}

		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return;
		}

		auto* currentAmmo = a_actor->GetCurrentAmmo();
		const auto currentAmmoID = currentAmmo ? currentAmmo->GetFormID() : RE::FormID(0);
		const bool ammoStillSlotted = (currentAmmoID == *lastAmmo);

		if (ammoStillSlotted) {
			if (trace) {
				logger::trace(
					"Override: ammo_unequip_skip actor={:08X} obj={:08X} reason=current_ammo_matches "
					"currentAmmo={:08X} lastAmmo={:08X}",
					actorID, a_object->GetFormID(), currentAmmoID, *lastAmmo);
			}
			return;
		}

		const auto prefID = *lastAmmo;
		RE::TESBoundObject* prefObj = nullptr;
		std::int32_t prefCount = 0;
		{
			auto inv = a_actor->GetInventory([prefID](RE::TESBoundObject& a_item) {
				return a_item.GetFormID() == prefID;
			});
			if (!inv.empty() && inv.begin()->second.first > 0) {
				prefObj = inv.begin()->first;
				prefCount = inv.begin()->second.first;
			}
		}

		if (trace) {
			logger::trace(
				"Override: ammo_unequip_eval actor={:08X} obj={:08X} ({}) "
				"lastAmmo={:08X} currentAmmo={:08X} prefCount={} prefFound={} "
				"drawn={} inCombat={} weapType={} right={:08X} ({})",
				actorID,
				a_object->GetFormID(),
				a_object->GetName(),
				*lastAmmo,
				currentAmmoID,
				prefCount,
				prefObj != nullptr,
				drawn,
				inCombat,
				static_cast<int>(weapType),
				rightWeap->GetFormID(),
				rightWeap->GetName());
		}

		if (prefObj) {
			if (prefCount >= 1) {
				Telemetry::LogAmmoReequipAttempt(a_actor, prefID, "ammo_unequip_while_ranged");

				if (trace) {
					logger::trace(
						"Override: ammo_reequip_detail actor={:08X} prefID={:08X} ({}) prefCount={} queue=true",
						actorID, prefID, prefObj->GetName(), prefCount);
				}

				EquipGate::ScopedBypass bypass;
				equipManager->EquipObject(
					a_actor,
					prefObj,
					nullptr,
					1,
					nullptr,
					true,   // queueEquip: defer to break the sequential unequip loop
					false,
					false,
					false); // applyNow=false
				return;
			}
		}

		// Preferred ammo is exhausted; fall back to another arrow or bolt of the same type.
		{
			const bool wantBolt = (weapType == RE::WEAPON_TYPE::kCrossbow);
			auto altInv = a_actor->GetInventory(
				[prefID, wantBolt](RE::TESBoundObject& a_item) {
					if (a_item.GetFormID() == prefID) {
						return false;
					}
					auto* ammo = a_item.As<RE::TESAmmo>();
					return ammo && ammo->IsBolt() == wantBolt;
				});

			RE::TESBoundObject* altAmmo = nullptr;
			std::int32_t altCount = 0;
			for (auto& [obj, data] : altInv) {
				if (data.first > altCount) {
					altAmmo = obj;
					altCount = data.first;
				}
			}

			if (trace) {
				logger::trace(
					"Override: ammo_exhaustion_scan actor={:08X} prefID={:08X} prefCount={} "
					"altAmmo={:08X} ({}) altCount={} altCandidates={}",
					actorID,
					prefID,
					prefCount,
					altAmmo ? altAmmo->GetFormID() : RE::FormID(0),
					altAmmo ? altAmmo->GetName() : "NONE",
					altCount,
					altInv.size());
			}

			if (altAmmo) {
				Telemetry::LogAmmoReequipAttempt(
					a_actor, altAmmo->GetFormID(), "ammo_exhaustion_transition");
				State::RememberLastAmmoOverride(actorID, altAmmo->GetFormID());

				EquipGate::ScopedBypass bypass;
				equipManager->EquipObject(
					a_actor, altAmmo, nullptr, 1, nullptr,
					true, false, false, false);
			} else {
				if (trace) {
					logger::trace(
						"Override: ammo_exhaustion_no_alt actor={:08X} prefID={:08X} "
						"— no alternative ammo found, follower will be unarmed",
						actorID, prefID);
				}
			}
		}
	}
}
