#include "OutfitSnapshotRestore.h"

#include "ActorScope.h"
#include "ContainerMenuUtil.h"
#include "EquipGate.h"
#include "InstanceSignature.h"
#include "InventoryUtil.h"
#include "SignatureResolve.h"
#include "PluginSettings.h"

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
		using Entry = OutfitSnapshotRestore::Entry;

		struct ActorState
		{
			std::unordered_map<std::uint32_t, Entry> armorBySlot{};
		};

		std::mutex g_mutex;
		std::unordered_map<RE::FormID, ActorState> g_actorSnapshots;
		bool g_installed{ false };

		// Actors queued for deferred restore after ContainerMenu closes.
		std::mutex g_pendingRefreshMutex;
		std::unordered_set<RE::FormID> g_pendingRefresh;

		ContainerMenuUtil::ListenerHandle g_closeListenerHandle{ 0 };

		[[nodiscard]] bool IsEnabled() noexcept
		{
			const auto& cfg = PluginSettings::Get().outfitSync;
			return cfg.enableUpdateNpcOutfitSuppression && cfg.enableOutfitSnapshotRestore;
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
			return a_state.armorBySlot.empty();
		}

		template <class Fn>
		void ForEachSlotBit(std::uint32_t a_mask, Fn&& a_fn)
		{
			if (a_mask == 0) {
				return;
			}
			for (std::uint32_t bit = 1u; bit != 0; bit <<= 1u) {
				if ((a_mask & bit) != 0) {
					a_fn(bit);
				}
			}
		}

		[[nodiscard]] RE::TESObjectARMO* AsTrackedArmor(RE::TESBoundObject* a_object)
		{
			if (!a_object) {
				return nullptr;
			}
			auto* armo = a_object->As<RE::TESObjectARMO>();
			if (!armo || armo->IsShield() || !armo->GetPlayable()) {
				return nullptr;
			}
			return armo;
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

		void UnequipIfPresent(RE::Actor* a_actor, RE::TESBoundObject* a_object, const RE::BGSEquipSlot* a_slot)
		{
			if (!a_actor || !a_object) {
				return;
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
			std::optional<InstanceSignature::EquipState> a_desiredState)
		{
			if (!a_actor || !a_object) {
				return;
			}
			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return;
			}

			const std::int32_t inventoryCount = InventoryCount(a_actor, a_object);
			if (inventoryCount <= 0) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("SnapshotRestore: EquipBestEffort skipped {:08X} '{}' — base item not in inventory (count={})",
						a_object->GetFormID(), FormName(a_object), inventoryCount);
				}
				return;
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
				logger::trace("SnapshotRestore: EquipBestEffort {:08X} '{}' — resolved xList={:p}",
					a_object->GetFormID(), FormName(a_object), static_cast<void*>(xList));
			}

			equipMan->EquipObject(a_actor, a_object, xList, 1, a_slot, false, false, true, true);
		}

		void RestoreSnapshot(RE::Actor* a_actor, const ActorState& a_state)
		{
			if (!a_actor) {
				return;
			}

			// Own equip and unequip calls can re-enter through UpdateNPCOutfit suppression.
			static thread_local bool g_insideRestore = false;
			if (g_insideRestore) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("SnapshotRestore: RestoreSnapshot re-entrancy blocked for {:08X}",
						a_actor->GetFormID());
				}
				return;
			}
			g_insideRestore = true;

			const bool doTrace = spdlog::should_log(spdlog::level::trace);

			if (doTrace) {
				logger::trace("SnapshotRestore: RestoreSnapshot BEGIN -- actor {:08X}, armorSlots={}",
					a_actor->GetFormID(),
					a_state.armorBySlot.size());
			}

			EquipGate::ScopedBypass gateBypass;

			// Skip body armor for actors that cannot equip it; restore would be ineffective
			// and could strip armor placed by other systems.
			if (ActorScope::BodyEquipAllowed(a_actor)) {
				std::unordered_map<RE::FormID, Entry> desiredByBase;
				desiredByBase.reserve(a_state.armorBySlot.size());
				for (const auto& [slotBit, entry] : a_state.armorBySlot) {
					if (entry.baseObjectID == 0) {
						continue;
					}
					desiredByBase.emplace(entry.baseObjectID, entry);
				}

				std::unordered_set<RE::FormID> missing;
				if (doTrace) logger::trace("SnapshotRestore: [Armor] processing {} desired armor pieces", desiredByBase.size());
				for (const auto& [baseID, entry] : desiredByBase) {
					auto* form = RE::TESForm::LookupByID(baseID);
					auto* armo = form ? form->As<RE::TESObjectARMO>() : nullptr;
					if (!armo || armo->IsShield() || !armo->GetPlayable()) {
						if (doTrace) logger::trace("SnapshotRestore: [Armor] {:08X} — not tracked playable armor, marking missing", baseID);
						missing.insert(baseID);
						continue;
					}
					if (!InventoryHasItem(a_actor, armo, entry.signature)) {
						if (doTrace) logger::trace("SnapshotRestore: [Armor] {:08X} '{}' — not in inventory, marking missing",
							baseID, FormName(armo));
						missing.insert(baseID);
						continue;
					}
					if (a_actor->GetWornArmor(baseID)) {
						if (doTrace) logger::trace("SnapshotRestore: [Armor] {:08X} '{}' — still worn, keeping",
							baseID, FormName(armo));
						continue;
					}
					if (doTrace) logger::trace("SnapshotRestore: [Armor] {:08X} '{}' — not currently worn, equipping fresh",
						baseID, FormName(armo));
					EquipBestEffort(a_actor, armo, entry.signature, nullptr, std::nullopt);
				}

				for (std::uint32_t bit = 1u; bit != 0; bit <<= 1u) {
					auto slot = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(bit);
					auto* worn = a_actor->GetWornArmor(slot);
					if (!worn || worn->IsShield()) {
						continue;
					}
					// Non-playable worn items belong to external systems and are not tracked.
					if (!worn->GetPlayable()) {
						continue;
					}
					const auto baseID = worn->GetFormID();
					if (desiredByBase.find(baseID) == desiredByBase.end() || missing.find(baseID) != missing.end()) {
						if (doTrace) logger::trace("SnapshotRestore: [Armor] unequipping unwanted {:08X} '{}'",
							baseID, FormName(worn));
						UnequipIfPresent(a_actor, worn, nullptr);
					}
				}
			}

			// Refresh worn-item enchantments without unequip/equip cycling; UnequipObject can free xLists.
			if (doTrace) logger::trace("SnapshotRestore: [Enchantment] dispelling worn item enchantments for {:08X}",
				a_actor->GetFormID());
			a_actor->DispelWornItemEnchantments();

			if (doTrace) logger::trace("SnapshotRestore: [Enchantment] reapplying worn item enchantments for {:08X}",
				a_actor->GetFormID());
			a_actor->CastPermanentMagic(true, false, false, false);

			if (doTrace) logger::trace("SnapshotRestore: RestoreSnapshot END — actor {:08X}", a_actor->GetFormID());
			g_insideRestore = false;
		}

		bool MaybeReseedArmorFromWorn(RE::Actor* a_actor);

		// ContainerMenu close fires during teardown; defer restore to the next frame
		// before mutating inventory. Use full restore because trade-session mods can
		// strip armor, and enchantment refresh alone would leave the wrong armor state.
		void DrainPendingRestores()
		{
			std::vector<RE::FormID> pending;
			{
				std::lock_guard lock(g_pendingRefreshMutex);
				if (g_pendingRefresh.empty()) {
					return;
				}
				pending.assign(g_pendingRefresh.begin(), g_pendingRefresh.end());
				g_pendingRefresh.clear();
			}

			logger::trace("SnapshotRestore: menu closed — scheduling deferred restore for {} actor(s)",
				pending.size());

			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}

			for (auto actorID : pending) {
				taskInterface->AddTask([actorID]() {
					auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
					if (!actor || actor->IsDead()) {
						logger::trace("SnapshotRestore: deferred restore — actor {:08X} not found or dead, skipping", actorID);
						return;
					}
					if (!ShouldRestoreSnapshot(actor)) {
						logger::trace("SnapshotRestore: deferred restore — ShouldRestoreSnapshot=false for {:08X}, skipping", actorID);
						return;
					}
					MaybeReseedArmorFromWorn(actor);
					ActorState snap{};
					{
						std::lock_guard lock(g_mutex);
						auto it = g_actorSnapshots.find(actorID);
						if (it == g_actorSnapshots.end()) {
							logger::trace("SnapshotRestore: deferred restore — no snapshot for {:08X}, skipping", actorID);
							return;
						}
						snap = it->second;
					}
					logger::trace("SnapshotRestore: deferred restore — running RestoreSnapshot for {:08X}", actorID);
					RestoreSnapshot(actor, snap);
				});
			}
		}

		// Enumerate worn playable armor by slot bit. Shields and non-playable armor are not tracked.
		void EnumerateCurrentWornArmor(
			RE::Actor* a_actor,
			std::unordered_map<std::uint32_t, RE::FormID>& a_out)
		{
			if (!a_actor) {
				return;
			}
			auto wornInv = a_actor->GetInventory([](RE::TESBoundObject& a_obj) {
				return a_obj.IsArmor();
			});
			for (auto& [obj, data] : wornInv) {
				if (!obj) {
					continue;
				}
				auto* armo = obj->As<RE::TESObjectARMO>();
				if (!armo) {
					continue;
				}
				if (armo->IsShield() || !armo->GetPlayable()) {
					continue;
				}
				auto* entry = data.second.get();
				if (!entry || !entry->extraLists || entry->extraLists->empty()) {
					continue;
				}
				bool isWorn = false;
				for (auto* xList : *entry->extraLists) {
					if (!xList) {
						continue;
					}
					if (xList->HasType<RE::ExtraWorn>() || xList->HasType<RE::ExtraWornLeft>()) {
						isWorn = true;
						break;
					}
				}
				if (!isWorn) {
					continue;
				}
				const auto baseID = obj->GetFormID();
				const auto mask = static_cast<std::uint32_t>(armo->GetSlotMask());
				if (mask == 0) {
					continue;
				}
				ForEachSlotBit(mask, [&](std::uint32_t bit) {
					a_out[bit] = baseID;
				});
			}
		}

		// If other systems changed worn armor while UpdateNPCOutfit was suppressed,
		// reseed the armor snapshot from worn state so RestoreSnapshot preserves it.
		// Empty worn armor means a hard-reset path; keep the snapshot so saved armor can
		// be restored. Compare by baseID only, not tempering or enchantment signature.
		bool MaybeReseedArmorFromWorn(RE::Actor* a_actor)
		{
			const auto& cfg = PluginSettings::Get().outfitSync;
			if (!cfg.allowOutfitChanges) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("SnapshotRestore: allowOutfitChanges off, skipping reseed check");
				}
				return false;
			}
			if (!ActorScope::BodyEquipAllowed(a_actor)) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("SnapshotRestore: BodyEquipAllowed=false for {:08X}, no reseed check",
						a_actor ? a_actor->GetFormID() : 0u);
				}
				return false;
			}

			std::unordered_map<std::uint32_t, RE::FormID> wornBySlot;
			EnumerateCurrentWornArmor(a_actor, wornBySlot);

			if (wornBySlot.empty()) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("SnapshotRestore: actor {:08X} has no worn armor (hard-reset path), keeping snapshot",
						a_actor->GetFormID());
				}
				return false;
			}

			bool same = false;
			{
				std::lock_guard lock(g_mutex);
				auto it = g_actorSnapshots.find(a_actor->GetFormID());
				if (it != g_actorSnapshots.end()) {
					same = true;
					const auto& snapArmor = it->second.armorBySlot;
					if (snapArmor.size() != wornBySlot.size()) {
						same = false;
					} else {
						for (auto& [slot, baseID] : wornBySlot) {
							auto sit = snapArmor.find(slot);
							if (sit == snapArmor.end() || sit->second.baseObjectID != baseID) {
								same = false;
								break;
							}
						}
					}
				}
			}

			if (same) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("SnapshotRestore: worn matches snapshot for {:08X} ({} slots), no reseed",
						a_actor->GetFormID(), wornBySlot.size());
				}
				return false;
			}

			{
				std::lock_guard lock(g_mutex);
				auto& state = g_actorSnapshots[a_actor->GetFormID()];
				state.armorBySlot.clear();
				for (auto& [slot, baseID] : wornBySlot) {
					Entry e{};
					e.baseObjectID = baseID;
					state.armorBySlot[slot] = std::move(e);
				}
			}
			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug(
					"SnapshotRestore: armor reseeded from worn for {:08X} (allowOutfitChanges, {} slots)",
					a_actor->GetFormID(), wornBySlot.size());
			}
			return true;
		}
	}

	void OutfitSnapshotRestore::Install()
	{
		if (g_installed) {
			return;
		}

		g_closeListenerHandle = ContainerMenuUtil::AddOnContainerMenuCloseListener([]() {
			if (!g_installed || !IsEnabled()) {
				return;
			}
			DrainPendingRestores();
		});

		g_installed = true;
	}

	std::vector<OutfitSnapshotRestore::SnapshotEntry> OutfitSnapshotRestore::SnapshotEntries()
	{
		std::vector<SnapshotEntry> out;
		std::lock_guard lock(g_mutex);
		out.reserve(g_actorSnapshots.size());
		for (const auto& [actorID, snap] : g_actorSnapshots) {
			for (const auto& [slotID, armor] : snap.armorBySlot) {
				SnapshotEntry entry{};
				entry.actorID = actorID;
				entry.slotID = slotID;
				entry.entry = armor;
				out.push_back(std::move(entry));
			}
		}
		return out;
	}

	std::unordered_set<RE::FormID> OutfitSnapshotRestore::GetSnapshotBaseObjectIDs(RE::FormID a_actorID)
	{
		std::unordered_set<RE::FormID> result;
		std::lock_guard lock(g_mutex);
		auto it = g_actorSnapshots.find(a_actorID);
		if (it == g_actorSnapshots.end()) {
			return result;
		}
		const auto& snap = it->second;
		for (const auto& [slotBit, entry] : snap.armorBySlot) {
			if (entry.baseObjectID != 0) {
				result.insert(entry.baseObjectID);
			}
		}
		return result;
	}

	std::vector<OutfitSnapshotRestore::Entry> OutfitSnapshotRestore::GetSnapshotEntries(RE::FormID a_actorID)
	{
		std::vector<Entry> result;
		std::lock_guard lock(g_mutex);
		auto it = g_actorSnapshots.find(a_actorID);
		if (it == g_actorSnapshots.end()) {
			return result;
		}
		const auto& snap = it->second;
		for (const auto& [slotBit, entry] : snap.armorBySlot) {
			if (entry.baseObjectID != 0) {
				result.push_back(entry);
			}
		}
		return result;
	}

	void OutfitSnapshotRestore::SetLoadedEntry(RE::FormID a_actorID, std::uint32_t a_slotID, Entry a_entry)
	{
		if (a_actorID == 0) {
			return;
		}

		std::lock_guard lock(g_mutex);
		auto& dest = g_actorSnapshots[a_actorID];
		const bool shouldClear = (a_entry.baseObjectID == 0);
		if (shouldClear) {
			dest.armorBySlot.erase(a_slotID);
		} else if (a_slotID != 0) {
			dest.armorBySlot[a_slotID] = std::move(a_entry);
		}

		if (IsStateEmptyLocked(dest)) {
			g_actorSnapshots.erase(a_actorID);
		}
	}

	void OutfitSnapshotRestore::ClearSnapshots()
	{
		std::lock_guard lock(g_mutex);
		g_actorSnapshots.clear();
	}

	void OutfitSnapshotRestore::EraseActor(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return;
		}

		std::lock_guard lock(g_mutex);
		g_actorSnapshots.erase(a_actorID);
	}

	void OutfitSnapshotRestore::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_closeListenerHandle != 0) {
			ContainerMenuUtil::RemoveOnContainerMenuCloseListener(g_closeListenerHandle);
			g_closeListenerHandle = 0;
		}

		{
			std::lock_guard lock(g_pendingRefreshMutex);
			g_pendingRefresh.clear();
		}

		ClearSnapshots();

		g_installed = false;
	}

	void OutfitSnapshotRestore::CaptureUserEquip(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		const std::optional<InstanceSignature>& a_sig)
	{
		if (!g_installed || !IsEnabled()) {
			return;
		}
		if (!IsEligibleActor(a_actor)) {
			return;
		}

		auto* armo = AsTrackedArmor(a_object);
		if (!armo) {
			return;
		}

		const auto objectID = a_object->GetFormID();
		if (objectID == 0) {
			return;
		}
		const auto mask = static_cast<std::uint32_t>(armo->GetSlotMask());
		if (mask == 0) {
			return;
		}

		Entry entry{};
		entry.baseObjectID = objectID;
		entry.signature = a_sig;

		const bool doDebug = spdlog::should_log(spdlog::level::debug);

		std::lock_guard lock(g_mutex);
		auto& state = g_actorSnapshots[a_actor->GetFormID()];

		// Engine auto-unequips overlapping armor; remove displaced items from all slots.
		std::unordered_set<RE::FormID> displaced;
		ForEachSlotBit(mask, [&](std::uint32_t bit) {
			auto it = state.armorBySlot.find(bit);
			if (it != state.armorBySlot.end() && it->second.baseObjectID != objectID) {
				displaced.insert(it->second.baseObjectID);
			}
		});

		ForEachSlotBit(mask, [&](std::uint32_t bit) {
			state.armorBySlot[bit] = entry;
		});

		if (doDebug) {
			logger::debug("SnapshotRestore: CaptureEquip armor {:08X} ({}) slotMask={:08X} displaced={} actor={:08X}",
				objectID, a_object->GetName(), mask,
				static_cast<std::uint32_t>(displaced.size()), a_actor->GetFormID());
		}

		// Purge every slot entry for each displaced item.
		for (const auto dispID : displaced) {
			auto* dispForm = RE::TESForm::LookupByID(dispID);
			auto* dispArmo = dispForm ? dispForm->As<RE::TESObjectARMO>() : nullptr;
			if (!dispArmo) {
				continue;
			}
			const auto dispMask = static_cast<std::uint32_t>(dispArmo->GetSlotMask());
			ForEachSlotBit(dispMask, [&](std::uint32_t bit) {
				auto it = state.armorBySlot.find(bit);
				if (it != state.armorBySlot.end() && it->second.baseObjectID == dispID) {
					state.armorBySlot.erase(it);
				}
			});
		}
	}

	void OutfitSnapshotRestore::CaptureUserEquip(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_xList,
		bool a_hasSelection)
	{
		std::optional<InstanceSignature> sig;
		if (a_xList) {
			sig = BuildInstanceSignature(*a_xList, a_object);
		} else if (a_hasSelection) {
			sig = InstanceSignature{};
		}
		CaptureUserEquip(a_actor, a_object, sig);
	}

	void OutfitSnapshotRestore::CaptureUserUnequip(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object)
	{
		if (!g_installed || !IsEnabled()) {
			return;
		}
		if (!IsEligibleActor(a_actor)) {
			return;
		}

		auto* armo = AsTrackedArmor(a_object);
		if (!armo) {
			return;
		}
		const auto mask = static_cast<std::uint32_t>(armo->GetSlotMask());
		if (mask == 0) {
			return;
		}

		std::lock_guard lock(g_mutex);
		auto it = g_actorSnapshots.find(a_actor->GetFormID());
		if (it == g_actorSnapshots.end()) {
			return;
		}
		auto& state = it->second;

		const bool doDebug = spdlog::should_log(spdlog::level::debug);

		ForEachSlotBit(mask, [&](std::uint32_t bit) {
			state.armorBySlot.erase(bit);
		});
		if (doDebug) {
			logger::debug("SnapshotRestore: CaptureUnequip armor {:08X} ({}) slotMask={:08X} actor={:08X}",
				a_object->GetFormID(), a_object->GetName(), mask, a_actor->GetFormID());
		}

		if (IsStateEmptyLocked(state)) {
			const auto actorID = a_actor->GetFormID();
			g_actorSnapshots.erase(it);
			if (doDebug) {
				logger::debug("SnapshotRestore: CaptureUnequip actor={:08X} snapshot empty, erased", actorID);
			}
		}
	}

	void OutfitSnapshotRestore::CaptureBaselineIfEmpty(RE::Actor* a_actor)
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

		// Skip body armor baseline for actors that cannot equip body armor.
		if (ActorScope::BodyEquipAllowed(a_actor)) {
			std::unordered_map<std::uint32_t, RE::FormID> wornBySlot;
			EnumerateCurrentWornArmor(a_actor, wornBySlot);
			for (const auto& [slot, baseID] : wornBySlot) {
				baseline.armorBySlot[slot] = Entry{ baseID, std::nullopt };
			}
		}

		if (IsStateEmptyLocked(baseline)) {
			return;
		}

		{
			std::lock_guard lock(g_mutex);
			// Double-check under lock before inserting the baseline.
			if (g_actorSnapshots.find(a_actor->GetFormID()) == g_actorSnapshots.end()) {
				g_actorSnapshots[a_actor->GetFormID()] = std::move(baseline);
			}
		}
	}

	void OutfitSnapshotRestore::OnUpdateNpcOutfitSuppressed(RE::Actor* a_actor)
	{
		if (!g_installed || !IsEnabled()) {
			return;
		}
		if (!IsEligibleActor(a_actor)) {
			return;
		}

		if (spdlog::should_log(spdlog::level::trace)) {
			logger::trace("SnapshotRestore: OnUpdateNpcOutfitSuppressed — actor {:08X}",
				a_actor->GetFormID());
		}

		// Do not restore while ContainerMenu is open; menu-time inventory mutation can corrupt
		// ContainerMenu state, especially with SkyrimSoulsRE keeping gameplay running.
		if (ContainerMenuUtil::IsContainerMenuOpen()) {
			{
				std::lock_guard lock(g_pendingRefreshMutex);
				if (!g_pendingRefresh.insert(a_actor->GetFormID()).second) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("SnapshotRestore: trade menu open — {:08X} already queued for close",
						a_actor->GetFormID());
				}
					return;
				}
			}
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("SnapshotRestore: trade menu open — queued {:08X} for restore on menu close",
					a_actor->GetFormID());
			}
			return;
		}

		if (!ShouldRestoreSnapshot(a_actor)) {
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("SnapshotRestore: ShouldRestoreSnapshot=false for {:08X} (combat={} containerOpen={})",
					a_actor->GetFormID(), a_actor->IsInCombat(), ContainerMenuUtil::IsContainerMenuOpen());
			}
			return;
		}

		// Preserve external armor changes that bypassed UpdateNPCOutfit.
		MaybeReseedArmorFromWorn(a_actor);

		ActorState snap{};
		{
			std::lock_guard lock(g_mutex);
			auto it = g_actorSnapshots.find(a_actor->GetFormID());
			if (it == g_actorSnapshots.end()) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("SnapshotRestore: no snapshot for {:08X}, skipping restore",
						a_actor->GetFormID());
				}
				return;
			}
			snap = it->second;
		}

		RestoreSnapshot(a_actor, snap);
	}

	void OutfitSnapshotRestore::OnOutfitFormChanged(RE::Actor* a_actor)
	{
		if (!g_installed || !IsEnabled()) {
			return;
		}
		if (!IsEligibleActor(a_actor)) {
			return;
		}

		const RE::FormID actorID = a_actor->GetFormID();

		// Rebuild snapshot from the newly applied outfit state.
		bool hadPrev = false;
		{
			std::lock_guard lock(g_mutex);
			auto it = g_actorSnapshots.find(actorID);
			if (it != g_actorSnapshots.end()) {
				hadPrev = true;
				g_actorSnapshots.erase(it);
			}
		}

		CaptureBaselineIfEmpty(a_actor);

		if (spdlog::should_log(spdlog::level::debug)) {
			std::size_t newSlots = 0;
			{
				std::lock_guard lock(g_mutex);
				auto it = g_actorSnapshots.find(actorID);
				if (it != g_actorSnapshots.end()) {
					newSlots = it->second.armorBySlot.size();
				}
			}
			logger::debug(
				"SnapshotRestore: outfit form changed for {:08X} — snapshot rebuilt from worn (had_prev={} new_armor_slots={})",
				actorID, hadPrev, newSlots);
		}
	}

	void OutfitSnapshotRestore::OnExternalArmorEquip(RE::Actor* a_actor)
	{
		if (!g_installed || !IsEnabled()) {
			return;
		}
		if (!IsEligibleActor(a_actor)) {
			return;
		}
		MaybeReseedArmorFromWorn(a_actor);
	}
}
