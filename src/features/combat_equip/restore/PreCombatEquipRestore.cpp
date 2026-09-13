#include "PreCombatEquipRestore.h"

#include "ActorScope.h"
#include "ActionScheduler.h"
#include "CombatEquipOverrideUtil.h"
#include "CombatEquipPreference.h"
#include "ContainerMenuUtil.h"
#include "EquipGate.h"
#include "InventoryUtil.h"
#include "PluginSettings.h"
#include "WeaponBound.h"

#include "RE/S/ScriptEventSourceHolder.h"
#include "RE/T/TESCombatEvent.h"

#include "InstanceSignature.h"
#include "SignatureResolve.h"

#include <mutex>
#include <unordered_map>

namespace FEC::PreCombatEquipRestore
{
	namespace
	{
		namespace Util = FEC::CombatEquipOverride::Util;

		struct ActorSnapshot
		{
			RE::ACTOR_COMBAT_STATE lastState{ RE::ACTOR_COMBAT_STATE::kNone };

			SlotSnapshot right{};
			SlotSnapshot left{};
			RE::FormID ammoID{ 0 };

			bool haveSnapshot{ false };
			bool restorePending{ false };
			std::uint8_t restoreTriesRemaining{ 0 };
		};

		std::mutex g_mutex;
		std::unordered_map<RE::FormID, ActorSnapshot> g_actorSnapshots;
		bool g_installed{ false };

		[[nodiscard]] bool IsEnabled() noexcept
		{
			return PluginSettings::Get().combatEquipRestore.enableRestorePreCombatOnExit;
		}

		[[nodiscard]] RE::Actor* LookupActor(RE::FormID a_actorID)
		{
			return RE::TESForm::LookupByID<RE::Actor>(a_actorID);
		}

		[[nodiscard]] bool InventoryHasItem(RE::Actor* a_actor, RE::TESBoundObject* a_object)
		{
			return InventoryUtil::GetTotalCount(a_actor, a_object) > 0;
		}

		[[nodiscard]] std::optional<InstanceSignature> TryCaptureSignature(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			std::optional<InstanceSignature::EquipState> a_desiredState)
		{
			if (!a_actor || !a_object) {
				return std::nullopt;
			}

			auto inv = a_actor->GetInventory([&](RE::TESBoundObject& a_obj) {
				return std::addressof(a_obj) == a_object;
			});
			const auto it = inv.find(a_object);
			if (it == inv.end()) {
				return std::nullopt;
			}

			auto* entry = it->second.second.get();
			if (!entry || !entry->extraLists) {
				return std::nullopt;
			}

			for (auto* xList : *entry->extraLists) {
				if (!xList) {
					continue;
				}
				const auto sig = BuildInstanceSignature(*xList, a_object);
				if (a_desiredState.has_value()) {
					if (sig.equipState != *a_desiredState) {
						continue;
					}
				} else {
					if (sig.equipState == InstanceSignature::EquipState::kNotWorn) {
						continue;
					}
				}
				return sig;
			}

			return std::nullopt;
		}

		void CaptureSnapshot(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return;
			}

			ActorSnapshot snap{};

			if (auto* rightForm = a_actor->GetEquippedObject(false)) {
				auto* rightObj = rightForm->As<RE::TESBoundObject>();
				if (auto* weap = rightObj ? rightObj->As<RE::TESObjectWEAP>() : nullptr) {
					// Bound weapons are transient; do not snapshot them.
					if (!WeaponBound::IsBoundWeapon(weap)) {
						snap.right.kind = SlotKind::kWeapon;
						snap.right.baseObjectID = weap->GetFormID();
						snap.right.signature = TryCaptureSignature(a_actor, weap, InstanceSignature::EquipState::kWornRight);
					}
				}
			}

			if (auto* leftForm = a_actor->GetEquippedObject(true)) {
				auto* leftObj = leftForm->As<RE::TESBoundObject>();
				if (auto* weap = leftObj ? leftObj->As<RE::TESObjectWEAP>() : nullptr) {
					// Bound weapons are transient; do not snapshot them.
					if (!WeaponBound::IsBoundWeapon(weap)) {
						snap.left.kind = SlotKind::kWeapon;
						snap.left.baseObjectID = weap->GetFormID();
						snap.left.signature = TryCaptureSignature(a_actor, weap, InstanceSignature::EquipState::kWornLeft);
					}
				} else if (auto* armo = leftObj ? leftObj->As<RE::TESObjectARMO>() : nullptr; armo && armo->IsShield()) {
					snap.left.kind = SlotKind::kShield;
					snap.left.baseObjectID = armo->GetFormID();
					snap.left.signature = TryCaptureSignature(a_actor, armo, std::nullopt);
				}
			}

