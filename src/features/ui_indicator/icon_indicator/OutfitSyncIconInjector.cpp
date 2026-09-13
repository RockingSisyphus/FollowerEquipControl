// Hooks SkyUI InventoryListEntry.formatName to add saved outfit item icons.
// Uses resolved ExtraDataList matches when possible, with base FormID fallback for
// entries that cannot be tied to a specific instance. Separate sentinel and stash
// members let this hook coexist with the other icon injectors through chaining.

#include "OutfitSyncIconInjector.h"

#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "IconPositioning.h"
#include "OutfitSnapshotRestore.h"
#include "PluginSettings.h"
#include "SignatureResolve.h"

#include "PCH.h"

#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace FEC
{
	namespace
	{
		// Wrapper visibility is controlled here, not on the loadMovie target.
		constexpr const char* kWrapperName = "fecOutfitSyncIconWrap";

		constexpr const char* kInnerIconName = "icon";

		constexpr std::int32_t kWrapperDepth = 9700;

		constexpr const char* kIconSwfPath = "FollowerEquipControl/marked.swf";

		// Sentinel on InventoryListEntry.prototype to prevent double-hooking.
		constexpr const char* kHookedFlagMember = "__fecOutfitSyncIconHooked";

		// Previous formatName function stashed on the prototype.
		constexpr const char* kOriginalFormatNameMember = "__fecOutfitSyncOriginalFormatName";

		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_postDisplayHandle{ 0 };
		ContainerMenuUtil::ListenerHandle g_menuCloseHandle{ 0 };

		// Resolved ExtraDataList* set for the viewed follower snapshot, read via row stamps.
		std::unordered_set<RE::ExtraDataList*> g_snapshotXLists;

		// baseFormId fallback set for entries without a specific ExtraDataList.
		std::unordered_set<RE::FormID> g_snapshotBaseOnly;

		// Item-row stamps become stale when this generation changes.
		std::uint64_t g_stampGeneration{ 0 };

		std::mutex g_snapshotMutex;

		// Snapshot hash used to detect changes and trigger list invalidation.
		std::size_t g_previousHash{ 0 };

		// Cheap pre-hash used to skip CollectSnapshotMatches when the snapshot is unchanged.
		std::size_t g_previousRawHash{ 0 };

		// Defer InvalidateData to the next PostDisplay to avoid onItemHighlightChange during AS2 execution.
		bool g_pendingInvalidate{ false };

		// Inner icon clip awaiting async loadMovie completion.
		RE::GFxValue g_pendingIcon;
		bool g_pendingSwfLoad{ false };

		// True when the Take tab (NPC->Player) is active.
		bool g_isNpcPanelActive{ false };

		bool g_loggedFirstHit{ false };

		[[nodiscard]] bool IsOutfitIconEnabled()
		{
			const auto& cfg = PluginSettings::Get().outfitSync;
			const auto& ia = PluginSettings::Get().iconAppearance;
			return cfg.enableUpdateNpcOutfitSuppression && cfg.enableOutfitSnapshotRestore && ia.enableOutfitSyncIcon;
		}

		[[nodiscard]] bool IsFeatureEnabled()
		{
			const auto& ia = PluginSettings::Get().iconAppearance;
			return ia.enableIconIndicator && IsOutfitIconEnabled();
		}

		[[nodiscard]] bool IsSkyUiPresent(const RE::GFxValue& a_root)
		{
			RE::GFxValue v;
			return a_root.GetMember("_platform", std::addressof(v)) && v.IsNumber();
		}

		[[nodiscard]] bool ShouldAffectMenu(RE::ContainerMenu* a_menu)
		{
			if (!a_menu || !IsFeatureEnabled()) {
				return false;
			}
			auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
			if (!target) {
				return false;
			}
			auto& root = a_menu->GetRuntimeData().root;
			return root.IsObject() && IsSkyUiPresent(root);
		}

		// Cheap per-frame hash; avoids SignatureResolve and inventory walks when base IDs are unchanged.
		[[nodiscard]] std::size_t ComputeRawSnapshotHash(RE::FormID a_actorId)
		{
			std::size_t h = std::hash<RE::FormID>{}(a_actorId);
			auto addEntry = [&](RE::FormID a_baseID) {
				h ^= std::hash<RE::FormID>{}(a_baseID) + 0x9e3779b9u + (h << 6) + (h >> 2);
			};
			h ^= 0x4F535253u + (h << 6) + (h >> 2);
			for (const auto& entry : OutfitSnapshotRestore::GetSnapshotEntries(a_actorId)) {
				addEntry(entry.baseObjectID);
			}
			return h;
		}

		// Collects snapshot instance matches for the follower being traded with.
		// Instance signatures are used to resolve exact ExtraDataList matches when possible.
		struct SnapshotSets
		{
			std::unordered_set<RE::ExtraDataList*> xLists;
			std::unordered_set<RE::FormID>         baseOnly;
		};

		[[nodiscard]] SnapshotSets CollectSnapshotMatches(RE::ContainerMenu* a_menu)
		{
			SnapshotSets result;
			auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
			if (!target) {
				return result;
			}
			// Prevent same-form snapshot entries from resolving to the same ExtraDataList.
			std::unordered_map<RE::FormID, std::unordered_set<RE::ExtraDataList*>> claimedByForm;
			auto addEntry = [&](RE::FormID a_baseObjectID, const std::optional<InstanceSignature>& a_signature) {
				if (a_baseObjectID == 0) {
					return;
				}
				auto* baseObj = RE::TESForm::LookupByID<RE::TESBoundObject>(a_baseObjectID);
				if (!baseObj) {
					return;
				}

				// Resolve even plain-item signatures; empty StableIdentity still allows worn-state tiebreaks.
				if (a_signature.has_value()) {
					auto& claimed = claimedByForm[a_baseObjectID];
					const auto resolved = claimed.empty()
						? SignatureResolve::Resolve(
							target.get(), baseObj, *a_signature,
							std::nullopt, SignatureResolve::Policy::kIdentityOnly)
						: SignatureResolve::Resolve(
							target.get(), baseObj, *a_signature,
							std::nullopt, SignatureResolve::Policy::kIdentityOnly,
							claimed);
					if (resolved.HasXList()) {
						claimed.insert(resolved.xList);
						result.xLists.insert(resolved.xList);
						return;
					}
				}

				// Base FormID fallback when no exact instance can be resolved.
				result.baseOnly.insert(a_baseObjectID);
			};

			for (const auto& entry : OutfitSnapshotRestore::GetSnapshotEntries(target->GetFormID())) {
				addEntry(entry.baseObjectID, entry.signature);
			}
			return result;
		}

		[[nodiscard]] std::size_t HashSnapshotSets(const SnapshotSets& a_sets)
		{
			std::size_t h = a_sets.xLists.size() + a_sets.baseOnly.size();
			for (auto* xList : a_sets.xLists) {
				h ^= std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(xList)) + 0x9e3779b9 + (h << 6) + (h >> 2);
			}
			for (auto formId : a_sets.baseOnly) {
				h ^= std::hash<RE::FormID>{}(formId) + 0x9e3779b9 + (h << 6) + (h >> 2);
			}
			return h;
		}

		// Stamps item rows lazily so formatName can read snapshot matches without resolving inventory.
		void StampAllSnapshotMatches(RE::ItemList* a_list, std::uint64_t a_gen)
		{
			if (!a_list) {
				return;
			}

			// Snapshot sets under lock, then iterate without holding it.
			std::unordered_set<RE::ExtraDataList*> snapXLists;
			std::unordered_set<RE::FormID> snapBaseOnly;
			{
				std::lock_guard lk(g_snapshotMutex);
				snapXLists  = g_snapshotXLists;
				snapBaseOnly = g_snapshotBaseOnly;
			}

			// Stacked rows need all xLists checked; split rows check only their stackIdx xList.
			std::unordered_map<RE::InventoryEntryData*, int> totalRows;
			for (auto* item : a_list->items) {
				if (!item || !item->obj.IsObject()) continue;
				if (auto* od = item->data.objDesc) totalRows[od]++;
			}

			std::unordered_map<RE::InventoryEntryData*, int> seenCount;
			for (auto* item : a_list->items) {
				if (!item || !item->obj.IsObject()) {
					continue;
				}
				auto* objDesc = item->data.objDesc;
				if (!objDesc) {
					continue;
				}

				const int stackIdx = seenCount[objDesc]++;
				const bool isStackedRow = (totalRows[objDesc] == 1);
				bool match = false;

				if (objDesc->extraLists) {
					int i = 0;
					for (auto* xList : *objDesc->extraLists) {
						if (xList && (isStackedRow || i == stackIdx)) {
							if (snapXLists.contains(xList)) {
								match = true;
								break;
							}
						}
						if (!isStackedRow && i == stackIdx) break;
						++i;
					}
				}

				if (!match) {
					auto* baseObj = objDesc->GetObject();
					if (baseObj) {
						match = snapBaseOnly.contains(baseObj->GetFormID());
					}
				}

				item->obj.SetMember("__fecOSMatch", RE::GFxValue(match));
				item->obj.SetMember("__fecOSGen",   RE::GFxValue(static_cast<double>(a_gen)));
			}
		}

		[[nodiscard]] double GetEntryHeight(const RE::GFxValue& a_state)
		{
			RE::GFxValue list, layout, entryHeightVal;
			if (a_state.GetMember("list", std::addressof(list)) &&
				list.GetMember("layout", std::addressof(layout)) &&
				layout.GetMember("entryHeight", std::addressof(entryHeightVal)) &&
				entryHeightVal.IsNumber()) {
				return entryHeightVal.GetNumber();
			}
			return 28.0;
		}

		class FormatNameHook final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				// Call the previous formatName in the chain first.
				if (a_params.thisPtr && a_params.thisPtr->IsDisplayObject()) {
					if (!a_params.thisPtr->Invoke(kOriginalFormatNameMember, a_params.retVal,
							a_params.args, a_params.argCount)) {
						static bool loggedFailure = false;
						if (!loggedFailure) {
							logger::warn("OutfitSyncIconInjector: failed to invoke chained formatName");
							loggedFailure = true;
						}
					}
				}

				if (a_params.argCount < 3 || !a_params.thisPtr) {
					return;
				}

				auto& entryField = a_params.args[0];
				auto& entryObject = a_params.args[1];
				auto& state = a_params.args[2];

				if (!entryObject.IsObject() || !entryField.IsDisplayObject() || !state.IsObject()) {
					return;
				}

				RE::GFxValue formIdVal;
				if (!entryObject.GetMember("formId", std::addressof(formIdVal)) || !formIdVal.IsNumber()) {
					return;
				}
				const auto entryFormId = static_cast<RE::FormID>(formIdVal.GetNumber());

				RE::GFxValue textVal;
				std::string entryText;
				if (entryObject.GetMember("text", std::addressof(textVal)) && textVal.IsString()) {
					entryText = textVal.GetString();
				}

				// Position-based stamps make snapshot matching lazy and order-independent.
				bool isMatch = false;
				if (g_isNpcPanelActive) {
					std::uint64_t curGen;
					{ std::lock_guard lk(g_snapshotMutex); curGen = g_stampGeneration; }

					RE::GFxValue matchVal, genVal;
					entryObject.GetMember("__fecOSMatch", std::addressof(matchVal));
					entryObject.GetMember("__fecOSGen",   std::addressof(genVal));

					const bool stampValid = matchVal.IsBool() && genVal.IsNumber() &&
						static_cast<std::uint64_t>(genVal.GetNumber()) == curGen;

					if (!stampValid) {
						auto menu = ContainerMenuUtil::GetOpenContainerMenu();
						auto* itemList = menu ? ContainerMenuUtil::GetItemList(menu.get()) : nullptr;
						StampAllSnapshotMatches(itemList, curGen);
						entryObject.GetMember("__fecOSMatch", std::addressof(matchVal));
					}

					if (matchVal.IsBool()) {
						isMatch = matchVal.GetBool();
					}
				}

				if (!g_loggedFirstHit) {
					logger::trace("OutfitSyncIconInjector: formatName hit, "
								  "entryFormId={:08X} text='{}' match={}",
						entryFormId, entryText, isMatch);
					g_loggedFirstHit = true;
				}

				RE::GFxValue wrapper;
				a_params.thisPtr->GetMember(kWrapperName, std::addressof(wrapper));

				if (!wrapper.IsDisplayObject()) {
					if (!isMatch) {
						return;
					}

					if (!a_params.thisPtr->CreateEmptyMovieClip(
							std::addressof(wrapper), kWrapperName, kWrapperDepth)) {
						static bool loggedCreate = false;
						if (!loggedCreate) {
							logger::warn("OutfitSyncIconInjector: CreateEmptyMovieClip (wrapper) failed");
							loggedCreate = true;
						}
						return;
					}

					RE::GFxValue innerIcon;
					if (!wrapper.CreateEmptyMovieClip(std::addressof(innerIcon), kInnerIconName, 1)) {
						static bool loggedInner = false;
						if (!loggedInner) {
							logger::warn("OutfitSyncIconInjector: CreateEmptyMovieClip (inner) failed");
							loggedInner = true;
						}
						return;
					}

					RE::GFxValue pathArg(kIconSwfPath);
					innerIcon.Invoke("loadMovie", nullptr, std::addressof(pathArg), 1);

					wrapper.SetMember("_visible", RE::GFxValue(false));

					if (!g_pendingSwfLoad) {
						g_pendingIcon = innerIcon;
						g_pendingSwfLoad = true;
					}

					static bool loggedLoad = false;
					if (!loggedLoad) {
						logger::trace("OutfitSyncIconInjector: wrapper+icon created, "
									  "loadMovie('{}') queued", kIconSwfPath);
						loggedLoad = true;
					}
				}

				if (isMatch) {
					const double entryHeight = GetEntryHeight(state);

					wrapper.SetMember("_width", RE::GFxValue(IconPositioning::IconSize()));
					wrapper.SetMember("_height", RE::GFxValue(IconPositioning::IconSize()));
					wrapper.SetMember("_y", RE::GFxValue(std::floor((entryHeight - IconPositioning::IconSize()) * 0.5)));
					wrapper.SetMember("_x", RE::GFxValue(
						IconPositioning::FindIconInsertX(*a_params.thisPtr, entryField, {"fecCombatIconWrap", "fecEquipIconWrap"})));
					wrapper.SetMember("_visible", RE::GFxValue(true));
				} else {
					wrapper.SetMember("_visible", RE::GFxValue(false));
				}

				IconPositioning::RepositionFecIcons(*a_params.thisPtr, entryField);
			}
		};

		RE::GPtr<FormatNameHook> g_formatNameHandler;

		void HookFormatNamePrototype(RE::ContainerMenu* a_menu)
		{
			auto* view = a_menu->uiMovie.get();
			if (!view) {
				return;
			}

			RE::GFxValue proto;
			view->GetVariable(std::addressof(proto),
				"_global.InventoryListEntry.prototype");

			if (!proto.IsObject()) {
				logger::trace("OutfitSyncIconInjector: InventoryListEntry.prototype not found");
				return;
			}

			RE::GFxValue hooked;
			if (proto.GetMember(kHookedFlagMember, std::addressof(hooked)) && hooked.IsBool() && hooked.GetBool()) {
				return;
			}

			RE::GFxValue origFn;
			if (!proto.GetMember("formatName", std::addressof(origFn)) || !origFn.IsObject()) {
				logger::warn("OutfitSyncIconInjector: formatName not found on prototype");
				return;
			}
			proto.SetMember(kOriginalFormatNameMember, origFn);

			if (!g_formatNameHandler) {
				g_formatNameHandler = RE::make_gptr<FormatNameHook>();
			}

			RE::GFxValue newFn;
			view->CreateFunction(std::addressof(newFn), g_formatNameHandler.get());
			proto.SetMember("formatName", newFn);

			proto.SetMember(kHookedFlagMember, RE::GFxValue(true));

			logger::trace("OutfitSyncIconInjector: hooked formatName on InventoryListEntry.prototype");
		}

		// No-op handler used to suppress onItemHighlightChange during InvalidateData.
		class SuppressHighlightHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params&) override {}
		};
		RE::GPtr<SuppressHighlightHandler> g_suppressHighlightHandler;

		// Suppress onItemHighlightChange while calling InvalidateData. Otherwise SkyUI can
		// synchronously call UpdateItem3D or RequestItemCardInfo and crash in skee64 or
		// ExtraDataList::GetMaximumCharge. The temporary root property shadows the prototype
		// handler and DeleteMember restores normal lookup afterward.
		void InvalidateItemList(RE::ContainerMenu* a_menu)
		{
			auto& root = a_menu->GetRuntimeData().root;
			RE::GFxValue invLists, itemList;
			if (!root.GetMember("inventoryLists", std::addressof(invLists)) ||
				!invLists.GetMember("itemList", std::addressof(itemList)) ||
				!itemList.IsDisplayObject()) {
				return;
			}

			auto* view = a_menu->uiMovie.get();
			if (!view) {
				itemList.Invoke("InvalidateData", nullptr, nullptr, 0);
				return;
			}

			if (!g_suppressHighlightHandler) {
				g_suppressHighlightHandler = RE::make_gptr<SuppressHighlightHandler>();
			}
			RE::GFxValue noopFn;
			view->CreateFunction(std::addressof(noopFn), g_suppressHighlightHandler.get());

			if (noopFn.IsObject()) {
				root.SetMember("onItemHighlightChange", noopFn);
				itemList.Invoke("InvalidateData", nullptr, nullptr, 0);
				root.DeleteMember("onItemHighlightChange");
			} else {
				itemList.Invoke("InvalidateData", nullptr, nullptr, 0);
			}
		}

		void OnPostDisplay(RE::ContainerMenu* a_menu)
		{
			if (!ShouldAffectMenu(a_menu)) {
				bool hadData = false;
				{
					std::lock_guard lk(g_snapshotMutex);
					if (!g_snapshotXLists.empty() || !g_snapshotBaseOnly.empty()) {
						hadData = (g_previousHash != 0);
						g_snapshotXLists.clear();
						g_snapshotBaseOnly.clear();
						g_previousHash = 0;
						++g_stampGeneration;
					}
				}
				g_previousRawHash = 0;
				g_pendingInvalidate = false;
				if (hadData) {
					InvalidateItemList(a_menu);
				}
				return;
			}

			// Rebuild the snapshot match set only when the cheap snapshot hash changes.
			{
				auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
				const RE::FormID actorId = target ? target->GetFormID() : RE::FormID(0);
				const auto rawHash = ComputeRawSnapshotHash(actorId) ^ IconPositioning::AppearanceHash();
				if (rawHash != g_previousRawHash) {
					g_previousRawHash = rawHash;
					auto newSets = CollectSnapshotMatches(a_menu);
					const auto newHash = HashSnapshotSets(newSets) ^ IconPositioning::AppearanceHash();
					{
						std::lock_guard lk(g_snapshotMutex);
						g_snapshotXLists  = std::move(newSets.xLists);
						g_snapshotBaseOnly = std::move(newSets.baseOnly);
						if (newHash != g_previousHash) {
							++g_stampGeneration;
						}
					}
					if (newHash != g_previousHash) {
						g_previousHash = newHash;
						g_pendingInvalidate = true;
					}
				}
			}

			// Fire deferred invalidation from the previous frame.
			if (g_pendingInvalidate) {
				g_pendingInvalidate = false;
				InvalidateItemList(a_menu);
			}

			auto& root = a_menu->GetRuntimeData().root;
			{
				RE::GFxValue invLists, catList, segVal;
				g_isNpcPanelActive =
					root.GetMember("inventoryLists", std::addressof(invLists)) &&
					invLists.GetMember("categoryList", std::addressof(catList)) &&
					catList.GetMember("activeSegment", std::addressof(segVal)) &&
					segVal.IsNumber() && static_cast<int>(segVal.GetNumber()) == 0;
			}

			HookFormatNamePrototype(a_menu);

			// Re-invalidate after async loadMovie gives the icon a size.
			if (g_pendingSwfLoad && g_pendingIcon.IsDisplayObject()) {
				RE::GFxValue w;
				g_pendingIcon.GetMember("_width", std::addressof(w));
				if (w.IsNumber() && w.GetNumber() > 0.0) {
					InvalidateItemList(a_menu);
					g_pendingSwfLoad = false;
					g_pendingIcon = RE::GFxValue();
				}
			}
		}

	}

	void OutfitSyncIconInjector::Install()
	{
		if (g_installed) {
			return;
		}

		ContainerMenuDisplayHook::Install();
		ContainerMenuUtil::InstallMenuOpenCloseWatcher();

		g_postDisplayHandle = ContainerMenuDisplayHook::AddPostDisplayListener(
			[](RE::ContainerMenu* a_menu) {
				if (a_menu) {
					OnPostDisplay(a_menu);
				}
			});

		g_menuCloseHandle = ContainerMenuUtil::AddOnContainerMenuCloseListener([]() {
			{
				std::lock_guard lk(g_snapshotMutex);
				g_snapshotXLists.clear();
				g_snapshotBaseOnly.clear();
				++g_stampGeneration;
			}
			g_previousHash = 0;
			g_previousRawHash = 0;
			g_pendingInvalidate = false;
			g_pendingSwfLoad = false;
			g_pendingIcon = RE::GFxValue();
			g_loggedFirstHit = false;
		});

		g_installed = true;
		logger::trace("OutfitSyncIconInjector: installed");
	}

	void OutfitSyncIconInjector::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_postDisplayHandle != 0) {
			ContainerMenuDisplayHook::UnregisterListener(g_postDisplayHandle);
			g_postDisplayHandle = 0;
		}
		if (g_menuCloseHandle != 0) {
			ContainerMenuUtil::RemoveOnContainerMenuCloseListener(g_menuCloseHandle);
			g_menuCloseHandle = 0;
		}

		{
			std::lock_guard lk(g_snapshotMutex);
			g_snapshotXLists.clear();
			g_snapshotBaseOnly.clear();
		}
		g_installed = false;
		logger::trace("OutfitSyncIconInjector: uninstalled");
	}

	void OutfitSyncIconInjector::NotifyEquipChanged() noexcept
	{
		// Raw hash ignores worn-state and signatures. Same-form swaps can change the
		// resolved xList without changing baseObjectID, so force a rebuild.
		g_previousRawHash = 0;
	}
}
