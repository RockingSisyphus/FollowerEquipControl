#include "OutfitItemSanitizer.h"

#include "ActorScope.h"
#include "AddObjectToContainerHook.h"
#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuTransferHook.h"
#include "ContainerMenuUtil.h"
#include "InstanceSignature.h"
#include "InventoryUtil.h"
#include "Logging.h"
#include "PluginSettings.h"

#include <RE/A/AIProcess.h>
#include <RE/E/ExtraContainerChanges.h>
#include <RE/E/ExtraDataTypes.h>
#include <RE/E/ExtraOutfitItem.h>
#include <RE/E/ExtraWorn.h>
#include <RE/E/ExtraWornLeft.h>
#include <RE/I/InventoryChanges.h>

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace FEC
{
	namespace
	{
		bool g_installed{ false };
		bool g_menuSinkRegistered{ false };
		ContainerMenuDisplayHook::ListenerHandle g_listenerHandle{ 0 };
		ContainerMenuTransferHook::ListenerHandle g_transferPostHandle{ 0 };
		AddObjectToContainerHook::ListenerHandle g_addObjectHandle{ 0 };

		// Deferred RunForActor tasks are coalesced per actor.
		std::unordered_set<RE::FormID> g_pendingOutfitAddActors;
		// Triggers received while running request one more pass instead of concurrent scans.
		std::unordered_set<RE::FormID> g_runningOutfitAddActors;
		std::unordered_set<RE::FormID> g_requeueOutfitAddActors;
		std::mutex g_pendingOutfitAddMutex;

		// Tracks active ContainerMenu sanitize passes and deferred RemoveItem tasks.
		std::unordered_set<RE::FormID> g_pendingRemovalActors;
		// ContainerMenu::Display fires every frame; reset this cache when the menu closes.
		std::unordered_set<RE::FormID> g_sanitizedActors;
		// Changes during an active pass force another pass on the next display frame.
		std::unordered_set<RE::FormID> g_rescanRequestedActors;
		std::mutex g_pendingMutex;

		struct Removal
		{
			RE::TESBoundObject* obj{ nullptr };
			RE::ExtraDataList* xList{ nullptr };  // nullptr = remove one from plain stack
			std::int32_t count{ 0 };
		};

		// Collected in the strip pass and consumed by the deferred RemoveItem task.
		std::unordered_map<RE::FormID, std::vector<Removal>> g_pendingRemovals;

		void QueueDeferredRemovalTask(RE::FormID a_actorID, std::vector<Removal> a_removals);

		void QueueRunForActorTask(RE::FormID a_actorID, const char* a_reason)
		{
			{
				std::scoped_lock lock(g_pendingOutfitAddMutex);
				if (!g_pendingOutfitAddActors.insert(a_actorID).second) {
					if (g_runningOutfitAddActors.contains(a_actorID)) {
						g_requeueOutfitAddActors.insert(a_actorID);
					}
					return;
				}
			}

			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				std::scoped_lock lock(g_pendingOutfitAddMutex);
				g_pendingOutfitAddActors.erase(a_actorID);
				return;
			}

			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug(
					"OutfitItemSanitizer: {} — scheduling sanitize task for {:08X}",
					a_reason ? a_reason : "trigger", a_actorID);
			}

			taskInterface->AddTask([a_actorID]() {
				{
					std::scoped_lock lock(g_pendingOutfitAddMutex);
					g_runningOutfitAddActors.insert(a_actorID);
				}

				auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
				if (actor && !actor->IsDead()) {
					OutfitItemSanitizer::RunForActor(actor);
				}

				bool requeue = false;
				{
					std::scoped_lock lock(g_pendingOutfitAddMutex);
					g_runningOutfitAddActors.erase(a_actorID);
					g_pendingOutfitAddActors.erase(a_actorID);
					requeue = g_requeueOutfitAddActors.erase(a_actorID) > 0;
				}
				if (requeue) {
					QueueRunForActorTask(a_actorID, "coalesced trigger");
				}
			});
		}

		[[nodiscard]] bool HandoffToOpenMenuSanitizer(RE::FormID a_actorID)
		{
			auto menu = ContainerMenuUtil::GetOpenContainerMenu();
			if (!menu) {
				return false;
			}

			// Handoff uses raw target identity; never mutate the open-menu actor synchronously.
			auto target = ContainerMenuUtil::ResolveActorHandle(menu->GetTargetRefHandle());
			if (!target || target->GetFormID() != a_actorID) {
				return false;
			}

			bool newlyRequested = false;
			{
				std::scoped_lock lock(g_pendingMutex);
				g_sanitizedActors.erase(a_actorID);
				newlyRequested = g_rescanRequestedActors.insert(a_actorID).second;
			}
			if (newlyRequested) {
				ContainerMenuUtil::QueueRefreshForActor(a_actorID);
			}
			return true;
		}

		// A rescan request made during the pass keeps the actor unsanitized for the next frame.
		[[nodiscard]] bool CompleteMenuSanitizePass(RE::FormID a_actorID)
		{
			std::scoped_lock lock(g_pendingMutex);
			g_pendingRemovals.erase(a_actorID);
			if (g_pendingRemovalActors.erase(a_actorID) == 0) {
				return false;
			}
			if (g_rescanRequestedActors.contains(a_actorID)) {
				g_sanitizedActors.erase(a_actorID);
				return true;
			}
			g_sanitizedActors.insert(a_actorID);
			return false;
		}

		void AbandonMenuSanitizePass(RE::FormID a_actorID)
		{
			std::scoped_lock lock(g_pendingMutex);
			g_pendingRemovalActors.erase(a_actorID);
			g_sanitizedActors.erase(a_actorID);
			g_pendingRemovals.erase(a_actorID);
		}

		class MenuCloseSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static MenuCloseSink* GetSingleton()
			{
				static MenuCloseSink s;
				return std::addressof(s);
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent* a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event || a_event->opening ||
					a_event->menuName != RE::ContainerMenu::MENU_NAME) {
					return RE::BSEventNotifyControl::kContinue;
				}

				// Requeue outstanding handoffs after menu teardown instead of dropping them.
				std::unordered_set<RE::FormID> deferredRescans;
				std::unordered_map<RE::FormID, std::vector<Removal>> deferredRemovals;
				{
					std::scoped_lock lock(g_pendingMutex);
					deferredRescans = g_rescanRequestedActors;
					deferredRemovals = std::move(g_pendingRemovals);
					g_pendingRemovalActors.clear();
					g_sanitizedActors.clear();
					g_rescanRequestedActors.clear();
				}
				for (auto& [actorID, removals] : deferredRemovals) {
					QueueDeferredRemovalTask(actorID, std::move(removals));
				}
				for (const auto actorID : deferredRescans) {
					QueueRunForActorTask(actorID, "menu-close rescan");
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		void CollectDefaultOutfitArmorFormIDs(RE::Actor* a_actor, std::unordered_set<RE::FormID>& a_out)
		{
			a_out.clear();
			if (!a_actor) {
				return;
			}

			a_out.reserve(32);
			auto* base = a_actor->GetActorBase();
			if (!base || !base->defaultOutfit) {
				return;
			}

			base->defaultOutfit->ForEachItem([&](RE::TESForm* item) {
				if (!item) {
					return RE::BSContainer::ForEachResult::kContinue;
				}

				if (auto* armor = item->As<RE::TESObjectARMO>()) {
					a_out.insert(armor->GetFormID());
					return RE::BSContainer::ForEachResult::kContinue;
				}

				if (auto* levItem = item->As<RE::TESLevItem>()) {
					RE::BSScrapArray<RE::CALCED_OBJECT> calced;
					levItem->CalculateCurrentFormList(a_actor->GetLevel(), 1, calced, 0, false);
					for (auto& entry : calced) {
						if (!entry.form) {
							continue;
						}
						if (auto* levArmor = entry.form->As<RE::TESObjectARMO>()) {
							a_out.insert(levArmor->GetFormID());
						}
					}
				}

				return RE::BSContainer::ForEachResult::kContinue;
			});
		}

		// a_xList must be non-null and still present in a_invChanges for a_obj.
		[[nodiscard]] bool IsXListLive(
			RE::InventoryChanges* a_invChanges,
			RE::TESBoundObject* a_obj,
			RE::ExtraDataList* a_xList)
		{
			for (auto* entry : *a_invChanges->entryList) {
				if (!entry || entry->object != a_obj || !entry->extraLists) {
					continue;
				}
				for (auto* xList : *entry->extraLists) {
					if (xList == a_xList) {
						return true;
					}
				}
			}
			return false;
		}

		void QueueDeferredRemovalTask(RE::FormID a_actorID, std::vector<Removal> a_removals)
		{
			if (a_removals.empty()) {
				return;
			}

			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}

			taskInterface->AddTask([a_actorID, removals = std::move(a_removals)]() {
				if (!g_installed) {
					return;
				}

				auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
				if (!actor || actor->IsDead()) {
					return;
				}
				if (!actor->GetActorRuntimeData().currentProcess) {
					logger::warn(
						"OutfitItemSanitizer: detached task {:08X} — currentProcess null, skipping RemoveItem",
						a_actorID);
					return;
				}

				auto* xcc = actor->extraList.GetByType<RE::ExtraContainerChanges>();
				auto* invChanges = (xcc && xcc->changes && xcc->changes->entryList)
					? xcc->changes
					: nullptr;

				bool anyRemoval = false;
				for (const auto& rem : removals) {
					if (!rem.obj) {
						continue;
					}
					if (rem.xList) {
						if (!invChanges || !IsXListLive(invChanges, rem.obj, rem.xList)) {
							if (spdlog::should_log(spdlog::level::debug)) {
								logger::debug(
									"OutfitItemSanitizer: detached task {:08X} — xList stale for {:08X} ({}), skipping",
									a_actorID, rem.obj->GetFormID(), rem.obj->GetName());
							}
							continue;
						}
					}

					actor->RemoveItem(rem.obj, rem.count, RE::ITEM_REMOVE_REASON::kRemove, rem.xList, nullptr);
					anyRemoval = true;
					logger::info(
						"OutfitItemSanitizer: detached task removed untagged duplicate {:08X} ({}) from {:08X} ({})",
						rem.obj->GetFormID(), rem.obj->GetName(),
						actor->GetFormID(), actor->GetName());
				}

				if (anyRemoval) {
					ContainerMenuUtil::QueueRefreshForActor(a_actorID);
				}
			});
		}

		// Strips ExtraOutfitItem tags and queues surplus untagged duplicates for removal.
		// a_defaultOutfitOnly selects default-outfit items; false selects externally distributed items.
		// Duplicate detection uses stable instance signatures or plain-stack count.
		// Use GetByType+Remove; RemoveByType can null-deref when all nodes are kOutfitItem.
		[[nodiscard]] bool SanitizeOutfitItems(
			RE::Actor* a_actor,
			RE::InventoryChanges* a_invChanges,
			const std::unordered_set<RE::FormID>& a_outfitArmorIDs,
			std::vector<Removal>& a_removalsOut,
			bool a_defaultOutfitOnly)
		{
			if (!a_actor || a_actor->IsPlayerRef() || a_actor->IsDead()) {
				return false;
			}
			if (!ActorScope::IsAffectedFollower(a_actor)) {
				return false;
			}
			if (a_defaultOutfitOnly && a_outfitArmorIDs.empty()) {
				return false;
			}

			struct TaggedEntry {
				RE::TESBoundObject* obj{ nullptr };
				RE::ExtraDataList* xList{ nullptr };
				bool isWorn{ false };
				bool hasDuplicate{ false };
				// duplicateXList == nullptr means remove one from the plain stack.
				RE::ExtraDataList* duplicateXList{ nullptr };
			};

			std::vector<TaggedEntry> tagged;
			tagged.reserve(8);

			for (auto* entry : *a_invChanges->entryList) {
				if (!entry || !entry->object) {
					continue;
				}
				auto* obj = entry->object;
				if (!obj->IsArmor()) {
					continue;
				}
				const bool inOutfit = a_outfitArmorIDs.contains(obj->GetFormID());
				if (inOutfit != a_defaultOutfitOnly) {
					continue;
				}
				if (!entry->extraLists || entry->extraLists->empty()) {
					continue;
				}

				std::vector<RE::ExtraDataList*> taggedLists;
				std::vector<RE::ExtraDataList*> untaggedLists;

				// Use GetTotalCount, not countDelta. Template-container copies can be absent
				// from InventoryChanges, and countDelta can make plainStackCount falsely positive.
				std::int32_t plainStackCount = InventoryUtil::GetTotalCount(a_actor, obj);

				for (auto* xList : *entry->extraLists) {
					if (!xList) {
						continue;
					}
					// Each xList represents one unit; the remainder is the plain stack.
					if (xList->HasType<RE::ExtraOutfitItem>()) {
						taggedLists.push_back(xList);
					} else {
						untaggedLists.push_back(xList);
					}
					--plainStackCount;
				}
				// Can be negative while GetTotalCount and extraLists are transiently out of sync.
				if (plainStackCount < 0) {
					plainStackCount = 0;
				}

				for (auto* xTagged : taggedLists) {
					TaggedEntry e{};
					e.obj = obj;
					e.xList = xTagged;
					e.isWorn = xTagged->HasType<RE::ExtraWorn>() || xTagged->HasType<RE::ExtraWornLeft>();

					const auto taggedSig = NormalizeStableIdentity(BuildInstanceSignature(*xTagged, obj));

					if (taggedSig.HasStableIdentity()) {
						// Identity-bearing items match duplicates by stable signature.
						for (auto* xUntagged : untaggedLists) {
							if (!xUntagged) {
								continue;
							}
							const auto untaggedSig = NormalizeStableIdentity(BuildInstanceSignature(*xUntagged, obj));
							if (StableIdentityEquals(taggedSig, untaggedSig)) {
								e.hasDuplicate = true;
								e.duplicateXList = xUntagged;
								break;
							}
						}
					} else {
						// Bare copies duplicate only untagged bare copies or plain-stack units.
						for (auto* xU : untaggedLists) {
							if (!xU) continue;
							const auto uSig = NormalizeStableIdentity(BuildInstanceSignature(*xU, obj));
							if (!uSig.HasStableIdentity()) {
								e.hasDuplicate = true;
								e.duplicateXList = xU;
								break;
							}
						}
						if (!e.hasDuplicate && plainStackCount > 0) {
							// Duplicate comes from the plain stack.
							e.hasDuplicate = true;
							e.duplicateXList = nullptr;
							--plainStackCount;  // avoid double-removing the same plain-stack unit
						}
					}

					tagged.push_back(std::move(e));
				}
			}

			if (tagged.empty()) {
				return false;
			}

			// Use GetByType+Remove to avoid RemoveByType null-deref when all nodes are kOutfitItem.
			const char* const category = a_defaultOutfitOnly ? "default-outfit" : "externally-distributed";
			for (auto& e : tagged) {
				if (auto* node = e.xList->GetByType<RE::ExtraOutfitItem>()) {
					e.xList->Remove(RE::ExtraDataType::kOutfitItem, node);
				}
				logger::info(
					"OutfitItemSanitizer: stripped ExtraOutfitItem from {} {} item {:08X} ({}) on {:08X} ({})",
					e.isWorn ? "worn" : "unworn",
					category,
					e.obj->GetFormID(), e.obj->GetName(),
					a_actor->GetFormID(), a_actor->GetName());
			}

			// Do not call RemoveItem here; caller chooses the safe execution context.
			a_removalsOut.reserve(a_removalsOut.size() + tagged.size());
			for (auto& e : tagged) {
				if (e.hasDuplicate) {
					a_removalsOut.push_back(Removal{ e.obj, e.duplicateXList, 1 });
				}
			}

			return true;
		}
	}

	// Deferred AddObjectToContainer tasks run after NFF outfit-switch sequences complete.
	// If the actor owns the open ContainerMenu, hand off to the menu sanitizer.
	// Re-entrancy guard prevents RestoreSnapshot equip hooks from nesting RunForActor.
	void OutfitItemSanitizer::RunForActor(RE::Actor* a_actor)
	{
		static thread_local bool s_inside = false;
		if (s_inside) {
			return;
		}

		if (!g_installed) {
			return;
		}
		if (!a_actor || a_actor->IsPlayerRef() || a_actor->IsDead()) {
			return;
		}
		if (!ActorScope::IsAffectedFollower(a_actor)) {
			return;
		}
		if (!PluginSettings::Get().hiddenItems.enableOutfitItems) {
			return;
		}

		const auto actorID = a_actor->GetFormID();
		if (HandoffToOpenMenuSanitizer(actorID)) {
			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug(
					"OutfitItemSanitizer: RunForActor {:08X} ({}) — handed off to open ContainerMenu",
					actorID, a_actor->GetName());
			}
			return;
		}

		auto* xcc = a_actor->extraList.GetByType<RE::ExtraContainerChanges>();
		if (!xcc || !xcc->changes || !xcc->changes->entryList) {
			return;
		}
		auto* invChanges = xcc->changes;

		std::unordered_set<RE::FormID> outfitArmorIDs;
		CollectDefaultOutfitArmorFormIDs(a_actor, outfitArmorIDs);

		std::vector<Removal> removals;

		const bool isSummon = ActorScope::IsPlayerCommandedActor(a_actor);
		if (!isSummon && PluginSettings::Get().hiddenItems.enableRevealDefaultOutfitItems) {
			(void)SanitizeOutfitItems(a_actor, invChanges, outfitArmorIDs, removals, true);
		}

		if (PluginSettings::Get().hiddenItems.enableRevealExternalOutfitItems) {
			(void)SanitizeOutfitItems(a_actor, invChanges, outfitArmorIDs, removals, false);
		}

		if (removals.empty()) {
			(void)HandoffToOpenMenuSanitizer(actorID);
			return;
		}

		s_inside = true;

		if (!a_actor->GetActorRuntimeData().currentProcess) {
			logger::warn(
				"OutfitItemSanitizer: RunForActor {:08X} ({}) — currentProcess null, skipping RemoveItem",
				a_actor->GetFormID(), a_actor->GetName());
			s_inside = false;
			(void)HandoffToOpenMenuSanitizer(actorID);
			return;
		}

		for (const auto& rem : removals) {
			if (!rem.obj) {
				continue;
			}
			// Validate xList liveness before RemoveItem.
			if (rem.xList && !IsXListLive(invChanges, rem.obj, rem.xList)) {
				if (spdlog::should_log(spdlog::level::debug)) {
					logger::debug(
						"OutfitItemSanitizer: RunForActor {:08X} — xList stale for {:08X} ({}), skipping",
						a_actor->GetFormID(), rem.obj->GetFormID(), rem.obj->GetName());
				}
				continue;
			}

			a_actor->RemoveItem(rem.obj, rem.count, RE::ITEM_REMOVE_REASON::kRemove, rem.xList, nullptr);
			logger::info(
				"OutfitItemSanitizer: RunForActor: removed duplicate {:08X} ({}) from {:08X} ({})",
				rem.obj->GetFormID(), rem.obj->GetName(),
				a_actor->GetFormID(), a_actor->GetName());
		}

		s_inside = false;

		// The menu may have opened while this pass was running.
		(void)HandoffToOpenMenuSanitizer(actorID);
	}

	void OutfitItemSanitizer::Install()
	{
		const auto& hidden = PluginSettings::Get().hiddenItems;
		if (!hidden.enableOutfitItems) {
			Uninstall();
			return;
		}

		if (g_installed) {
			// UI may not have been available during the first install attempt.
			if (!g_menuSinkRegistered) {
				if (auto* ui = RE::UI::GetSingleton()) {
					ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuCloseSink::GetSingleton());
					g_menuSinkRegistered = true;
				}
			}
			return;
		}

		ContainerMenuDisplayHook::Install();
		g_listenerHandle = ContainerMenuDisplayHook::AddPostDisplayListener([](RE::ContainerMenu* a_menu) {
			auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
			if (!target) {
				return;
			}
			if (!ActorScope::IsAffectedFollower(target.get())) {
				if (spdlog::should_log(spdlog::level::debug)) {
					logger::debug("OutfitItemSanitizer: skip {:08X} ({}) — not an affected follower",
						target->GetFormID(), target->GetName());
				}
				return;
			}
			const auto actorID = target->GetFormID();

			// Display fires every frame; skip completed sessions and in-flight tasks.
			{
				std::scoped_lock lock(g_pendingMutex);
				if (g_sanitizedActors.contains(actorID)) {
					return;
				}
				if (!g_pendingRemovalActors.insert(actorID).second) {
					return;
				}
				// Requests made while this pass runs remain set and force another pass.
				g_rescanRequestedActors.erase(actorID);
			}

			// Strip tags in ContainerMenu post-display, after current-frame inventory updates.
			// BSExtraData lists are stable here, avoiding BSJobs races on freed nodes.
			// Defer RemoveItem so the ContainerMenu vfunc does not leave StandardItemData
			// holding freed ExtraDataList pointers until the next rebuild.
			auto* actor = target.get();

			// ExtraContainerChanges is independent of currentProcess.
			auto* xContainerChanges = actor->extraList.GetByType<RE::ExtraContainerChanges>();
			if (!xContainerChanges || !xContainerChanges->changes || !xContainerChanges->changes->entryList) {
				if (CompleteMenuSanitizePass(actorID)) {
					ContainerMenuUtil::QueueRefreshForActor(actorID);
				}
				return;
			}
			auto* invChanges = xContainerChanges->changes;

			std::unordered_set<RE::FormID> outfitArmorIDs;
			CollectDefaultOutfitArmorFormIDs(actor, outfitArmorIDs);

			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug("OutfitItemSanitizer: hook {:08X} ({}) — defaultOutfit IDs={}",
					actorID, actor->GetName(),
					static_cast<std::uint32_t>(outfitArmorIDs.size()));
			}

			const bool isSummon = ActorScope::IsPlayerCommandedActor(actor);
			const bool runPassA = !isSummon &&
				PluginSettings::Get().hiddenItems.enableOutfitItems &&
				PluginSettings::Get().hiddenItems.enableRevealDefaultOutfitItems;

			std::vector<Removal> removals;
			const bool anyStripped = runPassA
				? SanitizeOutfitItems(actor, invChanges, outfitArmorIDs, removals, true)
				: false;

			const bool stripEnabled = PluginSettings::Get().hiddenItems.enableOutfitItems &&
				PluginSettings::Get().hiddenItems.enableRevealExternalOutfitItems;

			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug("OutfitItemSanitizer: hook {:08X} — anyStripped={} stripEnabled={}",
					actorID, anyStripped, stripEnabled);
			}

			const bool externalStripped = stripEnabled
				? SanitizeOutfitItems(actor, invChanges, outfitArmorIDs, removals, false)
				: false;

			const bool anyWork = anyStripped || externalStripped;
			const bool hasRemovals = !removals.empty();

			if (hasRemovals) {
				// Deferred task only revalidates stored xLists and removes duplicates.
				std::scoped_lock lock(g_pendingMutex);
				g_pendingRemovals[actorID] = std::move(removals);
			} else {
				// Complete immediately unless an in-flight inventory change requested a rescan.
				if (CompleteMenuSanitizePass(actorID)) {
					ContainerMenuUtil::QueueRefreshForActor(actorID);
				}
			}

			if (anyWork) {
				ContainerMenuUtil::QueueRefreshForActor(actorID);
			}

			if (!hasRemovals) {
				return;
			}

			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				if (CompleteMenuSanitizePass(actorID)) {
					ContainerMenuUtil::QueueRefreshForActor(actorID);
				}
				return;
			}

			// Remove duplicates outside the ContainerMenu vfunc after xList revalidation.
			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug("OutfitItemSanitizer: scheduling dedup task for {:08X} ({})",
					target->GetFormID(), target->GetName());
			}
			taskInterface->AddTask([actorID]() {
				// Keep the actor pending until removals finish so concurrent changes request a rescan.
				std::vector<Removal> pendingRemovals;
				bool passActive = false;
				{
					std::scoped_lock lock(g_pendingMutex);
					passActive = g_pendingRemovalActors.contains(actorID);
					auto it = g_pendingRemovals.find(actorID);
					if (it != g_pendingRemovals.end()) {
						pendingRemovals = std::move(it->second);
						g_pendingRemovals.erase(it);
					}
				}

				if (!passActive) {
					return;
				}

				if (pendingRemovals.empty()) {
					if (CompleteMenuSanitizePass(actorID)) {
						ContainerMenuUtil::QueueRefreshForActor(actorID);
					}
					return;
				}

				auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
				if (!actor || actor->IsDead()) {
					AbandonMenuSanitizePass(actorID);
					return;
				}

				// Validate that the menu is still open for this actor.
				auto menu = ContainerMenuUtil::GetOpenContainerMenu();
				if (!menu) {
					if (spdlog::should_log(spdlog::level::debug)) {
						logger::debug("OutfitItemSanitizer: task {:08X} — menu closed before execution", actorID);
					}
					AbandonMenuSanitizePass(actorID);
					QueueDeferredRemovalTask(actorID, std::move(pendingRemovals));
					return;
				}
				auto menuTarget = ContainerMenuUtil::ResolveActorHandle(menu->GetTargetRefHandle());
				if (!menuTarget || menuTarget->GetFormID() != actorID) {
					if (spdlog::should_log(spdlog::level::debug)) {
						logger::debug("OutfitItemSanitizer: task {:08X} — menu target changed before execution", actorID);
					}
					AbandonMenuSanitizePass(actorID);
					return;
				}

				// currentProcess is required for RemoveItem only.
				if (!actor->GetActorRuntimeData().currentProcess) {
					logger::warn(
						"OutfitItemSanitizer: task {:08X} — currentProcess null, skipping RemoveItem",
						actorID);
					if (CompleteMenuSanitizePass(actorID)) {
						ContainerMenuUtil::QueueRefreshForActor(actorID);
					}
					return;
				}

				// Re-acquire ExtraContainerChanges before using stored xLists.
				// Another mod may have removed the item and freed the xList between hook and task.
				auto* xccTask = actor->extraList.GetByType<RE::ExtraContainerChanges>();
				auto* invChangesTask = (xccTask && xccTask->changes && xccTask->changes->entryList)
					? xccTask->changes
					: nullptr;

				bool anyRemoval = false;
				for (const auto& rem : pendingRemovals) {
					if (!rem.obj) {
						continue;
					}

					// rem.xList == nullptr means remove from plain stack; otherwise validate liveness.
					if (rem.xList) {
						if (!invChangesTask) {
							// Without ExtraContainerChanges, liveness cannot be validated safely.
							if (spdlog::should_log(spdlog::level::debug)) {
								logger::debug(
									"OutfitItemSanitizer: task {:08X} — no ExtraContainerChanges for {:08X} ({}), skipping",
									actorID, rem.obj->GetFormID(), rem.obj->GetName());
							}
							continue;
						}
						if (!IsXListLive(invChangesTask, rem.obj, rem.xList)) {
							if (spdlog::should_log(spdlog::level::debug)) {
								logger::debug(
									"OutfitItemSanitizer: task {:08X} — xList stale for {:08X} ({}), skipping",
									actorID, rem.obj->GetFormID(), rem.obj->GetName());
							}
							continue;
						}
					}
					actor->RemoveItem(rem.obj, rem.count, RE::ITEM_REMOVE_REASON::kRemove, rem.xList, nullptr);
					anyRemoval = true;
					logger::info(
						"OutfitItemSanitizer: removed untagged duplicate {:08X} ({}) from {:08X} ({})",
						rem.obj->GetFormID(), rem.obj->GetName(),
						actor->GetFormID(), actor->GetName());
				}

				const bool rescanRequested = CompleteMenuSanitizePass(actorID);

				if (anyRemoval || rescanRequested) {
					ContainerMenuUtil::QueueRefreshForActor(actorID);
				}
			});
		});

		// Engine may tag player-transferred default-outfit items with ExtraOutfitItem.
		// Strip that tag so player gifts are not removed as outfit copies.
		g_transferPostHandle = ContainerMenuTransferHook::AddPostListener([](const ContainerMenuTransferHook::Context& ctx) {
			constexpr std::uint8_t kPlayerToNpc = 0x00;
			if (ctx.mode != kPlayerToNpc) {
				return;
			}
			if (ctx.transferredXListNonUnique || !ctx.transferredXList) {
				return;
			}
			auto* outfitExtra = ctx.transferredXList->GetByType<RE::ExtraOutfitItem>();
			if (!outfitExtra) {
				return;
			}
			ctx.transferredXList->Remove(outfitExtra);
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace(
					"OutfitItemSanitizer: stripped ExtraOutfitItem from player-transferred xList {:p} obj={:08X} target={:08X} ({})",
					static_cast<void*>(ctx.transferredXList),
					ctx.object ? ctx.object->GetFormID() : 0u,
					ctx.transferredToActor ? ctx.transferredToActor->GetFormID() : 0u,
					ctx.transferredToActor ? ctx.transferredToActor->GetName() : "?");
			}
		});

		if (!g_menuSinkRegistered) {
			if (auto* ui = RE::UI::GetSingleton()) {
				ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuCloseSink::GetSingleton());
				g_menuSinkRegistered = true;
			}
		}

		// AddObjectToContainer fires after the item and ExtraOutfitItem tag are live.
		// Defer the pass so NFF outfit-switch sequences finish before inventory is inspected.
		g_addObjectHandle = AddObjectToContainerHook::AddPostListener([](const AddObjectToContainerHook::Context& ctx) {
			if (!ctx.toActor || !ctx.object || !ctx.object->IsArmor()) {
				return;
			}
			if (!ActorScope::IsAffectedFollower(ctx.toActor)) {
				return;
			}
			if (!PluginSettings::Get().hiddenItems.enableOutfitItems) {
				return;
			}

			QueueRunForActorTask(ctx.toActor->GetFormID(), "outfit-add trigger");
		});

		g_installed = true;
		logger::info("OutfitItemSanitizer: installed (ContainerMenu post-display + AddObjectToContainer trigger)");
	}

	void OutfitItemSanitizer::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_listenerHandle != 0) {
			ContainerMenuDisplayHook::UnregisterListener(g_listenerHandle);
			g_listenerHandle = 0;
		}

		if (g_transferPostHandle != 0) {
			ContainerMenuTransferHook::RemoveListener(g_transferPostHandle);
			g_transferPostHandle = 0;
		}

		if (g_addObjectHandle != 0) {
			AddObjectToContainerHook::RemoveListener(g_addObjectHandle);
			g_addObjectHandle = 0;
		}

		{
			std::scoped_lock lock(g_pendingOutfitAddMutex);
			g_pendingOutfitAddActors.clear();
			g_runningOutfitAddActors.clear();
			g_requeueOutfitAddActors.clear();
		}

		{
			std::scoped_lock lock(g_pendingMutex);
			g_pendingRemovalActors.clear();
			g_sanitizedActors.clear();
			g_rescanRequestedActors.clear();
			g_pendingRemovals.clear();
		}

		if (g_menuSinkRegistered) {
			if (auto* ui = RE::UI::GetSingleton()) {
				ui->RemoveEventSink<RE::MenuOpenCloseEvent>(MenuCloseSink::GetSingleton());
			}
			g_menuSinkRegistered = false;
		}

		g_installed = false;
		logger::info("OutfitItemSanitizer: uninstalled");
	}

}
