#include "HeadgearAutoEquip.h"

#include "ActorScope.h"
#include "ActionScheduler.h"
#include "CombatEquipPreference.h"
#include "ContainerMenuUtil.h"
#include "EquipGate.h"
#include "PluginSettings.h"

#include "RE/S/ScriptEventSourceHolder.h"
#include "RE/T/TESCombatEvent.h"

#include "InstanceSignature.h"
#include "SignatureResolve.h"

#include <mutex>
#include <unordered_map>

namespace FEC::HeadgearAutoEquip
{
	namespace
	{
		struct ActorSnapshot
		{
			RE::ACTOR_COMBAT_STATE lastState{ RE::ACTOR_COMBAT_STATE::kNone };

			std::vector<WornHeadgearItem> wornHeadgear;  // all headgear worn before combat

			bool haveSnapshot{ false };
			bool restorePending{ false };
			std::uint8_t restoreTriesRemaining{ 0 };
		};

		std::mutex g_mutex;
		std::unordered_map<RE::FormID, ActorSnapshot> g_actorSnapshots;
		bool g_installed{ false };

		[[nodiscard]] bool IsEnabled() noexcept
		{
			return PluginSettings::Get().combatEquipRestore.enableHeadgearAutoEquip;
		}

		[[nodiscard]] RE::Actor* LookupActor(RE::FormID a_actorID)
		{
			return RE::TESForm::LookupByID<RE::Actor>(a_actorID);
		}

		[[nodiscard]] bool IsHeadgearSlot(RE::TESObjectARMO* a_armor)
		{
			if (!a_armor) {
				return false;
			}
			const auto mask = static_cast<std::uint32_t>(a_armor->GetSlotMask());
			constexpr auto kHead = static_cast<std::uint32_t>(RE::BGSBipedObjectForm::BipedObjectSlot::kHead);
			constexpr auto kHair = static_cast<std::uint32_t>(RE::BGSBipedObjectForm::BipedObjectSlot::kHair);
			constexpr auto kCirclet = static_cast<std::uint32_t>(RE::BGSBipedObjectForm::BipedObjectSlot::kCirclet);
			return (mask & (kHead | kHair | kCirclet)) != 0;
		}

		// Collect distinct worn headgear across Head, Hair, and Circlet slots.
		[[nodiscard]] std::vector<RE::TESObjectARMO*> FindAllWornHeadgear(RE::Actor* a_actor)
		{
			std::vector<RE::TESObjectARMO*> result;
			if (!a_actor) {
				return result;
			}
			constexpr RE::BGSBipedObjectForm::BipedObjectSlot kSlots[] = {
				RE::BGSBipedObjectForm::BipedObjectSlot::kHead,
				RE::BGSBipedObjectForm::BipedObjectSlot::kHair,
				RE::BGSBipedObjectForm::BipedObjectSlot::kCirclet,
			};
			for (auto slot : kSlots) {
				auto* worn = a_actor->GetWornArmor(slot);
				if (!worn || !worn->GetPlayable() || !IsHeadgearSlot(worn)) {
					continue;
				}
				// A helmet occupying Head+Hair appears in both slots.
				bool alreadySeen = false;
				for (auto* existing : result) {
					if (existing->GetFormID() == worn->GetFormID()) {
						alreadySeen = true;
						break;
					}
				}
				if (!alreadySeen) {
					result.push_back(worn);
				}
			}
			return result;
		}

		[[nodiscard]] std::optional<InstanceSignature> TryCaptureHeadgearSignature(
			RE::Actor* a_actor,
			RE::TESObjectARMO* a_armor)
		{
			if (!a_actor || !a_armor) {
				return std::nullopt;
			}

			auto inv = a_actor->GetInventory([&](RE::TESBoundObject& a_obj) {
				return std::addressof(a_obj) == a_armor;
			});
			const auto it = inv.find(a_armor);
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
				const auto sig = BuildInstanceSignature(*xList, a_armor);
				// Headgear may report any worn state except not-worn.
				if (sig.equipState != InstanceSignature::EquipState::kNotWorn &&
					sig.equipState != InstanceSignature::EquipState::kUnknown) {
					return sig;
				}
			}

			return std::nullopt;
		}

		void CaptureSnapshot(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return;
			}
			if (!ActorScope::ArmorValidationAllowed(a_actor)) {
				return;
			}

			std::vector<WornHeadgearItem> worn;
			for (auto* armo : FindAllWornHeadgear(a_actor)) {
				WornHeadgearItem item;
				item.baseObjectID = armo->GetFormID();
				item.signature = TryCaptureHeadgearSignature(a_actor, armo);
				worn.push_back(std::move(item));
			}

