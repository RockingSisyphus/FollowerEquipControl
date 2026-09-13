// Hooks SkyUI InventoryListEntry.formatName to add the preferred headgear icon.
// The loaded SWF lives inside a wrapper clip so loadMovie's async property reset
// cannot override wrapper visibility or positioning. Preference changes invalidate
// the SkyUI item list so visible entry clips are formatted again.

#include "CombatHeadgearIconInjector.h"

#include "ActorScope.h"
#include "CombatEquipPreference.h"
#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "IconPositioning.h"
#include "PluginSettings.h"
#include "SignatureResolve.h"

#include "PCH.h"

#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace FEC
{
	namespace
	{
		// Wrapper visibility is controlled here, not on the loadMovie target.
		constexpr const char* kWrapperName = "fecCombatIconWrap";

		constexpr const char* kInnerIconName = "icon";

		constexpr std::int32_t kWrapperDepth = 9900;

		constexpr const char* kIconSwfPath = "FollowerEquipControl/headgear.swf";

		// Sentinel on InventoryListEntry.prototype to prevent double-hooking.
		constexpr const char* kHookedFlagMember = "__fecCombatIconHooked";

		// Original formatName function stashed on the prototype.
		constexpr const char* kOriginalFormatNameMember = "__fecOriginalFormatName";

		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_postDisplayHandle{ 0 };
		ContainerMenuUtil::ListenerHandle g_menuCloseHandle{ 0 };

		// Preferred headgear ExtraDataList; null means base-only or no preference.
		RE::ExtraDataList* g_preferredXList{ nullptr };

		// Base FormID fallback when no specific ExtraDataList was resolved.
		RE::FormID g_preferredBaseFormId{ 0 };

		bool g_preferredBaseOnly{ false };

		// Item-row stamps become stale when this generation changes.
		std::uint64_t g_stampGeneration{ 0 };

		std::mutex g_preferredMutex;

		// Preferred match hash used for list invalidation.
		std::size_t g_previousHash{ 0 };

		// Cheap pre-hash; avoids SignatureResolve and inventory walks when preference base ID is unchanged.
		std::size_t g_previousRawHash{ 0 };

		// Defer InvalidateData to the next PostDisplay to avoid onItemHighlightChange during AS2 execution.
		bool g_pendingInvalidate{ false };

		// Inner icon clip awaiting async loadMovie completion; _width > 0 means loaded.
		RE::GFxValue g_pendingIcon;
		bool g_pendingSwfLoad{ false };

		// True when the Take tab (NPC->Player) is active; icons only show on the NPC side.
		bool g_isNpcPanelActive{ false };

		bool g_loggedFirstHit{ false };

		[[nodiscard]] bool IsFeatureEnabled()
		{
			const auto& ia = PluginSettings::Get().iconAppearance;
			return ia.enableIconIndicator &&
			       PluginSettings::Get().combatEquipRestore.enableHeadgearAutoEquip && ia.enableHeadgearIcon;
		}

		[[nodiscard]] bool IsSkyUiPresent(const RE::GFxValue& a_root)
		{
			RE::GFxValue v;
			return a_root.GetMember("_platform", std::addressof(v)) && v.IsNumber();
		}

		[[nodiscard]] bool IsNpcMode(RE::ContainerMenu* a_menu)
		{
			return a_menu && a_menu->GetContainerMode() == RE::ContainerMenu::ContainerMode::kNPCMode;
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

		// Cheap per-frame hash used to skip expensive preference resolution when base ID is unchanged.
		[[nodiscard]] std::size_t ComputeRawPreferenceHash(RE::FormID a_actorId)
		{
			auto e = CombatEquipPreference::GetEntry(a_actorId, CombatEquipPreference::Category::kHeadgear);
			const RE::FormID id = e.has_value() ? e->baseObjectID : RE::FormID(0);
			return std::hash<RE::FormID>{}(id) ^ (std::hash<RE::FormID>{}(a_actorId) * 2654435761u);
		}

		// Resolves the preferred headgear instance for the follower being traded with.
		struct HeadgearPref
		{
			RE::ExtraDataList* xList{ nullptr };
			RE::FormID         baseFormId{ 0 };
			bool               hasPreference{ false };
		};

		[[nodiscard]] HeadgearPref CollectPreferredHeadgear(RE::ContainerMenu* a_menu)
		{
			auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
			if (!target) {
				return {};
			}
			const auto pref = CombatEquipPreference::GetEntry(
				target->GetFormID(), CombatEquipPreference::Category::kHeadgear);
			if (!pref.has_value() || pref->baseObjectID == 0) {
				return {};
			}

			auto* baseObj = RE::TESForm::LookupByID<RE::TESBoundObject>(pref->baseObjectID);
			if (!baseObj) {
				return {};
			}

			// Resolve even plain-item signatures; empty StableIdentity still allows worn-state tiebreaks.
			auto resolved = SignatureResolve::Resolve(
				target.get(), baseObj, pref->signature,
				std::nullopt, SignatureResolve::Policy::kIdentityOnly);
			if (resolved.HasXList()) {
				return { resolved.xList, pref->baseObjectID, true };
			}

			// Base-only fallback when no exact instance can be resolved.
			return { nullptr, pref->baseObjectID, true };
		}

		// Stamps item rows lazily so formatName can read headgear matches without resolving inventory.
		void StampAllHeadgearMatches(RE::ItemList* a_list, std::uint64_t a_gen)
		{
			if (!a_list) {
				return;
			}

			// Snapshot state under lock, then iterate without holding it.
			RE::ExtraDataList* prefXList;
			RE::FormID prefBase;
			bool prefBaseOnly;
			{
				std::lock_guard lk(g_preferredMutex);
				prefXList    = g_preferredXList;
				prefBase     = g_preferredBaseFormId;
				prefBaseOnly = g_preferredBaseOnly;
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
				bool match = false;

				if (prefXList) {
					if (objDesc->extraLists) {
						int i = 0;
						for (auto* xList : *objDesc->extraLists) {
							if (i++ == stackIdx) {
								match = (xList == prefXList);
								break;
							}
						}
					}
				} else if (prefBaseOnly) {
					auto* baseObj = objDesc->GetObject();
					match = baseObj && (baseObj->GetFormID() == prefBase);
				}

				item->obj.SetMember("__fecHGMatch", RE::GFxValue(match));
				item->obj.SetMember("__fecHGGen",   RE::GFxValue(static_cast<double>(a_gen)));
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

		// Wraps SkyUI InventoryListEntry.formatName and toggles the headgear icon.

		class FormatNameHook final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				// Call the original formatName first.
				if (a_params.thisPtr && a_params.thisPtr->IsDisplayObject()) {
					if (!a_params.thisPtr->Invoke(kOriginalFormatNameMember, a_params.retVal,
							a_params.args, a_params.argCount)) {
						static bool loggedFailure = false;
						if (!loggedFailure) {
							logger::warn("CombatHeadgearIconInjector: failed to invoke original formatName");
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

				// Position-based stamps make matching lazy and order-independent.
				bool isMatch = false;
				if (g_isNpcPanelActive) {
					std::uint64_t curGen;
					{ std::lock_guard lk(g_preferredMutex); curGen = g_stampGeneration; }

					RE::GFxValue matchVal, genVal;
					entryObject.GetMember("__fecHGMatch", std::addressof(matchVal));
					entryObject.GetMember("__fecHGGen",   std::addressof(genVal));

					const bool stampValid = matchVal.IsBool() && genVal.IsNumber() &&
						static_cast<std::uint64_t>(genVal.GetNumber()) == curGen;

					if (!stampValid) {
						auto menu = ContainerMenuUtil::GetOpenContainerMenu();
						auto* itemList = menu ? ContainerMenuUtil::GetItemList(menu.get()) : nullptr;
						StampAllHeadgearMatches(itemList, curGen);
						entryObject.GetMember("__fecHGMatch", std::addressof(matchVal));
					}

					if (matchVal.IsBool()) {
						isMatch = matchVal.GetBool();
					}
				}

				if (!g_loggedFirstHit) {
					logger::trace("CombatHeadgearIconInjector: formatName hit, "
								  "entryFormId={:08X} text='{}' match={}",
						entryFormId, entryText, isMatch);
					g_loggedFirstHit = true;
				}

				// Toggle visibility on the wrapper; loadMovie can reset properties on the inner clip.
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
							logger::warn("CombatHeadgearIconInjector: CreateEmptyMovieClip (wrapper) failed");
							loggedCreate = true;
						}
						return;
					}

					RE::GFxValue innerIcon;
					if (!wrapper.CreateEmptyMovieClip(std::addressof(innerIcon), kInnerIconName, 1)) {
						static bool loggedInner = false;
						if (!loggedInner) {
							logger::warn("CombatHeadgearIconInjector: CreateEmptyMovieClip (inner) failed");
							loggedInner = true;
						}
						return;
					}

					RE::GFxValue pathArg(kIconSwfPath);
					innerIcon.Invoke("loadMovie", nullptr, std::addressof(pathArg), 1);

					wrapper.SetMember("_visible", RE::GFxValue(false));

					// Keep the inner clip so PostDisplay can detect async load completion.
					if (!g_pendingSwfLoad) {
						g_pendingIcon = innerIcon;
						g_pendingSwfLoad = true;
					}

					static bool loggedLoad = false;
					if (!loggedLoad) {
						logger::trace("CombatHeadgearIconInjector: wrapper+icon created, "
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
						IconPositioning::FindIconInsertX(*a_params.thisPtr, entryField)));
					wrapper.SetMember("_visible", RE::GFxValue(true));
				} else {
					wrapper.SetMember("_visible", RE::GFxValue(false));
				}

				IconPositioning::RepositionFecIcons(*a_params.thisPtr, entryField);
			}
		};

		// Keep the handler alive for Scaleform.
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
				logger::trace("CombatHeadgearIconInjector: InventoryListEntry.prototype not found");
				return;
			}

			RE::GFxValue hooked;
			if (proto.GetMember(kHookedFlagMember, std::addressof(hooked)) && hooked.IsBool() && hooked.GetBool()) {
				return;
			}

			RE::GFxValue origFn;
			if (!proto.GetMember("formatName", std::addressof(origFn)) || !origFn.IsObject()) {
				logger::warn("CombatHeadgearIconInjector: formatName not found on prototype");
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

			logger::trace("CombatHeadgearIconInjector: hooked formatName on InventoryListEntry.prototype");
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
					std::lock_guard lk(g_preferredMutex);
					if (g_preferredXList != nullptr || g_preferredBaseOnly) {
						hadData = (g_previousHash != 0);
						g_preferredXList      = nullptr;
						g_preferredBaseFormId = 0;
						g_preferredBaseOnly   = false;
						g_previousHash        = 0;
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

			// Rebuild the preferred match only when the cheap preference hash changes.
			{
				auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
				const RE::FormID actorId = target ? target->GetFormID() : RE::FormID(0);
				const auto rawHash = ComputeRawPreferenceHash(actorId) ^ IconPositioning::AppearanceHash();
				if (rawHash != g_previousRawHash) {
					g_previousRawHash = rawHash;
					auto newPref = CollectPreferredHeadgear(a_menu);
					const auto newHash = (std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(newPref.xList))
						^ std::hash<RE::FormID>{}(newPref.baseFormId)) ^ IconPositioning::AppearanceHash();
					{
						std::lock_guard lk(g_preferredMutex);
						g_preferredXList      = newPref.xList;
						g_preferredBaseFormId = newPref.baseFormId;
						g_preferredBaseOnly   = newPref.hasPreference && !newPref.xList;
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

			// Segment 0 is Take (NPC->Player); icons only appear on the NPC side.
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

	void CombatHeadgearIconInjector::Install()
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
				std::lock_guard lk(g_preferredMutex);
				g_preferredXList      = nullptr;
				g_preferredBaseFormId = 0;
				g_preferredBaseOnly   = false;
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
		logger::trace("CombatHeadgearIconInjector: installed");
	}

	void CombatHeadgearIconInjector::Uninstall()
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
			std::lock_guard lk(g_preferredMutex);
			g_preferredXList      = nullptr;
			g_preferredBaseFormId = 0;
			g_preferredBaseOnly   = false;
		}
		g_installed = false;
		logger::trace("CombatHeadgearIconInjector: uninstalled");
	}
}
