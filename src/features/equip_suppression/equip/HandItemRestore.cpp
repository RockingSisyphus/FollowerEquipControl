#include "HandItemRestore.h"

#include "ActorScope.h"
#include "ContainerMenuUtil.h"
#include "EquipGate.h"
#include "InstanceSignature.h"
#include "InventoryUtil.h"
#include "PluginSettings.h"
#include "SignatureResolve.h"
#include "WeaponBound.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace
{
	[[nodiscard]] const char* FormName(RE::TESBoundObject* a_obj)
	{
		if (!a_obj) {
			return "(null)";
		}
		const char* name = a_obj->GetName();
		return (name && name[0]) ? name : "(unnamed)";
	}
}

namespace FEC
{
	namespace
	{
		using Entry = HandItemRestore::Entry;
		using SlotKind = HandItemRestore::SlotKind;

		struct ActorState
		{
			std::optional<Entry> rightHand{};
			std::optional<Entry> leftHand{};
			std::optional<Entry> ammo{};
		};

		std::mutex g_mutex;
		std::unordered_map<RE::FormID, ActorState> g_actorSnapshots;
		bool g_installed{ false };

		std::mutex g_pendingRefreshMutex;
		std::unordered_set<RE::FormID> g_pendingRefresh;
		std::unordered_set<RE::FormID> g_menuCloseRefresh;

		ContainerMenuUtil::ListenerHandle g_closeListenerHandle{ 0 };

		[[nodiscard]] bool IsEnabled() noexcept
		{
			const auto& cfg = PluginSettings::Get().autoEquipBlocking;
			return cfg.enableNonCombatEquipBlocker && cfg.enableHandItemRestore;
		}

		[[nodiscard]] bool IsEligibleActor(RE::Actor* a_actor)
		{
			return a_actor && !a_actor->IsPlayerRef() && !a_actor->IsDead() && ActorScope::IsAffectedFollower(a_actor);
		}

		[[nodiscard]] bool ShouldRestoreSnapshot(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			if (a_actor->IsInCombat()) {
				return false;
			}
			if (ContainerMenuUtil::IsContainerMenuOpen()) {
				return false;
			}
			return true;
		}

		[[nodiscard]] bool IsStateEmptyLocked(const ActorState& a_state) noexcept
		{
			return !a_state.rightHand.has_value() &&
				!a_state.leftHand.has_value() &&
				!a_state.ammo.has_value();
		}

		[[nodiscard]] bool IsTorch(const RE::TESBoundObject* a_object) noexcept
		{
			return a_object && a_object->GetFormType() == RE::FormType::Light;
		}

		[[nodiscard]] bool IsShield(RE::TESBoundObject* a_object)
		{
			auto* armor = a_object ? a_object->As<RE::TESObjectARMO>() : nullptr;
			return armor && armor->IsShield();
		}

		[[nodiscard]] bool IsRestorableHandItem(RE::TESBoundObject* a_object)
		{
			if (!a_object || IsTorch(a_object) || a_object->Is(RE::FormType::Spell)) {
				return false;
			}
			if (auto* weapon = a_object->As<RE::TESObjectWEAP>()) {
				return !WeaponBound::IsBoundWeapon(weapon);
			}
			if (a_object->GetFormType() == RE::FormType::Scroll) {
				return true;
			}
			return IsShield(a_object);
		}

		[[nodiscard]] bool IsRestorableHandEntry(const Entry& a_entry)
		{
			if (a_entry.baseObjectID == 0) {
				return false;
			}
			auto* form = RE::TESForm::LookupByID(a_entry.baseObjectID);
			auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
			return IsRestorableHandItem(object);
		}

		[[nodiscard]] bool IsRestorableAmmoEntry(const Entry& a_entry)
		{
			if (a_entry.baseObjectID == 0) {
				return false;
			}
			auto* form = RE::TESForm::LookupByID(a_entry.baseObjectID);
			return form && form->As<RE::TESAmmo>();
		}

		[[nodiscard]] bool IsTwoHandedItem(RE::TESBoundObject* a_object)
		{
			if (!a_object) {
				return false;
			}
			if (auto* weap = a_object->As<RE::TESObjectWEAP>()) {
				return weap->IsTwoHandedSword() || weap->IsTwoHandedAxe() || weap->IsBow() || weap->IsCrossbow();
			}
			if (auto* scroll = a_object->As<RE::ScrollItem>()) {
				return scroll->IsTwoHanded();
			}
			return false;
		}

