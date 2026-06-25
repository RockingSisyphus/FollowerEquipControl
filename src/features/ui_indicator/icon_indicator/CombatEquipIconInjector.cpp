// Hooks SkyUI InventoryListEntry.formatName to add combat equip preference icons.
// Uses separate sentinel and stash members from CombatHeadgearIconInjector so both
// prototype hooks can coexist through chaining. Recycled entry clips reload their
// icon SWF when the preference category changes.

#include "CombatEquipIconInjector.h"

#include "ActorScope.h"
#include "CombatEquipPreference.h"
#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "IconPositioning.h"
#include "PluginSettings.h"
#include "SignatureResolve.h"

#include "PCH.h"

#include <array>
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
		enum class IconCategory : std::uint8_t
		{
			kNone = 0,
			kMelee,   // OneHandRight, OneHandLeft, ShieldLeft, TwoHand
			kAmmo,    // Arrow, Bolt
			kRanged,  // Bow, Crossbow
			kScroll,  // ScrollRight, ScrollLeft, ScrollBoth
			kStaff,   // StaffRight, StaffLeft
		};

		[[nodiscard]] IconCategory ToIconCategory(CombatEquipPreference::Category a_cat) noexcept
		{
			using Cat = CombatEquipPreference::Category;
			switch (a_cat) {
			case Cat::kOneHandRight:
			case Cat::kOneHandLeft:
			case Cat::kShieldLeft:
			case Cat::kTwoHand:
				return IconCategory::kMelee;
			case Cat::kArrow:
			case Cat::kBolt:
				return IconCategory::kAmmo;
			case Cat::kBow:
			case Cat::kCrossbow:
				return IconCategory::kRanged;
			case Cat::kScrollRight:
			case Cat::kScrollLeft:
			case Cat::kScrollBoth:
				return IconCategory::kScroll;
			case Cat::kStaffRight:
			case Cat::kStaffLeft:
				return IconCategory::kStaff;
			default:
				return IconCategory::kNone;
			}
		}

		[[nodiscard]] const char* GetSwfPath(IconCategory a_cat) noexcept
		{
			switch (a_cat) {
			case IconCategory::kMelee:  return "FollowerEquipControl/melee.swf";
			case IconCategory::kAmmo:   return "FollowerEquipControl/ammo.swf";
			case IconCategory::kRanged: return "FollowerEquipControl/ranged.swf";
			case IconCategory::kScroll: return "FollowerEquipControl/scroll.swf";
			case IconCategory::kStaff:  return "FollowerEquipControl/staff.swf";
			default:                    return nullptr;
			}
		}

		constexpr const char* kWrapperName = "fecEquipIconWrap";
		constexpr const char* kInnerIconName = "icon";
		constexpr std::int32_t kWrapperDepth = 9800;

		// Distinct from the headgear icon hook sentinel.
		constexpr const char* kHookedFlagMember = "__fecEquipIconHooked";

		// Previous formatName function stashed on the prototype.
		constexpr const char* kOriginalFormatNameMember = "__fecEquipOriginalFormatName";

		// Stores the currently loaded icon category on the wrapper clip.
		constexpr const char* kIconCatMember = "__fecEquipIconCat";

		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_postDisplayHandle{ 0 };
		ContainerMenuUtil::ListenerHandle g_menuCloseHandle{ 0 };

		// Resolved ExtraDataList* -> icon category, rebuilt on PostDisplay and read via stamps.
		std::unordered_map<RE::ExtraDataList*, IconCategory> g_preferredXLists;

		// baseFormId -> icon category fallback for entries without a specific ExtraDataList.
		std::unordered_map<RE::FormID, IconCategory> g_preferredBaseOnly;

		std::uint64_t g_stampGeneration{ 0 };

		std::mutex g_preferredMutex;

		// Snapshot hash of the preferred map.
		std::size_t g_previousMapHash{ 0 };

		// Cheap pre-hash; avoids SignatureResolve and inventory walks when preferences are unchanged.
		std::size_t g_previousRawHash{ 0 };

		// Defer InvalidateData to the next PostDisplay. Mid-frame invalidation can fire
		// onItemHighlightChange -> UpdateMagic3D -> skee64 with a partially initialized
		// NiNode overlay and crash.
		bool g_pendingInvalidate{ false };

		// Inner icon clip awaiting async loadMovie completion.
		RE::GFxValue g_pendingIcon;
		bool g_pendingSwfLoad{ false };

		// True when the Take tab (NPC->Player) is active.
		bool g_isNpcPanelActive{ false };

		bool g_loggedFirstHit{ false };

		[[nodiscard]] bool IsFeatureEnabled()
		{
			const auto& cfg = PluginSettings::Get();
			return cfg.iconAppearance.enableIconIndicator &&
			       (cfg.combatEquipPreference.enableScoring || cfg.combatEquipEnforcement.enableMeleeEnforcement) && cfg.iconAppearance.enableCombatEquipIcon;
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

		struct EquipPrefMaps
		{
			std::unordered_map<RE::ExtraDataList*, IconCategory> xLists;
			std::unordered_map<RE::FormID, IconCategory>         baseOnly;
		};

		// Collects non-headgear preferred instances and their icon categories.
		// SignatureResolve is used to find exact ExtraDataList matches when possible.
		[[nodiscard]] EquipPrefMaps CollectPreferredMatches(RE::ContainerMenu* a_menu)
		{
			EquipPrefMaps result;
			auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
			if (!target) {
				return result;
			}
			const auto actorId = target->GetFormID();

			using Cat = CombatEquipPreference::Category;
			static constexpr std::array kCats{
				Cat::kOneHandRight,
				Cat::kOneHandLeft,
				Cat::kShieldLeft,
				Cat::kTwoHand,
				Cat::kBow,
				Cat::kArrow,
				Cat::kCrossbow,
				Cat::kBolt,
				Cat::kStaffRight,
				Cat::kStaffLeft,
				Cat::kScrollRight,
				Cat::kScrollLeft,
				Cat::kScrollBoth,
			};

			// Prevent same-form categories from resolving to the same ExtraDataList.
			std::unordered_map<RE::FormID, std::unordered_set<RE::ExtraDataList*>> claimedByForm;
			for (auto cat : kCats) {
				auto entry = CombatEquipPreference::GetEntry(actorId, cat);
				if (!entry.has_value() || entry->baseObjectID == 0) {
					continue;
				}

				auto iconCat = ToIconCategory(cat);
				if (iconCat == IconCategory::kNone) {
					continue;
				}

				auto* baseObj = RE::TESForm::LookupByID<RE::TESBoundObject>(entry->baseObjectID);
				if (!baseObj) {
					continue;
				}

				// Resolve even plain-item signatures; empty StableIdentity still allows worn-state tiebreaks.
				auto& claimed = claimedByForm[entry->baseObjectID];
				const auto resolved = claimed.empty()
					? SignatureResolve::Resolve(
						target.get(), baseObj, entry->signature,
						std::nullopt, SignatureResolve::Policy::kIdentityOnly)
					: SignatureResolve::Resolve(
						target.get(), baseObj, entry->signature,
						std::nullopt, SignatureResolve::Policy::kIdentityOnly,
						claimed);
				if (resolved.HasXList()) {
					claimed.insert(resolved.xList);
					result.xLists.try_emplace(resolved.xList, iconCat);
					continue;
				}

				result.baseOnly.try_emplace(entry->baseObjectID, iconCat);
			}
			return result;
		}

		// Cheap per-frame hash used to skip expensive preference resolution when base IDs are unchanged.
		[[nodiscard]] std::size_t ComputeRawPreferenceHash(RE::FormID a_actorId)
		{
			using Cat = CombatEquipPreference::Category;
			static constexpr std::array kCategories{
				Cat::kOneHandRight,
				Cat::kOneHandLeft,
				Cat::kShieldLeft,
				Cat::kTwoHand,
				Cat::kBow,
				Cat::kArrow,
				Cat::kCrossbow,
				Cat::kBolt,
				Cat::kStaffRight,
				Cat::kStaffLeft,
				Cat::kScrollRight,
				Cat::kScrollLeft,
				Cat::kScrollBoth,
			};
			std::size_t h = std::hash<RE::FormID>{}(a_actorId);
			for (auto cat : kCategories) {
				auto e = CombatEquipPreference::GetEntry(a_actorId, cat);
				const RE::FormID id = e.has_value() ? e->baseObjectID : RE::FormID(0);
				h ^= std::hash<RE::FormID>{}(id) + 0x9e3779b9u + (h << 6) + (h >> 2);
				h ^= std::hash<std::uint32_t>{}(static_cast<std::uint32_t>(cat)) + 0x517cc1b7u + (h << 3) + (h >> 5);
			}
			return h;
		}

		[[nodiscard]] std::size_t HashEquipMaps(const EquipPrefMaps& a_maps)
		{
			std::size_t h = a_maps.xLists.size() + a_maps.baseOnly.size();
			for (const auto& [xList, cat] : a_maps.xLists) {
				h ^= std::hash<uintptr_t>{}(reinterpret_cast<uintptr_t>(xList)) + 0x9e3779b9 + (h << 6) + (h >> 2);
				h ^= static_cast<std::size_t>(cat) + 0x517cc1b7 + (h << 6) + (h >> 2);
			}
			for (const auto& [formId, cat] : a_maps.baseOnly) {
				h ^= std::hash<RE::FormID>{}(formId) + 0x9e3779b9 + (h << 6) + (h >> 2);
				h ^= static_cast<std::size_t>(cat) + 0x517cc1b7 + (h << 6) + (h >> 2);
			}
			return h;
		}

		// Stamps item rows lazily so formatName can read icon matches without resolving inventory.
		void StampAllEquipMatches(RE::ItemList* a_list, std::uint64_t a_gen)
		{
			if (!a_list) {
				return;
			}

			// Snapshot maps under lock, then iterate without holding it.
			std::unordered_map<RE::ExtraDataList*, IconCategory> prefXLists;
			std::unordered_map<RE::FormID, IconCategory> prefBaseOnly;
			{
				std::lock_guard lk(g_preferredMutex);
				prefXLists  = g_preferredXLists;
				prefBaseOnly = g_preferredBaseOnly;
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
				IconCategory cat = IconCategory::kNone;

				if (objDesc->extraLists) {
					int i = 0;
					for (auto* xList : *objDesc->extraLists) {
						if (xList && (isStackedRow || i == stackIdx)) {
							auto it = prefXLists.find(xList);
							if (it != prefXLists.end()) {
								cat = it->second;
								break;
							}
						}
						if (!isStackedRow && i == stackIdx) break;
						++i;
					}
				}

				if (cat == IconCategory::kNone) {
					auto* baseObj = objDesc->GetObject();
					if (baseObj) {
						auto it = prefBaseOnly.find(baseObj->GetFormID());
						if (it != prefBaseOnly.end()) {
							cat = it->second;
						}
					}
				}

				item->obj.SetMember("__fecEqCat", RE::GFxValue(static_cast<double>(cat)));
				item->obj.SetMember("__fecEqGen", RE::GFxValue(static_cast<double>(a_gen)));
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
							logger::warn("CombatEquipIconInjector: failed to invoke chained formatName");
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
				IconCategory iconCat = IconCategory::kNone;
				if (g_isNpcPanelActive) {
					std::uint64_t curGen;
					{ std::lock_guard lk(g_preferredMutex); curGen = g_stampGeneration; }

					RE::GFxValue catVal, genVal;
					entryObject.GetMember("__fecEqCat", std::addressof(catVal));
					entryObject.GetMember("__fecEqGen", std::addressof(genVal));

					const bool stampValid = catVal.IsNumber() && genVal.IsNumber() &&
						static_cast<std::uint64_t>(genVal.GetNumber()) == curGen;

					if (!stampValid) {
						auto menu = ContainerMenuUtil::GetOpenContainerMenu();
						auto* itemList = menu ? ContainerMenuUtil::GetItemList(menu.get()) : nullptr;
						StampAllEquipMatches(itemList, curGen);
						entryObject.GetMember("__fecEqCat", std::addressof(catVal));
					}

					if (catVal.IsNumber()) {
						iconCat = static_cast<IconCategory>(static_cast<int>(catVal.GetNumber()));
					}
				}
				const bool isMatch = iconCat != IconCategory::kNone;

				if (!g_loggedFirstHit) {
					logger::trace("CombatEquipIconInjector: formatName hit, "
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
							logger::warn("CombatEquipIconInjector: CreateEmptyMovieClip (wrapper) failed");
							loggedCreate = true;
						}
						return;
					}

					RE::GFxValue innerIcon;
					if (!wrapper.CreateEmptyMovieClip(std::addressof(innerIcon), kInnerIconName, 1)) {
						static bool loggedInner = false;
						if (!loggedInner) {
							logger::warn("CombatEquipIconInjector: CreateEmptyMovieClip (inner) failed");
							loggedInner = true;
						}
						return;
					}

					const char* swfPath = GetSwfPath(iconCat);
					RE::GFxValue pathArg(swfPath);
					innerIcon.Invoke("loadMovie", nullptr, std::addressof(pathArg), 1);

					wrapper.SetMember("_visible", RE::GFxValue(false));
					wrapper.SetMember(kIconCatMember, RE::GFxValue(static_cast<double>(iconCat)));

					if (!g_pendingSwfLoad) {
						g_pendingIcon = innerIcon;
						g_pendingSwfLoad = true;
					}

					static bool loggedLoad = false;
					if (!loggedLoad) {
						logger::trace("CombatEquipIconInjector: wrapper+icon created, "
									  "loadMovie('{}') queued", swfPath);
						loggedLoad = true;
					}
				} else if (isMatch) {
					// Recycled clips may need a different icon SWF.
					RE::GFxValue storedCat;
					wrapper.GetMember(kIconCatMember, std::addressof(storedCat));
					const auto currentCat = storedCat.IsNumber()
						? static_cast<IconCategory>(static_cast<int>(storedCat.GetNumber()))
						: IconCategory::kNone;

					if (currentCat != iconCat) {
						RE::GFxValue innerIcon;
						wrapper.GetMember(kInnerIconName, std::addressof(innerIcon));
						if (innerIcon.IsDisplayObject()) {
							const char* swfPath = GetSwfPath(iconCat);
							RE::GFxValue pathArg(swfPath);
							innerIcon.Invoke("loadMovie", nullptr, std::addressof(pathArg), 1);
							wrapper.SetMember(kIconCatMember, RE::GFxValue(static_cast<double>(iconCat)));
						}
					}
				}

				if (isMatch) {
					const double entryHeight = GetEntryHeight(state);

					wrapper.SetMember("_width", RE::GFxValue(IconPositioning::IconSize()));
					wrapper.SetMember("_height", RE::GFxValue(IconPositioning::IconSize()));
					wrapper.SetMember("_y", RE::GFxValue(std::floor((entryHeight - IconPositioning::IconSize()) * 0.5)));
					wrapper.SetMember("_x", RE::GFxValue(
						IconPositioning::FindIconInsertX(*a_params.thisPtr, entryField, {"fecCombatIconWrap"})));
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
				logger::trace("CombatEquipIconInjector: InventoryListEntry.prototype not found");
				return;
			}

			RE::GFxValue hooked;
			if (proto.GetMember(kHookedFlagMember, std::addressof(hooked)) && hooked.IsBool() && hooked.GetBool()) {
				return;
			}

			RE::GFxValue origFn;
			if (!proto.GetMember("formatName", std::addressof(origFn)) || !origFn.IsObject()) {
				logger::warn("CombatEquipIconInjector: formatName not found on prototype");
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

			logger::trace("CombatEquipIconInjector: hooked formatName on InventoryListEntry.prototype");
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
					if (!g_preferredXLists.empty() || !g_preferredBaseOnly.empty()) {
						hadData = (g_previousMapHash != 0);
						g_preferredXLists.clear();
						g_preferredBaseOnly.clear();
						g_previousMapHash = 0;
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

			// Rebuild the full match map only when the cheap preference hash changes.
			{
				auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
				const RE::FormID actorId = target ? target->GetFormID() : RE::FormID(0);
				const auto rawHash = ComputeRawPreferenceHash(actorId) ^ IconPositioning::AppearanceHash();
				if (rawHash != g_previousRawHash) {
					g_previousRawHash = rawHash;
					auto newMaps = CollectPreferredMatches(a_menu);
					const auto newHash = HashEquipMaps(newMaps) ^ IconPositioning::AppearanceHash();
					{
						std::lock_guard lk(g_preferredMutex);
						g_preferredXLists  = std::move(newMaps.xLists);
						g_preferredBaseOnly = std::move(newMaps.baseOnly);
						if (newHash != g_previousMapHash) {
							++g_stampGeneration;
						}
					}
					if (newHash != g_previousMapHash) {
						g_previousMapHash = newHash;
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

	void CombatEquipIconInjector::Install()
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
				g_preferredXLists.clear();
				g_preferredBaseOnly.clear();
				++g_stampGeneration;
			}
			g_previousMapHash = 0;
			g_previousRawHash = 0;
			g_pendingInvalidate = false;
			g_pendingSwfLoad = false;
			g_pendingIcon = RE::GFxValue();
			g_loggedFirstHit = false;
		});

		g_installed = true;
		logger::trace("CombatEquipIconInjector: installed");
	}

	void CombatEquipIconInjector::Uninstall()
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
			g_preferredXLists.clear();
			g_preferredBaseOnly.clear();
		}
		g_installed = false;
		logger::trace("CombatEquipIconInjector: uninstalled");
	}

	void CombatEquipIconInjector::NotifyEquipChanged() noexcept
	{
		// Raw hash ignores worn-state and signatures. Same-form swaps can change the
		// resolved xList without changing baseObjectID, so force a rebuild.
		g_previousRawHash = 0;
	}
}