			std::lock_guard lock(g_mutex);
			auto& entry = g_actorSnapshots[a_actor->GetFormID()];
			entry.wornHeadgear = std::move(worn);
			entry.haveSnapshot = true;

			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug(
					"HeadgearToggle: captured pre-combat snapshot actor={:08X} headgearCount={}",
					a_actor->GetFormID(),
					entry.wornHeadgear.size());
			}
		}

		void ScheduleRestore(RE::FormID a_actorID);
		void ScheduleReconcile(RE::FormID a_actorID, std::vector<WornHeadgearItem> a_expected, std::uint8_t a_triesRemaining);

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

		void EquipBestEffort(
			RE::Actor* a_actor,
			RE::TESObjectARMO* a_armor,
			const std::optional<InstanceSignature>& a_sig)
		{
			if (!a_actor || !a_armor) {
				return;
			}
			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return;
			}

			const InstanceSignature sig = a_sig.has_value() ? *a_sig : InstanceSignature{};
			const auto resolved = SignatureResolve::Resolve(
				a_actor,
				a_armor,
				sig,
				std::nullopt,
				SignatureResolve::Policy::kIdentityOnly);
			if (sig.HasStableIdentity() && !resolved.HasXList()) {
				return;
			}
			auto* xList = resolved.HasXList() ? resolved.xList : nullptr;

			equipMan->EquipObject(a_actor, a_armor, xList, 1, nullptr, false, false, true, true);
		}

		void UnequipHeadgear(RE::Actor* a_actor, RE::TESObjectARMO* a_armor)
		{
			if (!a_actor || !a_armor) {
				return;
			}
			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return;
			}
			equipMan->UnequipObject(a_actor, a_armor, nullptr, 1, nullptr, false, false, true, true);
		}

		[[nodiscard]] bool InventoryHasItem(RE::Actor* a_actor, RE::FormID a_baseID)
		{
			if (!a_actor || a_baseID == 0) {
				return false;
			}
			auto inv = a_actor->GetInventory([a_baseID](RE::TESBoundObject& a_obj) {
				return a_obj.GetFormID() == a_baseID;
			});
			for (const auto& [obj, data] : inv) {
				if (data.first > 0) {
					return true;
				}
			}
			return false;
		}

		void TryReconcilePostRestore(
			RE::FormID a_actorID,
			const std::vector<WornHeadgearItem>& a_expected,
			std::uint8_t a_triesRemaining)
		{
			if (!IsEnabled()) {
				return;
			}

			auto* actor = LookupActor(a_actorID);
			if (!actor || !ActorScope::IsAffectedFollower(actor)) {
				return;
			}

			if (actor->IsInCombat() || ContainerMenuUtil::IsContainerMenuOpen()) {
				if (a_triesRemaining > 0) {
					ScheduleReconcile(a_actorID, a_expected, static_cast<std::uint8_t>(a_triesRemaining - 1));
				}
				return;
			}

			EquipGate::ScopedBypass gateBypass;

			auto currentlyWorn = FindAllWornHeadgear(actor);

			bool mismatch = false;

			// Remove headgear not present in the expected pre-combat set.
			for (auto* worn : currentlyWorn) {
				const auto wornID = worn->GetFormID();
				bool inExpected = false;
				for (const auto& exp : a_expected) {
					if (exp.baseObjectID == wornID) {
						inExpected = true;
						break;
					}
				}
				if (!inExpected) {
					UnequipHeadgear(actor, worn);
					mismatch = true;
				}
			}

			// Restore expected pre-combat headgear that is missing.
			for (const auto& exp : a_expected) {
				if (exp.baseObjectID == 0) {
					continue;
				}
				bool alreadyWorn = false;
				for (auto* worn : currentlyWorn) {
					if (worn->GetFormID() == exp.baseObjectID) {
						alreadyWorn = true;
						break;
					}
				}
				if (!alreadyWorn) {
					auto* form = RE::TESForm::LookupByID<RE::TESObjectARMO>(exp.baseObjectID);
					if (form && IsHeadgearSlot(form) && ActorScope::ArmorEquipAllowed(actor, form) && InventoryHasItem(actor, exp.baseObjectID)) {
						EquipBestEffort(actor, form, exp.signature);
						mismatch = true;
					}
				}
			}

			if (mismatch && a_triesRemaining > 0) {
				ScheduleReconcile(a_actorID, a_expected, static_cast<std::uint8_t>(a_triesRemaining - 1));
			}
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

			std::vector<WornHeadgearItem> preCombat;
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
				preCombat = entry.wornHeadgear;
			}

			if (actor->IsInCombat()) {
				return;
			}

			EquipGate::ScopedBypass gateBypass;

			auto currentlyWorn = FindAllWornHeadgear(actor);

			// Remove headgear not present in the pre-combat set.
			for (auto* worn : currentlyWorn) {
				const auto wornID = worn->GetFormID();
				bool inPreCombat = false;
				for (const auto& pre : preCombat) {
					if (pre.baseObjectID == wornID) {
						inPreCombat = true;
						break;
					}
				}
				if (!inPreCombat) {
					UnequipHeadgear(actor, worn);
				}
			}

			// Restore pre-combat headgear that is missing.
			for (const auto& pre : preCombat) {
				if (pre.baseObjectID == 0) {
					continue;
				}
				bool alreadyWorn = false;
				for (auto* worn : currentlyWorn) {
					if (worn->GetFormID() == pre.baseObjectID) {
						alreadyWorn = true;
						break;
					}
				}
				if (!alreadyWorn) {
					auto* armo = RE::TESForm::LookupByID<RE::TESObjectARMO>(pre.baseObjectID);
					if (armo && IsHeadgearSlot(armo) && ActorScope::ArmorEquipAllowed(actor, armo) &&
						InventoryHasItem(actor, pre.baseObjectID)) {
						EquipBestEffort(actor, armo, pre.signature);
					}
				}
			}

			ScheduleReconcile(a_actorID, preCombat, 3);

			logger::debug("HeadgearToggle: restored pre-combat headgear actor={:08X} count={}", a_actorID, preCombat.size());
		}

		void EquipCombatHeadgear(RE::FormID a_actorID)
		{
			if (!IsEnabled()) {
				return;
			}

			auto* actor = LookupActor(a_actorID);
			if (!actor || !ActorScope::IsAffectedFollower(actor)) {
				return;
			}
			if (!ActorScope::ArmorValidationAllowed(actor)) {
				return;
			}

			const auto pref = CombatEquipPreference::GetEntry(a_actorID, CombatEquipPreference::Category::kHeadgear);
			if (!pref.has_value() || pref->baseObjectID == 0) {
				return;
			}

			for (auto* worn : FindAllWornHeadgear(actor)) {
				if (worn->GetFormID() == pref->baseObjectID) {
					logger::debug("HeadgearToggle: preferred headgear {:08X} already worn on {:08X}", pref->baseObjectID, a_actorID);
					return;
				}
			}

			if (!InventoryHasItem(actor, pref->baseObjectID)) {
				logger::debug("HeadgearToggle: preferred headgear {:08X} not in inventory of {:08X}", pref->baseObjectID, a_actorID);
				return;
			}

			auto* armo = RE::TESForm::LookupByID<RE::TESObjectARMO>(pref->baseObjectID);
			if (!armo || !IsHeadgearSlot(armo) || !ActorScope::ArmorEquipAllowed(actor, armo)) {
				return;
			}

			EquipGate::ScopedBypass gateBypass;

			EquipBestEffort(actor, armo, pref->signature.HasStableIdentity() ? std::optional<InstanceSignature>{ pref->signature } : std::nullopt);

			logger::debug("HeadgearToggle: equipped combat headgear {:08X} on {:08X}", pref->baseObjectID, a_actorID);
		}

		void ScheduleRestore(RE::FormID a_actorID)
		{
			EquipMode::Core::ActionScheduler::NextFrame([a_actorID]() {
				RestoreActorNow(a_actorID);
			});
		}

		void ScheduleReconcile(RE::FormID a_actorID, std::vector<WornHeadgearItem> a_expected, std::uint8_t a_triesRemaining)
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

				if (IsEnabled() && enteredCombat) {
					CaptureSnapshot(actor);
				}

				if (IsEnabled() && enteredCombat) {
					EquipMode::Core::ActionScheduler::NextFrame([actorID]() {
						EquipCombatHeadgear(actorID);
					});
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
		logger::info("HeadgearToggle: installed TESCombatEvent sink");
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
		logger::info("HeadgearToggle: uninstalled TESCombatEvent sink");
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
			entry.wornHeadgear = snap.wornHeadgear;
			out.push_back(std::move(entry));
		}
		return out;
	}

	void SetLoadedEntry(SnapshotEntry a_entry)
	{
		if (a_entry.actorID == 0) {
			return;
		}

		// Mid-combat loads must preserve the serialized pre-combat snapshot.
		// Seed lastState so the next combat event does not overwrite it.
		auto combatState = RE::ACTOR_COMBAT_STATE::kNone;
		if (auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_entry.actorID)) {
			if (actor->IsInCombat()) {
				combatState = RE::ACTOR_COMBAT_STATE::kCombat;
				logger::debug("HeadgearToggle: loaded actor {:08X} already in combat, preserving snapshot", a_entry.actorID);
			}
		}

		std::lock_guard lock(g_mutex);
		auto& entry = g_actorSnapshots[a_entry.actorID];
		entry.wornHeadgear = std::move(a_entry.wornHeadgear);
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
}