		[[nodiscard]] bool IsEntryTwoHanded(const Entry& a_entry)
		{
			if (a_entry.baseObjectID == 0) {
				return false;
			}
			auto* form = RE::TESForm::LookupByID(a_entry.baseObjectID);
			auto* obj = form ? form->As<RE::TESBoundObject>() : nullptr;
			return IsTwoHandedItem(obj);
		}

		[[nodiscard]] const RE::BGSEquipSlot* GetHandEquipSlot(bool a_leftHand)
		{
			auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
			if (!dom) {
				return nullptr;
			}
			return dom->GetObject<RE::BGSEquipSlot>(a_leftHand ? RE::DEFAULT_OBJECT::kLeftHandEquip : RE::DEFAULT_OBJECT::kRightHandEquip);
		}

		[[nodiscard]] const RE::BGSEquipSlot* GetEquipSlotForHandItem(RE::TESBoundObject* a_object, bool a_leftHand)
		{
			if (!a_object) {
				return GetHandEquipSlot(a_leftHand);
			}
			if (auto* armo = a_object->As<RE::TESObjectARMO>(); armo && armo->IsShield()) {
				return nullptr;
			}
			return GetHandEquipSlot(a_leftHand);
		}

		[[nodiscard]] std::int32_t InventoryCount(RE::Actor* a_actor, RE::TESBoundObject* a_object)
		{
			return InventoryUtil::GetTotalCount(a_actor, a_object);
		}

		[[nodiscard]] bool InventoryHasItem(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			const std::optional<InstanceSignature>& a_sig)
		{
			(void)a_sig;
			return InventoryCount(a_actor, a_object) > 0;
		}

		[[nodiscard]] bool UnequipIfPresent(RE::Actor* a_actor, RE::TESBoundObject* a_object, const RE::BGSEquipSlot* a_slot)
		{
			if (!a_actor || !a_object) {
				return false;
			}
			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return false;
			}

			equipMan->UnequipObject(a_actor, a_object, nullptr, 1, a_slot, false, false, true, true);
			return true;
		}

		[[nodiscard]] bool EquipBestEffort(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			const std::optional<InstanceSignature>& a_sig,
			const RE::BGSEquipSlot* a_slot,
			std::optional<InstanceSignature::EquipState> a_desiredState)
		{
			if (!a_actor || !a_object) {
				return false;
			}
			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return false;
			}

