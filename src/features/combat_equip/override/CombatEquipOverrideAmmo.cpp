#include "CombatEquipOverrideAmmo.h"

#include "CombatEquipOverridePolicy.h"
#include "CombatEquipOverrideState.h"
#include "CombatEquipOverrideTelemetry.h"
#include "CombatEquipOverrideUtil.h"

#include "CombatEquipPreference.h"
#include "EquipGate.h"
#include "PluginSettings.h"
#include "WeaponBound.h"

#include <mutex>
#include <unordered_set>

namespace FEC::CombatEquipOverride::Ammo
{
	namespace
	{
		struct InventoryAmmoResult
		{
			RE::TESBoundObject* object{ nullptr };
			std::int32_t count{ 0 };
			std::size_t candidates{ 0 };
		};

		std::mutex g_pendingAmmoReequipMutex;
		std::unordered_set<RE::FormID> g_pendingAmmoReequipActors;

		[[nodiscard]] bool MarkPendingAmmoReequip(RE::FormID a_actorID)
		{
			if (a_actorID == 0) {
				return false;
			}

			std::scoped_lock lock(g_pendingAmmoReequipMutex);
			return g_pendingAmmoReequipActors.insert(a_actorID).second;
		}

		void ClearPendingAmmoReequip(RE::FormID a_actorID)
		{
			if (a_actorID == 0) {
				return;
			}

			std::scoped_lock lock(g_pendingAmmoReequipMutex);
			g_pendingAmmoReequipActors.erase(a_actorID);
		}

		struct PendingAmmoReequipClear
		{
			RE::FormID actorID{ 0 };

			~PendingAmmoReequipClear()
			{
				ClearPendingAmmoReequip(actorID);
			}
		};

		[[nodiscard]] InventoryAmmoResult FindAmmoInInventory(RE::Actor* a_actor, RE::FormID a_ammoID)
		{
			InventoryAmmoResult result{};
			if (!a_actor || a_ammoID == 0) {
				return result;
			}

			auto inv = a_actor->GetInventory([a_ammoID](RE::TESBoundObject& a_item) {
				return a_item.GetFormID() == a_ammoID && a_item.GetFormType() == RE::FormType::Ammo;
			});

			result.candidates = inv.size();
			if (!inv.empty() && inv.begin()->second.first > 0) {
				result.object = inv.begin()->first;
				result.count = inv.begin()->second.first;
			}

			return result;
		}

		[[nodiscard]] InventoryAmmoResult FindFallbackAmmo(RE::Actor* a_actor, RE::FormID a_excludeAmmoID, bool a_wantBolt)
		{
			InventoryAmmoResult result{};
			if (!a_actor) {
				return result;
			}

			auto inv = a_actor->GetInventory([a_excludeAmmoID, a_wantBolt](RE::TESBoundObject& a_item) {
				if (a_item.GetFormID() == a_excludeAmmoID) {
					return false;
				}

				auto* ammo = a_item.As<RE::TESAmmo>();
				return ammo && ammo->IsBolt() == a_wantBolt;
			});

			result.candidates = inv.size();
			for (auto& [obj, data] : inv) {
				if (data.first > result.count) {
					result.object = obj;
					result.count = data.first;
				}
			}

			return result;
		}