			if (auto* ammo = a_actor->GetCurrentAmmo()) {
				snap.ammoID = ammo->GetFormID();
			}

			snap.haveSnapshot = true;

			std::lock_guard lock(g_mutex);
			auto& entry = g_actorSnapshots[a_actor->GetFormID()];
			entry.right = std::move(snap.right);
			entry.left = std::move(snap.left);
			entry.ammoID = snap.ammoID;
			entry.haveSnapshot = true;

			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug(
					"CombatEquip: captured pre-combat snapshot actor={:08X} right={:08X} left={:08X} ammo={:08X}",
					a_actor->GetFormID(),
					entry.right.baseObjectID,
					entry.left.baseObjectID,
					entry.ammoID);
			}
		}

		void ScheduleRestore(RE::FormID a_actorID);
		struct ExpectedSnapshot
		{
			SlotSnapshot right{};
			SlotSnapshot left{};
			RE::FormID ammoID{ 0 };
		};
		void ScheduleReconcile(RE::FormID a_actorID, ExpectedSnapshot a_expected, std::uint8_t a_triesRemaining);

		[[nodiscard]] bool ShouldRetryRestore(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			if (a_actor->IsInCombat()) {
				return true;
			}
			if (ContainerMenuUtil::IsContainerMenuOpen()) {
				return true;
			}
			return false;
		}

		void UnequipIfPresent(RE::Actor* a_actor, RE::TESBoundObject* a_object, const RE::BGSEquipSlot* a_slot)
		{
			if (!a_actor || !a_object) {
				return;
			}

			// Only unequip if the item is still in the target slot.
			if (a_slot == Util::GetRightHandSlot()) {
				if (a_actor->GetEquippedObject(false) != a_object) {
					return;
				}
			} else if (a_slot == Util::GetLeftHandSlot()) {
				if (a_actor->GetEquippedObject(true) != a_object) {
					return;
				}
			}

			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return;
			}

			equipMan->UnequipObject(a_actor, a_object, nullptr, 1, a_slot, false, false, true, true);
		}

		void EquipBestEffort(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			const std::optional<InstanceSignature>& a_sig,
			const RE::BGSEquipSlot* a_slot,
			std::optional<InstanceSignature::EquipState> a_desiredState);

		void TryReconcilePostRestore(RE::FormID a_actorID, const ExpectedSnapshot& a_expected, std::uint8_t a_triesRemaining)
		{
			if (!IsEnabled()) {
				return;
			}

			auto* actor = LookupActor(a_actorID);
			if (!actor || !ActorScope::IsAffectedFollower(actor)) {
				return;
			}

			// Avoid fighting combat state or menu work; retry shortly if needed.
			if (actor->IsInCombat() || ContainerMenuUtil::IsContainerMenuOpen()) {
				if (a_triesRemaining > 0) {
					ScheduleReconcile(a_actorID, a_expected, static_cast<std::uint8_t>(a_triesRemaining - 1));
				}
				return;
			}

			EquipGate::ScopedBypass gateBypass;

			bool mismatch = false;

			{
				auto* currentForm = actor->GetEquippedObject(false);
				auto* currentObj = currentForm ? currentForm->As<RE::TESBoundObject>() : nullptr;
				auto* currentWeap = currentObj ? currentObj->As<RE::TESObjectWEAP>() : nullptr;
				if (currentWeap && WeaponBound::IsBoundWeapon(currentWeap)) {
					// Do not fight bound weapons; unequip or sheathe can end the conjuration.
				} else {

				if (a_expected.right.kind == SlotKind::kNone || a_expected.right.baseObjectID == 0) {
					if (currentWeap) {
						mismatch = true;
						UnequipIfPresent(actor, currentWeap, Util::GetRightHandSlot());
					}
				} else {
					if (!currentObj || currentObj->GetFormID() != a_expected.right.baseObjectID) {
						mismatch = true;
						auto* form = RE::TESForm::LookupByID(a_expected.right.baseObjectID);
						auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr;
						if (auto* weap = obj ? obj->As<RE::TESObjectWEAP>() : nullptr) {
							if (!WeaponBound::IsBoundWeapon(weap)) {
							EquipBestEffort(actor, weap, a_expected.right.signature, Util::GetRightHandSlot(), InstanceSignature::EquipState::kWornRight);
							}
						}
					}
				}
				}
			}

			{
				auto* currentForm = actor->GetEquippedObject(true);
				auto* currentObj = currentForm ? currentForm->As<RE::TESBoundObject>() : nullptr;
				auto* currentWeap = currentObj ? currentObj->As<RE::TESObjectWEAP>() : nullptr;
				auto* currentArmo = currentObj ? currentObj->As<RE::TESObjectARMO>() : nullptr;
				const bool currentIsShield = currentArmo && currentArmo->IsShield();
				if (currentWeap && WeaponBound::IsBoundWeapon(currentWeap)) {
					// Do not fight bound weapons; unequip or sheathe can end the conjuration.
				} else {

				if (a_expected.left.kind == SlotKind::kNone || a_expected.left.baseObjectID == 0) {
					if (currentWeap) {
						mismatch = true;
						UnequipIfPresent(actor, currentWeap, Util::GetLeftHandSlot());
					} else if (currentIsShield) {
						mismatch = true;
						UnequipIfPresent(actor, currentArmo, nullptr);
					}
				} else {
					const bool baseMatches = currentObj && currentObj->GetFormID() == a_expected.left.baseObjectID;
					const bool kindMatches =
						(a_expected.left.kind == SlotKind::kWeapon && currentWeap) ||
						(a_expected.left.kind == SlotKind::kShield && currentIsShield);

					if (!baseMatches || !kindMatches) {
						mismatch = true;

						// Clear the conflicting left-hand item first to reduce swap loops.
						if (currentWeap && a_expected.left.kind == SlotKind::kShield) {
							UnequipIfPresent(actor, currentWeap, Util::GetLeftHandSlot());
						}
						if (currentIsShield && a_expected.left.kind == SlotKind::kWeapon) {
							UnequipIfPresent(actor, currentArmo, nullptr);
						}

						auto* form = RE::TESForm::LookupByID(a_expected.left.baseObjectID);
						auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr;

						if (a_expected.left.kind == SlotKind::kWeapon) {
							if (auto* weap = obj ? obj->As<RE::TESObjectWEAP>() : nullptr) {
								if (!WeaponBound::IsBoundWeapon(weap)) {
										EquipBestEffort(actor, weap, a_expected.left.signature, Util::GetLeftHandSlot(), InstanceSignature::EquipState::kWornLeft);
								}
							}
						} else if (a_expected.left.kind == SlotKind::kShield) {
							if (auto* armo = obj ? obj->As<RE::TESObjectARMO>() : nullptr; armo && armo->IsShield()) {
								EquipBestEffort(actor, armo, a_expected.left.signature, nullptr, std::nullopt);
							}
						}
					}
				}
				}
			}

			{
				auto* currentAmmo = actor->GetCurrentAmmo();
				const auto currentAmmoID = currentAmmo ? currentAmmo->GetFormID() : 0;

				if (a_expected.ammoID == 0) {
					if (currentAmmo) {
						mismatch = true;
						UnequipIfPresent(actor, currentAmmo, nullptr);
					}
				} else {
					if (currentAmmoID != a_expected.ammoID) {
						mismatch = true;
						auto* form = RE::TESForm::LookupByID(a_expected.ammoID);
						auto* ammo = form ? form->As<RE::TESAmmo>() : nullptr;
						if (ammo) {
							EquipBestEffort(actor, ammo, std::nullopt, nullptr, std::nullopt);
						}
					}
				}
			}

			if (mismatch && a_triesRemaining > 0) {
				ScheduleReconcile(a_actorID, a_expected, static_cast<std::uint8_t>(a_triesRemaining - 1));
			}
		}

		void EquipBestEffort(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			const std::optional<InstanceSignature>& a_sig,
			const RE::BGSEquipSlot* a_slot,
			std::optional<InstanceSignature::EquipState> a_desiredState)
		{
			if (!a_actor || !a_object) {
				return;
			}
			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return;
			}
			if (!InventoryHasItem(a_actor, a_object)) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"CombatEquip: skipped pre-combat restore equip for missing inventory item actor={:08X} item={:08X}",
						a_actor->GetFormID(),
						a_object->GetFormID());
				}
				return;
			}

			const InstanceSignature sig = a_sig.has_value() ? *a_sig : InstanceSignature{};
			const auto resolved = SignatureResolve::Resolve(
				a_actor,
				a_object,
				sig,
				a_desiredState,
				SignatureResolve::Policy::kIdentityOnly);
			if (sig.HasStableIdentity() && !resolved.HasXList()) {
				return;
			}
			auto* xList = resolved.HasXList() ? resolved.xList : nullptr;

			// Stackable items such as ammo may have no extraLists; nullptr is the safe fallback.
			equipMan->EquipObject(a_actor, a_object, xList, 1, a_slot, false, false, true, true);
		}

		void RestoreActorNow(RE::FormID a_actorID)
		{
			if (!IsEnabled()) {
				return;
			}

			auto* actor = LookupActor(a_actorID);
			if (!actor || !ActorScope::IsAffectedFollower(actor)) {
				return;
			}

			ActorSnapshot snapCopy{};
			{
				std::lock_guard lock(g_mutex);
				auto it = g_actorSnapshots.find(a_actorID);
				if (it == g_actorSnapshots.end()) {
					return;
				}
				auto& entry = it->second;
				if (!entry.restorePending || !entry.haveSnapshot) {
					return;
				}

				if (ShouldRetryRestore(actor) && entry.restoreTriesRemaining > 0) {
					--entry.restoreTriesRemaining;
					ScheduleRestore(a_actorID);
					return;
				}

				entry.restorePending = false;
				entry.restoreTriesRemaining = 0;
				snapCopy = entry;
			}

			// If combat resumed, skip; the next combat-exit event can restore later.
			if (actor->IsInCombat()) {
				return;
			}

			EquipGate::ScopedBypass gateBypass;

			{
				auto* currentForm = actor->GetEquippedObject(false);
				auto* currentObj = currentForm ? currentForm->As<RE::TESBoundObject>() : nullptr;
				auto* currentWeap = currentObj ? currentObj->As<RE::TESObjectWEAP>() : nullptr;
				const bool currentIsBound = currentWeap && WeaponBound::IsBoundWeapon(currentWeap);

				if (snapCopy.right.kind == SlotKind::kNone || snapCopy.right.baseObjectID == 0) {
					if (!currentIsBound && currentWeap) {
						UnequipIfPresent(actor, currentWeap, Util::GetRightHandSlot());
					}
				} else {
					if (!currentObj || currentObj->GetFormID() != snapCopy.right.baseObjectID) {
						auto* form = RE::TESForm::LookupByID(snapCopy.right.baseObjectID);
						auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr;
						if (auto* weap = obj ? obj->As<RE::TESObjectWEAP>() : nullptr) {
							if (!currentIsBound && !WeaponBound::IsBoundWeapon(weap)) {
							EquipBestEffort(actor, weap, snapCopy.right.signature, Util::GetRightHandSlot(), InstanceSignature::EquipState::kWornRight);
							}
						}
					}
				}
			}

			{
				auto* currentForm = actor->GetEquippedObject(true);
				auto* currentObj = currentForm ? currentForm->As<RE::TESBoundObject>() : nullptr;
				const bool currentIsWeapon = currentObj && currentObj->IsWeapon();
				auto* currentWeap = currentIsWeapon ? currentObj->As<RE::TESObjectWEAP>() : nullptr;
				const bool currentIsBound = currentWeap && WeaponBound::IsBoundWeapon(currentWeap);
				const bool currentIsShield = [&]() {
					auto* a = currentObj ? currentObj->As<RE::TESObjectARMO>() : nullptr;
					return a && a->IsShield();
				}();

				if (snapCopy.left.kind == SlotKind::kNone || snapCopy.left.baseObjectID == 0) {
					if (currentIsWeapon) {
						if (!currentIsBound) {
							UnequipIfPresent(actor, currentObj, Util::GetLeftHandSlot());
						}
					} else if (currentIsShield) {
						UnequipIfPresent(actor, currentObj, nullptr);
					}
				} else {
					if (!currentObj || currentObj->GetFormID() != snapCopy.left.baseObjectID) {
						auto* form = RE::TESForm::LookupByID(snapCopy.left.baseObjectID);
						auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr;

						if (snapCopy.left.kind == SlotKind::kWeapon) {
							if (auto* weap = obj ? obj->As<RE::TESObjectWEAP>() : nullptr) {
								if (!currentIsBound && !WeaponBound::IsBoundWeapon(weap)) {
										EquipBestEffort(actor, weap, snapCopy.left.signature, Util::GetLeftHandSlot(), InstanceSignature::EquipState::kWornLeft);
								}
							}
						} else if (snapCopy.left.kind == SlotKind::kShield) {
							if (auto* armo = obj ? obj->As<RE::TESObjectARMO>() : nullptr; armo && armo->IsShield()) {
								// Use nullptr for shields to avoid slot mismatch.
								EquipBestEffort(actor, armo, snapCopy.left.signature, nullptr, std::nullopt);
							}
						}
					}
				}
			}

			{
				auto* currentAmmo = actor->GetCurrentAmmo();
				if (snapCopy.ammoID == 0) {
					if (currentAmmo) {
						UnequipIfPresent(actor, currentAmmo, nullptr);
					}
				} else {
					if (!currentAmmo || currentAmmo->GetFormID() != snapCopy.ammoID) {
						auto* form = RE::TESForm::LookupByID(snapCopy.ammoID);
						auto* ammo = form ? form->As<RE::TESAmmo>() : nullptr;
						if (ammo) {
							EquipBestEffort(actor, ammo, std::nullopt, nullptr, std::nullopt);
						}
					}
				}
			}

			// Short reconcile loop for observable equip-state and ammo UI edge cases.
			ScheduleReconcile(
				a_actorID,
				ExpectedSnapshot{ snapCopy.right, snapCopy.left, snapCopy.ammoID },
				3);

			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug("CombatEquip: restored pre-combat snapshot actor={:08X}", actor->GetFormID());
			}
		}

		void ScheduleRestore(RE::FormID a_actorID)
		{
			EquipMode::Core::ActionScheduler::NextFrame([a_actorID]() {
				RestoreActorNow(a_actorID);
			});
		}

		void ScheduleReconcile(RE::FormID a_actorID, ExpectedSnapshot a_expected, std::uint8_t a_triesRemaining)
		{
			EquipMode::Core::ActionScheduler::NextFrame([a_actorID, a_expected = std::move(a_expected), a_triesRemaining]() {
				TryReconcilePostRestore(a_actorID, a_expected, a_triesRemaining);
			});
		}

		class CombatEventSink final : public RE::BSTEventSink<RE::TESCombatEvent>
		{
		public:
			static CombatEventSink* GetSingleton()
			{
				static CombatEventSink s;
				return std::addressof(s);
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESCombatEvent* a_event,
				RE::BSTEventSource<RE::TESCombatEvent>* /*a_source*/) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto* ref = a_event->actor.get();
				auto* actor = ref ? const_cast<RE::TESObjectREFR*>(ref)->As<RE::Actor>() : nullptr;
				if (!actor || !ActorScope::IsAffectedFollower(actor)) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto actorID = actor->GetFormID();
				bool enteredCombat = false;
				bool exitedCombat = false;
				{
					std::lock_guard lock(g_mutex);
					auto& entry = g_actorSnapshots[actorID];
					const auto prev = entry.lastState;
					const auto next = a_event->newState.get();
					entry.lastState = next;

					enteredCombat = (prev == RE::ACTOR_COMBAT_STATE::kNone) && (next != RE::ACTOR_COMBAT_STATE::kNone);
					exitedCombat = (prev != RE::ACTOR_COMBAT_STATE::kNone) && (next == RE::ACTOR_COMBAT_STATE::kNone);

					if (IsEnabled() && exitedCombat) {
						entry.restorePending = true;
						entry.restoreTriesRemaining = 5;
					}
				}

				if (enteredCombat) {
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace("CombatEquip: actor {:08X} entered combat", actorID);
					}
					using Cat = CombatEquipPreference::Category;
					static constexpr std::pair<Cat, const char*> kAllCategories[] = {
						{ Cat::kOneHandRight, "OneHandRight" },
						{ Cat::kOneHandLeft,  "OneHandLeft"  },
						{ Cat::kShieldLeft,   "ShieldLeft"   },
						{ Cat::kTwoHand,      "TwoHand"      },
						{ Cat::kBow,          "Bow"          },
						{ Cat::kArrow,        "Arrow"        },
						{ Cat::kCrossbow,     "Crossbow"     },
						{ Cat::kBolt,         "Bolt"         },
						{ Cat::kStaffRight,   "StaffRight"   },
						{ Cat::kStaffLeft,    "StaffLeft"    },
						{ Cat::kScrollRight,  "ScrollRight"  },
						{ Cat::kScrollLeft,   "ScrollLeft"   },
						{ Cat::kScrollBoth,   "ScrollBoth"   },
					};
					for (const auto& [cat, name] : kAllCategories) {
						const auto e = CombatEquipPreference::GetEntry(actorID, cat);
						if (e.has_value() && e->baseObjectID != 0) {
							if (spdlog::should_log(spdlog::level::trace)) {
								logger::trace("CombatEquip:   pref {} = {:08X}", name, e->baseObjectID);
							}
						}
					}
				}
				if (exitedCombat) {
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace("CombatEquip: actor {:08X} exited combat", actorID);
					}
				}

				if (IsEnabled() && enteredCombat) {
					CaptureSnapshot(actor);
				}
				if (IsEnabled() && exitedCombat) {
					ScheduleRestore(actorID);
				}

				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	void Install()
	{
		if (g_installed) {
			return;
		}

		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (!holder) {
			return;
		}

		holder->AddEventSink<RE::TESCombatEvent>(CombatEventSink::GetSingleton());
		g_installed = true;
		logger::info("CombatEquip: installed TESCombatEvent sink (restore pre-combat)");
	}

	std::vector<SnapshotEntry> SnapshotEntries()
	{
		std::vector<SnapshotEntry> out;
		std::lock_guard lock(g_mutex);
		out.reserve(g_actorSnapshots.size());
		for (const auto& [actorID, snap] : g_actorSnapshots) {
			if (!snap.haveSnapshot) {
				continue;
			}
			SnapshotEntry entry{};
			entry.actorID = actorID;
			entry.right = snap.right;
			entry.left = snap.left;
			entry.ammoID = snap.ammoID;
			out.push_back(std::move(entry));
		}
		return out;
	}

	void SetLoadedEntry(SnapshotEntry a_entry)
	{
		if (a_entry.actorID == 0) {
			return;
		}

		// Mid-combat saves load while the actor is already in combat. Seed lastState so the first
		// event does not look like kNone->kCombat and overwrite the real pre-combat snapshot.
		auto combatState = RE::ACTOR_COMBAT_STATE::kNone;
		if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_entry.actorID)) {
			if (actor->IsInCombat()) {
				combatState = RE::ACTOR_COMBAT_STATE::kCombat;
				logger::debug("CombatEquip: loaded actor {:08X} already in combat, preserving snapshot", a_entry.actorID);
			}
		}

		std::lock_guard lock(g_mutex);
		auto& entry = g_actorSnapshots[a_entry.actorID];
		entry.right = std::move(a_entry.right);
		entry.left = std::move(a_entry.left);
		entry.ammoID = a_entry.ammoID;
		entry.haveSnapshot = true;
		entry.restorePending = false;
		entry.restoreTriesRemaining = 0;
		entry.lastState = combatState;
	}

	void ClearSnapshots()
	{
		std::lock_guard lock(g_mutex);
		g_actorSnapshots.clear();
	}

	void EraseActor(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return;
		}

		std::lock_guard lock(g_mutex);
		g_actorSnapshots.erase(a_actorID);
	}

	void Uninstall()
	{
		if (!g_installed) {
			return;
		}

		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (holder) {
			holder->RemoveEventSink<RE::TESCombatEvent>(CombatEventSink::GetSingleton());
		}

		ClearSnapshots();

		g_installed = false;
		logger::info("CombatEquip: uninstalled TESCombatEvent sink (restore pre-combat)");
	}
}