			const std::int32_t inventoryCount = InventoryCount(a_actor, a_object);
			if (inventoryCount <= 0) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("HandItemRestore: EquipBestEffort skipped {:08X} '{}' - base item not in inventory (count={})",
						a_object->GetFormID(), FormName(a_object), inventoryCount);
				}
				return false;
			}

			const InstanceSignature sig = a_sig.has_value() ? *a_sig : InstanceSignature{};
			const auto resolved = SignatureResolve::Resolve(
				a_actor,
				a_object,
				sig,
				a_desiredState,
				SignatureResolve::Policy::kIdentityThenAnyBase);
			auto* xList = resolved.HasXList() ? resolved.xList : nullptr;

			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("HandItemRestore: EquipBestEffort {:08X} '{}' - resolved xList={:p}",
					a_object->GetFormID(), FormName(a_object), static_cast<void*>(xList));
			}

			equipMan->EquipObject(a_actor, a_object, xList, 1, a_slot, false, false, true, true);
			return true;
		}

		void RestoreSnapshot(RE::Actor* a_actor, const ActorState& a_state)
		{
			if (!a_actor) {
				return;
			}

			static thread_local bool g_insideRestore = false;
			if (g_insideRestore) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("HandItemRestore: RestoreSnapshot re-entrancy blocked for {:08X}",
						a_actor->GetFormID());
				}
				return;
			}
			g_insideRestore = true;

			const bool doTrace = spdlog::should_log(spdlog::level::trace);

			if (doTrace) {
				logger::trace("HandItemRestore: RestoreSnapshot BEGIN -- actor {:08X}, RH={:08X} LH={:08X} ammo={:08X}",
					a_actor->GetFormID(),
					a_state.rightHand.has_value() ? a_state.rightHand->baseObjectID : 0u,
					a_state.leftHand.has_value() ? a_state.leftHand->baseObjectID : 0u,
					a_state.ammo.has_value() ? a_state.ammo->baseObjectID : 0u);
			}

			EquipGate::ScopedBypass gateBypass;

			auto resolveDesiredObject = [](const std::optional<Entry>& a_entry) -> RE::TESBoundObject* {
				if (!a_entry.has_value() || a_entry->baseObjectID == 0) {
					return nullptr;
				}

				auto* form = RE::TESForm::LookupByID(a_entry->baseObjectID);
				auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
				return IsRestorableHandItem(object) ? object : nullptr;
			};

			auto sameBase = [](RE::TESBoundObject* a_current, RE::TESBoundObject* a_desired) noexcept {
				return a_current && a_desired && a_current->GetFormID() == a_desired->GetFormID();
			};

			bool appliedAnyRestore = false;
			const auto* initialLeftForm = a_actor->GetEquippedObject(true);
			auto* initialLeftObj = initialLeftForm ? initialLeftForm->As<RE::TESBoundObject>() : nullptr;
			const bool currentLeftIsTorch = IsTorch(initialLeftObj);
			bool rightHandIsTwoHanded = false;
			{
				auto* currentForm = a_actor->GetEquippedObject(false);
				auto* currentObj = currentForm ? currentForm->As<RE::TESBoundObject>() : nullptr;
				auto* currentWeap = currentObj ? currentObj->As<RE::TESObjectWEAP>() : nullptr;
				const bool currentIsBound = currentWeap && WeaponBound::IsBoundWeapon(currentWeap);
				if (doTrace) {
					logger::trace("HandItemRestore: [RightHand] current={:08X} '{}' bound={}",
						currentObj ? currentObj->GetFormID() : 0u,
						FormName(currentObj),
						currentIsBound);
				}

				RE::TESBoundObject* desired = resolveDesiredObject(a_state.rightHand);
				std::optional<InstanceSignature> desiredSig = a_state.rightHand.has_value() ? a_state.rightHand->signature : std::nullopt;

				if (desired) {
					rightHandIsTwoHanded = IsTwoHandedItem(desired);
				}

				if (IsTorch(currentObj)) {
					if (doTrace) logger::trace("HandItemRestore: [RightHand] skipped - current item is a torch");
					rightHandIsTwoHanded = false;
				} else if (currentLeftIsTorch && desired && IsTwoHandedItem(desired)) {
					if (doTrace) logger::trace("HandItemRestore: [RightHand] skipped - left hand has a torch and desired item is two-handed");
					rightHandIsTwoHanded = false;
				} else if (sameBase(currentObj, desired)) {
					if (doTrace) logger::trace("HandItemRestore: [RightHand] same item {:08X} '{}' - already worn, keeping",
						desired->GetFormID(), FormName(desired));
				} else if (!desired) {
					if (doTrace) logger::trace("HandItemRestore: [RightHand] no desired item - unequipping current");
					if (!currentIsBound && currentObj) {
						appliedAnyRestore = UnequipIfPresent(a_actor, currentObj, GetEquipSlotForHandItem(currentObj, false)) || appliedAnyRestore;
					}
				} else if (!InventoryHasItem(a_actor, desired, desiredSig)) {
					if (doTrace) logger::trace("HandItemRestore: [RightHand] desired {:08X} '{}' missing from inventory - unequipping current",
						desired->GetFormID(), FormName(desired));
					if (!currentIsBound && currentObj) {
						appliedAnyRestore = UnequipIfPresent(a_actor, currentObj, GetEquipSlotForHandItem(currentObj, false)) || appliedAnyRestore;
					}
				} else {
					if (doTrace) logger::trace("HandItemRestore: [RightHand] different item - equipping {:08X} '{}'",
						desired->GetFormID(), FormName(desired));
					if (!currentIsBound) {
						appliedAnyRestore = EquipBestEffort(
							a_actor,
							desired,
							desiredSig,
							GetEquipSlotForHandItem(desired, false),
							InstanceSignature::EquipState::kWornRight) || appliedAnyRestore;
					}
				}
			}

			if (!rightHandIsTwoHanded) {
				auto* currentForm = a_actor->GetEquippedObject(true);
				auto* currentObj = currentForm ? currentForm->As<RE::TESBoundObject>() : nullptr;
				auto* currentWeap = currentObj ? currentObj->As<RE::TESObjectWEAP>() : nullptr;
				const bool currentIsBound = currentWeap && WeaponBound::IsBoundWeapon(currentWeap);
				if (doTrace) {
					logger::trace("HandItemRestore: [LeftHand] current={:08X} '{}' bound={}",
						currentObj ? currentObj->GetFormID() : 0u,
						FormName(currentObj),
						currentIsBound);
				}

				RE::TESBoundObject* desired = resolveDesiredObject(a_state.leftHand);
				std::optional<InstanceSignature> desiredSig = a_state.leftHand.has_value() ? a_state.leftHand->signature : std::nullopt;

				if (IsTorch(currentObj)) {
					if (doTrace) logger::trace("HandItemRestore: [LeftHand] skipped - current item is a torch");
				} else if (sameBase(currentObj, desired)) {
					if (doTrace) logger::trace("HandItemRestore: [LeftHand] same item {:08X} '{}' - already worn, keeping",
						desired->GetFormID(), FormName(desired));
				} else if (!desired) {
					if (doTrace) logger::trace("HandItemRestore: [LeftHand] no desired item - unequipping current");
					if (!currentIsBound && currentObj) {
						appliedAnyRestore = UnequipIfPresent(a_actor, currentObj, GetEquipSlotForHandItem(currentObj, true)) || appliedAnyRestore;
					}
				} else if (!InventoryHasItem(a_actor, desired, desiredSig)) {
					if (doTrace) logger::trace("HandItemRestore: [LeftHand] desired {:08X} '{}' missing from inventory - unequipping current",
						desired->GetFormID(), FormName(desired));
					if (!currentIsBound && currentObj) {
						appliedAnyRestore = UnequipIfPresent(a_actor, currentObj, GetEquipSlotForHandItem(currentObj, true)) || appliedAnyRestore;
					}
				} else {
					if (doTrace) logger::trace("HandItemRestore: [LeftHand] different item - equipping {:08X} '{}'",
						desired->GetFormID(), FormName(desired));
					if (!currentIsBound) {
						appliedAnyRestore = EquipBestEffort(
							a_actor,
							desired,
							desiredSig,
							GetEquipSlotForHandItem(desired, true),
							InstanceSignature::EquipState::kWornLeft) || appliedAnyRestore;
					}
				}
			} else {
				if (doTrace) logger::trace("HandItemRestore: [LeftHand] skipped - right hand is two-handed");
			}

			{
				auto* currentAmmo = a_actor->GetCurrentAmmo();
				if (doTrace) logger::trace("HandItemRestore: [Ammo] current={:08X}",
					currentAmmo ? currentAmmo->GetFormID() : 0u);
				RE::TESAmmo* desiredAmmo = nullptr;
				if (a_state.ammo.has_value() && a_state.ammo->baseObjectID != 0) {
					auto* form = RE::TESForm::LookupByID(a_state.ammo->baseObjectID);
					desiredAmmo = form ? form->As<RE::TESAmmo>() : nullptr;
				}

				if (currentAmmo && desiredAmmo && currentAmmo->GetFormID() == desiredAmmo->GetFormID()) {
					if (doTrace) logger::trace("HandItemRestore: [Ammo] same ammo {:08X} - already equipped, keeping",
						desiredAmmo->GetFormID());
				} else if (!desiredAmmo) {
					if (doTrace) logger::trace("HandItemRestore: [Ammo] no desired ammo - unequipping current");
					if (currentAmmo) {
						appliedAnyRestore = UnequipIfPresent(a_actor, currentAmmo, nullptr) || appliedAnyRestore;
					}
				} else if (!InventoryHasItem(a_actor, desiredAmmo, a_state.ammo->signature)) {
					if (doTrace) logger::trace("HandItemRestore: [Ammo] desired ammo {:08X} missing from inventory - unequipping current",
						desiredAmmo->GetFormID());
					if (currentAmmo) {
						appliedAnyRestore = UnequipIfPresent(a_actor, currentAmmo, nullptr) || appliedAnyRestore;
					}
				} else {
					if (doTrace) logger::trace("HandItemRestore: [Ammo] different ammo - equipping {:08X}",
						desiredAmmo->GetFormID());
					appliedAnyRestore = EquipBestEffort(a_actor, desiredAmmo, a_state.ammo->signature, nullptr, std::nullopt) || appliedAnyRestore;
				}
			}

			if (appliedAnyRestore) {
				if (doTrace) logger::trace("HandItemRestore: [Enchantment] dispelling worn item enchantments for {:08X}",
					a_actor->GetFormID());
				a_actor->DispelWornItemEnchantments();

				if (doTrace) logger::trace("HandItemRestore: [Enchantment] reapplying worn item enchantments for {:08X}",
					a_actor->GetFormID());
				a_actor->CastPermanentMagic(true, false, false, false);
			} else if (doTrace) {
				logger::trace("HandItemRestore: no restore changes applied for {:08X}; enchantment refresh skipped",
					a_actor->GetFormID());
			}

			if (doTrace) logger::trace("HandItemRestore: RestoreSnapshot END - actor {:08X}", a_actor->GetFormID());
			g_insideRestore = false;
		}

		[[nodiscard]] bool TakePending(RE::FormID a_actorID)
		{
			std::lock_guard lock(g_pendingRefreshMutex);
			return g_pendingRefresh.erase(a_actorID) != 0;
		}

		void ClearPending(RE::FormID a_actorID)
		{
			std::lock_guard lock(g_pendingRefreshMutex);
			g_pendingRefresh.erase(a_actorID);
		}

		void AddMenuCloseRefresh(RE::FormID a_actorID)
		{
			if (a_actorID == 0) {
				return;
			}

			std::lock_guard lock(g_pendingRefreshMutex);
			if (!g_menuCloseRefresh.insert(a_actorID).second) {
				logger::trace("HandItemRestore: actor {:08X} already queued for menu-close reconcile", a_actorID);
			}
		}

		std::vector<RE::FormID> TakeMenuCloseRefreshes()
		{
			std::vector<RE::FormID> ids;
			std::lock_guard lock(g_pendingRefreshMutex);
			ids.reserve(g_menuCloseRefresh.size());
			for (const auto actorID : g_menuCloseRefresh) {
				ids.push_back(actorID);
			}
			g_menuCloseRefresh.clear();
			return ids;
		}

		void ClearAllPendingRestores()
		{
			std::size_t pendingCount = 0;
			std::size_t menuCloseCount = 0;
			{
				std::lock_guard lock(g_pendingRefreshMutex);
				pendingCount = g_pendingRefresh.size();
				menuCloseCount = g_menuCloseRefresh.size();
				g_pendingRefresh.clear();
				g_menuCloseRefresh.clear();
			}

			if (pendingCount != 0 || menuCloseCount != 0) {
				logger::trace("HandItemRestore: cleared {} pending restore request(s) and {} menu-close request(s)",
					pendingCount, menuCloseCount);
			}
		}

		void RunRestoreTask(RE::FormID a_actorID, bool a_requirePending)
		{
			if (!g_installed || !IsEnabled()) {
				if (a_requirePending) {
					ClearPending(a_actorID);
				}
				return;
			}

			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
			if (!IsEligibleActor(actor)) {
				if (a_requirePending) {
					ClearPending(a_actorID);
				}
				logger::trace("HandItemRestore: deferred restore - actor {:08X} not found, dead, player, or ineligible", a_actorID);
				return;
			}

			if (ContainerMenuUtil::IsContainerMenuOpen()) {
				if (a_requirePending) {
					ClearPending(a_actorID);
				}
				AddMenuCloseRefresh(a_actorID);
				logger::trace("HandItemRestore: deferred restore - menu open for {:08X}, queued for menu close", a_actorID);
				return;
			}

			if (a_requirePending && !TakePending(a_actorID)) {
				logger::trace("HandItemRestore: deferred restore - actor {:08X} no longer pending", a_actorID);
				return;
			}

			if (!ShouldRestoreSnapshot(actor)) {
				logger::trace("HandItemRestore: deferred restore - ShouldRestoreSnapshot=false for {:08X}, skipping", a_actorID);
				return;
			}

			ActorState snap{};
			{
				std::lock_guard lock(g_mutex);
				auto it = g_actorSnapshots.find(a_actorID);
				if (it == g_actorSnapshots.end()) {
					logger::trace("HandItemRestore: deferred restore - no snapshot for {:08X}, skipping", a_actorID);
					return;
				}
				snap = it->second;
			}

			logger::trace("HandItemRestore: deferred restore - running RestoreSnapshot for {:08X}", a_actorID);
			RestoreSnapshot(actor, snap);
		}

		[[nodiscard]] bool QueueRestoreTask(RE::FormID a_actorID, bool a_requirePending)
		{
			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				return false;
			}

			taskInterface->AddTask([a_actorID, a_requirePending]() {
				RunRestoreTask(a_actorID, a_requirePending);
			});
			return true;
		}

		[[nodiscard]] bool QueuePendingRestore(RE::FormID a_actorID, const char* a_reason)
		{
			if (a_actorID == 0) {
				return false;
			}

			{
				std::lock_guard lock(g_pendingRefreshMutex);
				if (!g_pendingRefresh.insert(a_actorID).second) {
					logger::trace("HandItemRestore: actor {:08X} already pending, skipping duplicate {} request",
						a_actorID, a_reason ? a_reason : "reconcile");
					return false;
				}
			}

			if (!QueueRestoreTask(a_actorID, true)) {
				ClearPending(a_actorID);
				logger::trace("HandItemRestore: SKSE task interface unavailable, dropped {} request for {:08X}",
					a_reason ? a_reason : "reconcile", a_actorID);
				return false;
			}

			return true;
		}

		void FlushMenuCloseRefreshes()
		{
			const auto ids = TakeMenuCloseRefreshes();
			if (ids.empty()) {
				return;
			}

			if (!g_installed || !IsEnabled()) {
				logger::trace("HandItemRestore: dropped {} menu-close reconcile request(s) because the feature is disabled",
					ids.size());
				return;
			}

			logger::trace("HandItemRestore: queueing {} menu-close reconcile request(s)", ids.size());
			for (const auto actorID : ids) {
				(void)QueuePendingRestore(actorID, "menu-close reconcile");
			}
		}
	}

	void HandItemRestore::Install()
	{
		if (g_installed) {
			return;
		}

		g_closeListenerHandle = ContainerMenuUtil::AddOnContainerMenuCloseListener([]() {
			FlushMenuCloseRefreshes();
		});

		g_installed = true;
	}

	std::vector<HandItemRestore::SnapshotEntry> HandItemRestore::SnapshotEntries()
	{
		std::vector<SnapshotEntry> out;
		std::lock_guard lock(g_mutex);
		out.reserve(g_actorSnapshots.size() * 3);
		for (const auto& [actorID, snap] : g_actorSnapshots) {
			if (snap.rightHand.has_value()) {
				SnapshotEntry entry{};
				entry.actorID = actorID;
				entry.kind = SlotKind::kRightHand;
				entry.entry = *snap.rightHand;
				out.push_back(std::move(entry));
			}
			if (snap.leftHand.has_value()) {
				SnapshotEntry entry{};
				entry.actorID = actorID;
				entry.kind = SlotKind::kLeftHand;
				entry.entry = *snap.leftHand;
				out.push_back(std::move(entry));
			}
			if (snap.ammo.has_value()) {
				SnapshotEntry entry{};
				entry.actorID = actorID;
				entry.kind = SlotKind::kAmmo;
				entry.entry = *snap.ammo;
				out.push_back(std::move(entry));
			}
		}
		return out;
	}

	void HandItemRestore::SetLoadedEntry(RE::FormID a_actorID, SlotKind a_kind, Entry a_entry)
	{
		if (a_actorID == 0) {
			return;
		}

		std::lock_guard lock(g_mutex);
		auto& dest = g_actorSnapshots[a_actorID];
		bool shouldClear = (a_entry.baseObjectID == 0);
		if (!shouldClear) {
			if (a_kind == SlotKind::kAmmo) {
				shouldClear = !IsRestorableAmmoEntry(a_entry);
			} else {
				shouldClear = !IsRestorableHandEntry(a_entry);
			}
		}
		switch (a_kind) {
		case SlotKind::kRightHand:
			if (shouldClear) {
				dest.rightHand.reset();
			} else {
				dest.rightHand = std::move(a_entry);
			}
			break;
		case SlotKind::kLeftHand:
			if (shouldClear) {
				dest.leftHand.reset();
			} else {
				dest.leftHand = std::move(a_entry);
			}
			break;
		case SlotKind::kAmmo:
			if (shouldClear) {
				dest.ammo.reset();
			} else {
				dest.ammo = std::move(a_entry);
			}
			break;
		default:
			break;
		}

		if (IsStateEmptyLocked(dest)) {
			g_actorSnapshots.erase(a_actorID);
		}
	}

	void HandItemRestore::ClearSnapshots()
	{
		{
			std::lock_guard lock(g_mutex);
			g_actorSnapshots.clear();
		}

		ClearAllPendingRestores();
	}

	void HandItemRestore::EraseActor(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return;
		}

		{
			std::lock_guard lock(g_mutex);
			g_actorSnapshots.erase(a_actorID);
		}

		{
			std::lock_guard lock(g_pendingRefreshMutex);
			g_pendingRefresh.erase(a_actorID);
			g_menuCloseRefresh.erase(a_actorID);
		}
	}

	void HandItemRestore::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_closeListenerHandle != 0) {
			ContainerMenuUtil::RemoveOnContainerMenuCloseListener(g_closeListenerHandle);
			g_closeListenerHandle = 0;
		}

		ClearAllPendingRestores();

		ClearSnapshots();

		g_installed = false;
	}

	void HandItemRestore::CaptureUserEquip(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		bool a_leftHand,
		const std::optional<InstanceSignature>& a_sig)
	{
		if (!g_installed || !IsEnabled()) {
			return;
		}
		if (!IsEligibleActor(a_actor) || !a_object) {
			return;
		}

		const auto objectID = a_object->GetFormID();
		if (objectID == 0) {
			return;
		}

		const bool isAmmo = a_object->GetFormType() == RE::FormType::Ammo;
		const bool isRestorableHandItem = IsRestorableHandItem(a_object);
		if (!isAmmo && !isRestorableHandItem) {
			return;
		}

		Entry entry{};
		entry.baseObjectID = objectID;
		entry.signature = a_sig;

		const bool doDebug = spdlog::should_log(spdlog::level::debug);

		std::lock_guard lock(g_mutex);
		auto& state = g_actorSnapshots[a_actor->GetFormID()];
		if (isAmmo) {
			state.ammo = std::move(entry);
			if (doDebug) {
				logger::debug("HandItemRestore: CaptureEquip ammo {:08X} ({}) actor={:08X}",
					objectID, a_object->GetName(), a_actor->GetFormID());
			}
			return;
		}
		if (a_object->IsWeapon() || a_object->GetFormType() == RE::FormType::Scroll) {
			if (a_leftHand) {
				state.leftHand = std::move(entry);
				if (doDebug) {
					logger::debug("HandItemRestore: CaptureEquip leftHand {:08X} ({}) actor={:08X}",
						objectID, a_object->GetName(), a_actor->GetFormID());
				}
				if (state.rightHand.has_value() && IsEntryTwoHanded(*state.rightHand)) {
					state.rightHand.reset();
				}
			} else {
				state.rightHand = std::move(entry);
				if (doDebug) {
					logger::debug("HandItemRestore: CaptureEquip rightHand {:08X} ({}) actor={:08X}",
						objectID, a_object->GetName(), a_actor->GetFormID());
				}
				if (IsTwoHandedItem(a_object)) {
					state.leftHand.reset();
				}
			}
			return;
		}
		if (auto* armo = a_object->As<RE::TESObjectARMO>(); armo && armo->IsShield()) {
			state.leftHand = std::move(entry);
			if (doDebug) {
				logger::debug("HandItemRestore: CaptureEquip shield->leftHand {:08X} ({}) actor={:08X}",
					objectID, a_object->GetName(), a_actor->GetFormID());
			}
			if (state.rightHand.has_value() && IsEntryTwoHanded(*state.rightHand)) {
				state.rightHand.reset();
			}
		}
	}

	void HandItemRestore::CaptureUserEquip(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		bool a_leftHand,
		RE::ExtraDataList* a_xList,
		bool a_hasSelection)
	{
		std::optional<InstanceSignature> sig;
		if (a_xList) {
			sig = BuildInstanceSignature(*a_xList, a_object);
		} else if (a_hasSelection) {
			sig = InstanceSignature{};
		}
		CaptureUserEquip(a_actor, a_object, a_leftHand, sig);
	}

	void HandItemRestore::CaptureUserUnequip(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		bool a_leftHand)
	{
		if (!g_installed || !IsEnabled()) {
			return;
		}
		if (!IsEligibleActor(a_actor) || !a_object) {
			return;
		}

		std::lock_guard lock(g_mutex);
		auto it = g_actorSnapshots.find(a_actor->GetFormID());
		if (it == g_actorSnapshots.end()) {
			return;
		}
		auto& state = it->second;

		const bool doDebug = spdlog::should_log(spdlog::level::debug);

		if (a_object->GetFormType() == RE::FormType::Ammo) {
			state.ammo.reset();
			if (doDebug) {
				logger::debug("HandItemRestore: CaptureUnequip ammo {:08X} ({}) actor={:08X}",
					a_object->GetFormID(), a_object->GetName(), a_actor->GetFormID());
			}
		} else if (a_object->IsWeapon() || a_object->GetFormType() == RE::FormType::Scroll) {
			if (a_leftHand) {
				state.leftHand.reset();
				if (doDebug) {
					logger::debug("HandItemRestore: CaptureUnequip leftHand {:08X} ({}) actor={:08X}",
						a_object->GetFormID(), a_object->GetName(), a_actor->GetFormID());
				}
			} else {
				state.rightHand.reset();
				if (doDebug) {
					logger::debug("HandItemRestore: CaptureUnequip rightHand {:08X} ({}) actor={:08X}",
						a_object->GetFormID(), a_object->GetName(), a_actor->GetFormID());
				}
			}
		} else if (auto* armo = a_object->As<RE::TESObjectARMO>(); armo && armo->IsShield()) {
			state.leftHand.reset();
			if (doDebug) {
				logger::debug("HandItemRestore: CaptureUnequip shield {:08X} ({}) actor={:08X}",
					a_object->GetFormID(), a_object->GetName(), a_actor->GetFormID());
			}
		}

		if (IsStateEmptyLocked(state)) {
			const auto actorID = a_actor->GetFormID();
			g_actorSnapshots.erase(it);
			if (doDebug) {
				logger::debug("HandItemRestore: CaptureUnequip actor={:08X} snapshot empty, erased", actorID);
			}
		}
	}

	void HandItemRestore::CaptureBaselineIfEmpty(RE::Actor* a_actor)
	{
		if (!g_installed || !IsEnabled()) {
			return;
		}
		if (!IsEligibleActor(a_actor)) {
			return;
		}

		{
			std::lock_guard lock(g_mutex);
			if (g_actorSnapshots.find(a_actor->GetFormID()) != g_actorSnapshots.end()) {
				return;
			}
		}

		ActorState baseline{};

		if (auto* rhForm = a_actor->GetEquippedObject(false)) {
			auto* rhObj = rhForm->As<RE::TESBoundObject>();
			if (IsRestorableHandItem(rhObj)) {
				baseline.rightHand = Entry{ rhObj->GetFormID(), std::nullopt };
			}
		}

		const bool rhIsTwoHanded = baseline.rightHand.has_value() && IsEntryTwoHanded(*baseline.rightHand);
		if (!rhIsTwoHanded) {
			if (auto* lhForm = a_actor->GetEquippedObject(true)) {
				auto* lhObj = lhForm->As<RE::TESBoundObject>();
				if (IsRestorableHandItem(lhObj)) {
					baseline.leftHand = Entry{ lhObj->GetFormID(), std::nullopt };
				}
			}
		}

		if (auto* ammo = a_actor->GetCurrentAmmo()) {
			baseline.ammo = Entry{ ammo->GetFormID(), std::nullopt };
		}

		if (IsStateEmptyLocked(baseline)) {
			return;
		}

		{
			std::lock_guard lock(g_mutex);
			if (g_actorSnapshots.find(a_actor->GetFormID()) == g_actorSnapshots.end()) {
				g_actorSnapshots[a_actor->GetFormID()] = std::move(baseline);
			}
		}
	}

	void HandItemRestore::RequestReconcile(RE::Actor* a_actor)
	{
		if (!g_installed || !IsEnabled()) {
			return;
		}
		if (!IsEligibleActor(a_actor)) {
			return;
		}
		if (ContainerMenuUtil::IsContainerMenuOpen()) {
			logger::trace("HandItemRestore: RequestReconcile skipped - trade menu open");
			return;
		}

		const auto actorID = a_actor->GetFormID();
		if (spdlog::should_log(spdlog::level::trace)) {
			logger::trace("HandItemRestore: RequestReconcile - actor {:08X}", actorID);
		}

		(void)QueuePendingRestore(actorID, "reconcile");
	}

	void HandItemRestore::RequestReconcileAfterMenuClose(RE::Actor* a_actor)
	{
		if (!g_installed || !IsEnabled()) {
			return;
		}
		if (!IsEligibleActor(a_actor)) {
			return;
		}

		const auto actorID = a_actor->GetFormID();
		if (ContainerMenuUtil::IsContainerMenuOpen()) {
			AddMenuCloseRefresh(actorID);
			logger::trace("HandItemRestore: RequestReconcileAfterMenuClose - actor {:08X} queued until trade menu closes", actorID);
			return;
		}

		if (spdlog::should_log(spdlog::level::trace)) {
			logger::trace("HandItemRestore: RequestReconcileAfterMenuClose - actor {:08X} queued immediately", actorID);
		}

		(void)QueuePendingRestore(actorID, "inventory-refresh reconcile");
	}

	std::vector<HandItemRestore::Entry> HandItemRestore::GetSnapshotEntries(RE::FormID a_actorID)
	{
		std::vector<Entry> result;
		std::lock_guard lock(g_mutex);
		auto it = g_actorSnapshots.find(a_actorID);
		if (it == g_actorSnapshots.end()) {
			return result;
		}
		const auto& snap = it->second;
		if (snap.rightHand.has_value() && snap.rightHand->baseObjectID != 0) {
			result.push_back(*snap.rightHand);
		}
		if (snap.leftHand.has_value() && snap.leftHand->baseObjectID != 0) {
			result.push_back(*snap.leftHand);
		}
		if (snap.ammo.has_value() && snap.ammo->baseObjectID != 0) {
			result.push_back(*snap.ammo);
		}
		return result;
	}
}