		[[nodiscard]] bool ValidateRangedAmmoContext(
			RE::Actor* a_actor,
			RE::FormID a_actorID,
			RE::FormID a_sourceAmmoID,
			const char* a_logPrefix,
			bool a_trace,
			RE::TESObjectWEAP*& a_rightWeapon)
		{
			a_rightWeapon = nullptr;

			if (!a_actor) {
				return false;
			}

			auto* st = a_actor->AsActorState();
			const bool drawn = (st && st->IsWeaponDrawn());
			const bool inCombat = a_actor->IsInCombat();
			if (!drawn && !inCombat) {
				if (a_trace) {
					logger::trace(
						"Override: {} actor={:08X} obj={:08X} reason=not_ranged_combat drawn={} inCombat={}",
						a_logPrefix, a_actorID, a_sourceAmmoID, drawn, inCombat);
				}
				return false;
			}

			auto* rightForm = a_actor->GetEquippedObject(false);
			auto* rightWeap = rightForm ? rightForm->As<RE::TESObjectWEAP>() : nullptr;
			if (!rightWeap) {
				if (a_trace) {
					logger::trace(
						"Override: {} actor={:08X} obj={:08X} reason=no_right_weapon rightForm={:08X}",
						a_logPrefix,
						a_actorID,
						a_sourceAmmoID,
						rightForm ? rightForm->GetFormID() : 0);
				}
				return false;
			}

			const auto weapType = rightWeap->GetWeaponType();
			if (weapType != RE::WEAPON_TYPE::kBow && weapType != RE::WEAPON_TYPE::kCrossbow) {
				if (a_trace) {
					logger::trace(
						"Override: {} actor={:08X} obj={:08X} reason=not_ranged weapType={}",
						a_logPrefix, a_actorID, a_sourceAmmoID, static_cast<int>(weapType));
				}
				return false;
			}

			if (WeaponBound::IsBoundWeapon(rightWeap)) {
				if (a_trace) {
					logger::trace(
						"Override: {} actor={:08X} obj={:08X} reason=bound_weapon",
						a_logPrefix, a_actorID, a_sourceAmmoID);
				}
				return false;
			}

			a_rightWeapon = rightWeap;
			return true;
		}

		void RunDeferredAmmoReequip(
			RE::ActorHandle a_actorHandle,
			RE::FormID a_actorID,
			RE::FormID a_sourceAmmoID,
			const char* a_reason)
		{
			const PendingAmmoReequipClear clear{ a_actorID };
			const bool trace = spdlog::should_log(spdlog::level::trace);

			auto actor = RE::Actor::LookupByHandle(a_actorHandle.native_handle());
			if (!actor || actor->GetFormID() != a_actorID || actor->IsDeleted() || !actor->Is3DLoaded() || actor->IsDead()) {
				if (trace) {
					logger::trace(
						"Override: ammo_reequip_task_skip actor={:08X} obj={:08X} reason=actor_invalid",
						a_actorID, a_sourceAmmoID);
				}
				return;
			}

			if (!PluginSettings::Get().combatEquipEnforcement.enableAmmoPreference) {
				if (trace) {
					logger::trace(
						"Override: ammo_reequip_task_skip actor={:08X} obj={:08X} reason=feature_disabled",
						a_actorID, a_sourceAmmoID);
				}
				return;
			}

			if (!Policy::ShouldConsiderUnequip(actor.get(), true)) {
				if (trace) {
					logger::trace(
						"Override: ammo_reequip_task_skip actor={:08X} obj={:08X} reason=policy_skip",
						a_actorID, a_sourceAmmoID);
				}
				return;
			}

			RE::TESObjectWEAP* rightWeap = nullptr;
			if (!ValidateRangedAmmoContext(
					actor.get(), a_actorID, a_sourceAmmoID, "ammo_reequip_task_skip", trace, rightWeap)) {
				return;
			}

			const auto lastAmmo = State::GetLastAmmoOverride(a_actorID);
			if (!lastAmmo.has_value() || *lastAmmo == 0) {
				if (trace) {
					logger::trace(
						"Override: ammo_reequip_task_skip actor={:08X} obj={:08X} reason=no_last_ammo",
						a_actorID, a_sourceAmmoID);
				}
				return;
			}

			const auto prefID = *lastAmmo;
			auto* currentAmmo = actor->GetCurrentAmmo();
			const auto currentAmmoID = currentAmmo ? currentAmmo->GetFormID() : RE::FormID(0);
			if (currentAmmoID == prefID) {
				if (trace) {
					logger::trace(
						"Override: ammo_reequip_task_skip actor={:08X} obj={:08X} reason=current_ammo_matches "
						"currentAmmo={:08X} lastAmmo={:08X}",
						a_actorID, a_sourceAmmoID, currentAmmoID, prefID);
				}
				return;
			}

			auto* equipManager = RE::ActorEquipManager::GetSingleton();
			if (!equipManager) {
				if (trace) {
					logger::trace(
						"Override: ammo_reequip_task_skip actor={:08X} obj={:08X} reason=no_equip_manager",
						a_actorID, a_sourceAmmoID);
				}
				return;
			}

			const auto pref = FindAmmoInInventory(actor.get(), prefID);
			const auto weapType = rightWeap->GetWeaponType();

			if (trace) {
				logger::trace(
					"Override: ammo_reequip_task_eval actor={:08X} obj={:08X} "
					"lastAmmo={:08X} currentAmmo={:08X} prefCount={} prefFound={} "
					"weapType={} right={:08X} ({}) reason={}",
					a_actorID,
					a_sourceAmmoID,
					prefID,
					currentAmmoID,
					pref.count,
					pref.object != nullptr,
					static_cast<int>(weapType),
					rightWeap->GetFormID(),
					rightWeap->GetName(),
					a_reason ? a_reason : "");
			}

			if (pref.object && pref.count >= 1) {
				Telemetry::LogAmmoReequipAttempt(actor.get(), prefID, a_reason ? a_reason : "ammo_unequip_while_ranged");

				if (trace) {
					logger::trace(
						"Override: ammo_reequip_task_apply actor={:08X} prefID={:08X} ({}) prefCount={} queue=true",
						a_actorID, prefID, pref.object->GetName(), pref.count);
				}

				EquipGate::ScopedBypass bypass;
				equipManager->EquipObject(
					actor.get(),
					pref.object,
					nullptr,
					1,
					nullptr,
					true,
					false,
					false,
					false);
				return;
			}

			const bool wantBolt = (weapType == RE::WEAPON_TYPE::kCrossbow);
			const auto alt = FindFallbackAmmo(actor.get(), prefID, wantBolt);

			if (trace) {
				logger::trace(
					"Override: ammo_exhaustion_task_scan actor={:08X} prefID={:08X} prefCount={} "
					"altAmmo={:08X} ({}) altCount={} altCandidates={}",
					a_actorID,
					prefID,
					pref.count,
					alt.object ? alt.object->GetFormID() : RE::FormID(0),
					alt.object ? alt.object->GetName() : "NONE",
					alt.count,
					alt.candidates);
			}

			if (!alt.object || alt.count < 1) {
				if (trace) {
					logger::trace(
						"Override: ammo_exhaustion_no_alt actor={:08X} prefID={:08X} "
						"— no alternative ammo found, follower will be unarmed",
						a_actorID, prefID);
				}
				return;
			}

			Telemetry::LogAmmoReequipAttempt(
				actor.get(), alt.object->GetFormID(), "ammo_exhaustion_transition");
			State::RememberLastAmmoOverride(a_actorID, alt.object->GetFormID());

			EquipGate::ScopedBypass bypass;
			equipManager->EquipObject(
				actor.get(), alt.object, nullptr, 1, nullptr,
				true, false, false, false);
		}

		[[nodiscard]] bool QueueDeferredAmmoReequip(
			RE::Actor* a_actor,
			RE::FormID a_sourceAmmoID,
			const char* a_reason)
		{
			if (!a_actor || a_sourceAmmoID == 0) {
				return false;
			}

			const auto actorID = a_actor->GetFormID();
			if (actorID == 0) {
				return false;
			}

			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"Override: ammo_reequip_queue_skip actor={:08X} obj={:08X} reason=no_task_interface",
						actorID, a_sourceAmmoID);
				}
				return false;
			}

			if (!MarkPendingAmmoReequip(actorID)) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"Override: ammo_reequip_queue_skip actor={:08X} obj={:08X} reason=already_pending",
						actorID, a_sourceAmmoID);
				}
				return true;
			}

			const auto actorHandle = a_actor->GetHandle();
			taskInterface->AddTask([actorHandle, actorID, a_sourceAmmoID, a_reason]() {
				RunDeferredAmmoReequip(actorHandle, actorID, a_sourceAmmoID, a_reason);
			});

			return true;
		}
	}

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

		RE::TESObjectWEAP* rightWeap = nullptr;
		if (!ValidateRangedAmmoContext(
				a_actor, actorID, a_object->GetFormID(), "ammo_unequip_skip", trace, rightWeap)) {
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

		if (trace) {
			logger::trace(
				"Override: ammo_reequip_queue actor={:08X} obj={:08X} ({}) "
				"lastAmmo={:08X} currentAmmo={:08X} weapType={} right={:08X} ({})",
				actorID,
				a_object->GetFormID(),
				a_object->GetName(),
				*lastAmmo,
				currentAmmoID,
				static_cast<int>(rightWeap->GetWeaponType()),
				rightWeap->GetFormID(),
				rightWeap->GetName());
		}

		(void)QueueDeferredAmmoReequip(a_actor, a_object->GetFormID(), "ammo_unequip_while_ranged");
	}
}
