#include "WeaponRechargePickerUI.h"

#include "ContainerMenuUtil.h"
#include "PluginSettings.h"
#include "WeaponRechargeGfxUtil.h"
#include "WeaponRechargeTypes.h"

#include <array>

namespace FEC::WeaponRecharge
{
	namespace
	{
		constexpr double kICT_LIST = 14.0;

		std::atomic_bool g_handlersCreated{ false };

		struct PendingPick
		{
			PickerUI::PickCallback onPick{};
			PickerUI::CancelCallback onCancel{};
		};

		PendingPick& Pending()
		{
			static PendingPick p{};
			return p;
		}

		[[nodiscard]] const char* SoulLabel(RE::SOUL_LEVEL a_soul)
		{
			switch (a_soul) {
			case RE::SOUL_LEVEL::kPetty:
				return "Petty";
			case RE::SOUL_LEVEL::kLesser:
				return "Lesser";
			case RE::SOUL_LEVEL::kCommon:
				return "Common";
			case RE::SOUL_LEVEL::kGreater:
				return "Greater";
			case RE::SOUL_LEVEL::kGrand:
				return "Grand";
			default:
				return nullptr;
			}
		}

		void RestoreItemCard(RE::ContainerMenu* a_menu)
		{
			if (!a_menu) {
				return;
			}
			auto& root = a_menu->GetRuntimeData().root;
			if (!root.IsObject()) {
				return;
			}
			RE::GFxValue itemCard;
			if (!root.GetMember("itemCard", std::addressof(itemCard)) || (!itemCard.IsObject() && !itemCard.IsDisplayObject())) {
				return;
			}
			RE::GFxValue saved;
			if (root.GetMember("__fecWRSavedItemInfo", std::addressof(saved)) && (saved.IsObject() || saved.IsDisplayObject())) {
				itemCard.SetMember("itemInfo", saved);
			}
			root.DeleteMember("__fecWRSavedItemInfo");
		}

		[[nodiscard]] bool TryInstallListeners(RE::ContainerMenu* a_menu)
		{
			if (!a_menu) {
				return false;
			}
			auto& root = a_menu->GetRuntimeData().root;
			if (!root.IsObject()) {
				return false;
			}

			bool already = false;
			if (WeaponRecharge::GfxUtil::TryGetMemberBool(root, "__fecWRListenersInstalled", already) && already) {
				return true;
			}

			auto view = a_menu->uiMovie.get();
			if (!view) {
				return false;
			}

			RE::GFxValue itemCard;
			if (!root.GetMember("itemCard", std::addressof(itemCard)) || (!itemCard.IsObject() && !itemCard.IsDisplayObject())) {
				return false;
			}

			// Native handlers live on root so ActionScript event listeners can call them.
			RE::GFxValue onPickFn;
			if (!root.GetMember("__fecWROnPick", std::addressof(onPickFn))) {
				return false;
			}
			RE::GFxValue onCloseFn;
			if (!root.GetMember("__fecWROnClose", std::addressof(onCloseFn))) {
				return false;
			}

			RE::GFxValue evItemPress;
			view->CreateString(std::addressof(evItemPress), "itemPress");
			RE::GFxValue cbPick;
			view->CreateString(std::addressof(cbPick), "__fecWROnPick");
			std::array<RE::GFxValue, 3> args1{ evItemPress, root, cbPick };
			(void)itemCard.Invoke("addEventListener", nullptr, args1.data(), static_cast<std::uint32_t>(args1.size()));

			RE::GFxValue evSubMenu;
			view->CreateString(std::addressof(evSubMenu), "subMenuAction");
			RE::GFxValue cbClose;
			view->CreateString(std::addressof(cbClose), "__fecWROnClose");
			std::array<RE::GFxValue, 3> args2{ evSubMenu, root, cbClose };
			(void)itemCard.Invoke("addEventListener", nullptr, args2.data(), static_cast<std::uint32_t>(args2.size()));

			root.SetMember("__fecWRListenersInstalled", RE::GFxValue(true));
			return true;
		}

		class PickHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				auto menu = ::FEC::ContainerMenuUtil::GetOpenContainerMenu();
				if (!menu) {
					return;
				}
				auto& root = menu->GetRuntimeData().root;
				if (!root.IsObject()) {
					return;
				}

				bool open = false;
				(void)WeaponRecharge::GfxUtil::TryGetMemberBool(root, "__fecWROpen", open);
				if (!open) {
					return;
				}

				if (a_params.argCount < 1) {
					return;
				}
				const auto& eventObj = a_params.args[0];
				if (!eventObj.IsObject() && !eventObj.IsDisplayObject()) {
					return;
				}
				RE::GFxValue entryObj;
				if (!eventObj.GetMember("entry", std::addressof(entryObj)) || (!entryObj.IsObject() && !entryObj.IsDisplayObject())) {
					return;
				}

				double formIdNum = 0.0;
				double sourceNum = 0.0;
				double soulNum = 0.0;
				(void)WeaponRecharge::GfxUtil::TryGetMemberNumber(entryObj, "fecFormId", formIdNum);
				(void)WeaponRecharge::GfxUtil::TryGetMemberNumber(entryObj, "fecSource", sourceNum);
				(void)WeaponRecharge::GfxUtil::TryGetMemberNumber(entryObj, "fecSoul", soulNum);
				const auto formId = static_cast<RE::FormID>(static_cast<std::uint32_t>(formIdNum));
				const auto source = static_cast<SoulGemSource>(static_cast<std::uint8_t>(static_cast<std::uint32_t>(sourceNum)));
				const auto soul = static_cast<RE::SOUL_LEVEL>(static_cast<std::int32_t>(static_cast<std::uint32_t>(soulNum)));

				auto* gemForm = RE::TESForm::LookupByID(formId);
				auto* gem = gemForm ? gemForm->As<RE::TESSoulGem>() : nullptr;
				if (!gem) {
					return;
				}

				auto cb = std::move(Pending().onPick);
				Pending().onPick = {};
				Pending().onCancel = {};

				root.SetMember("__fecWROpen", RE::GFxValue(false));
				RestoreItemCard(menu.get());

				if (cb) {
					cb(gem, source, soul);
				}
			}
		};

		class CloseHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				auto menu = ::FEC::ContainerMenuUtil::GetOpenContainerMenu();
				if (!menu) {
					return;
				}
				auto& root = menu->GetRuntimeData().root;
				if (!root.IsObject()) {
					return;
				}

				bool open = false;
				(void)WeaponRecharge::GfxUtil::TryGetMemberBool(root, "__fecWROpen", open);
				if (!open) {
					return;
				}

				if (a_params.argCount < 1) {
					return;
				}
				const auto& eventObj = a_params.args[0];
				if (!eventObj.IsObject() && !eventObj.IsDisplayObject()) {
					return;
				}

				std::string_view menuName;
				(void)WeaponRecharge::GfxUtil::TryGetMemberString(eventObj, "menu", menuName);
				bool opening = true;
				(void)WeaponRecharge::GfxUtil::TryGetMemberBool(eventObj, "opening", opening);
				if (menuName != "list" || opening) {
					return;
				}

				auto cb = std::move(Pending().onCancel);
				Pending().onPick = {};
				Pending().onCancel = {};

				root.SetMember("__fecWROpen", RE::GFxValue(false));
				RestoreItemCard(menu.get());

				if (cb) {
					cb();
				}
			}
		};

		RE::GFxFunctionHandler* GetPickHandler()
		{
			static PickHandler* handler = []() {
				auto* h = new PickHandler();
				h->AddRef();
				return h;
			}();
			return handler;
		}

		RE::GFxFunctionHandler* GetCloseHandler()
		{
			static CloseHandler* handler = []() {
				auto* h = new CloseHandler();
				h->AddRef();
				return h;
			}();
			return handler;
		}
	}

	void PickerUI::EnsureHandlersCreated()
	{
		g_handlersCreated.store(true);
		(void)GetPickHandler();
		(void)GetCloseHandler();
	}

	bool PickerUI::Open(
		RE::ContainerMenu* a_menu,
		const std::vector<SoulGemOption>& a_options,
		double a_selectedCurrentAbs,
		double a_selectedMaxAbs,
		PickCallback a_onPick,
		CancelCallback a_onCancel)
	{
		if (!a_menu) {
			return false;
		}
		auto& root = a_menu->GetRuntimeData().root;
		if (!root.IsObject()) {
			return false;
		}

		bool alreadyOpen = false;
		(void)WeaponRecharge::GfxUtil::TryGetMemberBool(root, "__fecWROpen", alreadyOpen);
		if (alreadyOpen) {
			return true;
		}

		auto view = a_menu->uiMovie.get();
		if (!view) {
			return false;
		}

		RE::GFxValue pickFn;
		view->CreateFunction(std::addressof(pickFn), GetPickHandler());
		root.SetMember("__fecWROnPick", pickFn);
		RE::GFxValue closeFn;
		view->CreateFunction(std::addressof(closeFn), GetCloseHandler());
		root.SetMember("__fecWROnClose", closeFn);

		if (!TryInstallListeners(a_menu)) {
			return false;
		}

		RE::GFxValue itemCard;
		if (!root.GetMember("itemCard", std::addressof(itemCard)) || (!itemCard.IsObject() && !itemCard.IsDisplayObject())) {
			return false;
		}

		// Save itemInfo so the normal item card can be restored after close.
		RE::GFxValue savedItemInfo;
		double currentChargePct = 0.0;
		if (itemCard.GetMember("itemInfo", std::addressof(savedItemInfo)) && (savedItemInfo.IsObject() || savedItemInfo.IsDisplayObject())) {
			root.SetMember("__fecWRSavedItemInfo", savedItemInfo);
			(void)WeaponRecharge::GfxUtil::TryGetMemberNumber(savedItemInfo, "charge", currentChargePct);
		}

		struct Entry
		{
			RE::TESSoulGem* gem{ nullptr };
			SoulGemSource source{ SoulGemSource::kPlayer };
			std::int32_t count{ 0 };
			double addedPercent{ 0.0 };
			double soulValue{ 0.0 };
			RE::SOUL_LEVEL soul{ RE::SOUL_LEVEL::kNone };
			RE::FormID gemFormID{ 0 };
		};

		std::vector<Entry> entries;
		entries.reserve(a_options.size());

		const auto& cfg = PluginSettings::Get().weaponEnchantmentRecharge;
		const bool advanced = cfg.advancedSoulGemDisplay;
		const auto containedSoulMode = cfg.containedSoulDisplayMode;
		const auto sourceMode = cfg.sourceDisplayMode;

		std::string playerSuffix;
		if (advanced && (sourceMode == PluginSettings::SoulGemSourceDisplayMode::kPlayerOnly || sourceMode == PluginSettings::SoulGemSourceDisplayMode::kFollowerAndPlayer)) {
			if (const auto* player = RE::PlayerCharacter::GetSingleton(); player && player->GetName()) {
				playerSuffix.reserve(32);
				playerSuffix += "  |  ";
				playerSuffix += player->GetName();
			}
		}

		std::string followerSuffix;
		if (advanced && (sourceMode == PluginSettings::SoulGemSourceDisplayMode::kFollowerOnly || sourceMode == PluginSettings::SoulGemSourceDisplayMode::kFollowerAndPlayer)) {
			if (const auto follower = ::FEC::ContainerMenuUtil::GetAffectedTarget(a_menu); follower && follower->GetName()) {
				followerSuffix.reserve(32);
				followerSuffix += "  |  ";
				followerSuffix += follower->GetName();
			}
		}

		const bool haveChargeInfo = (a_selectedMaxAbs > 0.0);
		const double currentAbs = a_selectedCurrentAbs;
		const double maxAbs = a_selectedMaxAbs;

		for (const auto& opt : a_options) {
			auto* gemForm = RE::TESForm::LookupByID(opt.gemFormID);
			auto* gem = gemForm ? gemForm->As<RE::TESSoulGem>() : nullptr;
			if (!gem) {
				continue;
			}

			double addedPct = 0.0;
			double soulValue = 0.0;
			if (haveChargeInfo) {
				soulValue = GetSoulRechargeValue(opt.soul);
				if (soulValue > 0.0 && maxAbs > 0.0) {
					const double newAbs = std::clamp(currentAbs + soulValue, 0.0, maxAbs);
					addedPct = ((newAbs - currentAbs) / maxAbs) * 100.0;
				}
			}

			// Hide gems that would add no charge.
			if (haveChargeInfo && addedPct <= 0.0) {
				continue;
			}

			entries.push_back(Entry{
				.gem = gem,
				.source = opt.source,
				.count = opt.count,
				.addedPercent = addedPct,
				.soulValue = soulValue,
				.soul = opt.soul,
				.gemFormID = opt.gemFormID,
			});
		}

		// Advanced mode can sort by soul; otherwise keep vanilla-ish name sorting.
		const bool useSoulSorting = (advanced && cfg.soulGemSortMode != PluginSettings::SoulGemSortMode::kName);
		if (!useSoulSorting) {
			// Vanilla-ish name sort.
			auto ciLess = [](std::string_view a, std::string_view b) -> bool {
				auto lower = [](unsigned char c) { return static_cast<unsigned char>(std::tolower(c)); };
				const std::size_t n = (std::min)(a.size(), b.size());
				for (std::size_t i = 0; i < n; ++i) {
					const unsigned char ac = lower(static_cast<unsigned char>(a[i]));
					const unsigned char bc = lower(static_cast<unsigned char>(b[i]));
					if (ac < bc) return true;
					if (ac > bc) return false;
				}
				return a.size() < b.size();
			};

			std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
				const char* an = (a.gem && a.gem->GetName()) ? a.gem->GetName() : "";
				const char* bn = (b.gem && b.gem->GetName()) ? b.gem->GetName() : "";
				const std::string_view asv(an ? an : "");
				const std::string_view bsv(bn ? bn : "");
				if (asv != bsv) {
					return ciLess(asv, bsv);
				}
				if (a.gemFormID != b.gemFormID) {
					return a.gemFormID < b.gemFormID;
				}
				return static_cast<std::uint8_t>(a.source) < static_cast<std::uint8_t>(b.source);
			});
		} else {
			const bool sortAsc = (cfg.soulGemSortMode == PluginSettings::SoulGemSortMode::kSoulAsc);
			std::stable_sort(entries.begin(), entries.end(), [=](const Entry& a, const Entry& b) {
				// Soul sort priority: reusable, contained soul, capacity, black/non-black, source.
				constexpr RE::FormID kReusableSoulGemKeyword = 0x000ED2F1;
				auto isReusable = [&](const Entry& e) -> bool {
					return e.gem && e.gem->HasKeywordID(kReusableSoulGemKeyword);
				};
				auto soulRank = [&](RE::SOUL_LEVEL s) -> int {
					return static_cast<int>(s);
				};

				const bool aReusable = isReusable(a);
				const bool bReusable = isReusable(b);
				if (aReusable != bReusable) {
					return aReusable;
				}

				if (a.soul != b.soul) {
					return sortAsc ? (soulRank(a.soul) < soulRank(b.soul)) : (soulRank(a.soul) > soulRank(b.soul));
				}

				const auto aCap = a.gem ? a.gem->GetMaximumCapacity() : RE::SOUL_LEVEL::kNone;
				const auto bCap = b.gem ? b.gem->GetMaximumCapacity() : RE::SOUL_LEVEL::kNone;
				if (aCap != bCap) {
					return sortAsc ? (soulRank(aCap) < soulRank(bCap)) : (soulRank(aCap) > soulRank(bCap));
				}

				// Black soul gems use the can-hold-NPC-soul flag.
				const bool aBlack = a.gem && a.gem->CanHoldNPCSoul();
				const bool bBlack = b.gem && b.gem->CanHoldNPCSoul();
				if (aBlack != bBlack) {
					return sortAsc ? (!aBlack) : (aBlack);
				}
				if (a.source != b.source) {
					return static_cast<std::uint8_t>(a.source) < static_cast<std::uint8_t>(b.source);
				}

				if (a.gemFormID != b.gemFormID) {
					return a.gemFormID < b.gemFormID;
				}
				const char* an = a.gem ? a.gem->GetName() : "";
				const char* bn = b.gem ? b.gem->GetName() : "";
				return std::string_view(an ? an : "") < std::string_view(bn ? bn : "");
			});
		}

		RE::GFxValue listItems;
		view->CreateArray(std::addressof(listItems));
		std::uint32_t outIndex = 0;
		for (const auto& e : entries) {
			RE::GFxValue entryObj;
			view->CreateObject(std::addressof(entryObj));

			std::string label;
			label.reserve(64);
			label += (e.gem && e.gem->GetName()) ? e.gem->GetName() : "<soul gem>";
			if (advanced && containedSoulMode != PluginSettings::ContainedSoulDisplayMode::kDisable) {
				if (e.gem && e.soul > RE::SOUL_LEVEL::kNone) {
					bool shouldShow = false;
					if (containedSoulMode == PluginSettings::ContainedSoulDisplayMode::kAlways) {
						shouldShow = true;
					} else {
						const auto cap = e.gem->GetMaximumCapacity();
						shouldShow = (cap > e.soul);
					}
					if (shouldShow) {
						if (const char* soulText = SoulLabel(e.soul)) {
							label += " (";
							label += soulText;
							label += ")";
						}
					}
				}
			}
			if (advanced && cfg.showStackCount) {
				if (e.count > 1) {
					label += " (";
					label += std::to_string(e.count);
					label += ")";
				}
			}
			if (advanced) {
				if (e.source == SoulGemSource::kFollower) {
					if (!followerSuffix.empty()) {
						label += followerSuffix;
					}
				} else {
					if (!playerSuffix.empty()) {
						label += playerSuffix;
					}
				}
			}

			RE::GFxValue text;
			view->CreateString(std::addressof(text), label.c_str());
			entryObj.SetMember("text", text);
			entryObj.SetMember("fecFormId", RE::GFxValue(static_cast<double>(e.gemFormID)));
			entryObj.SetMember("fecSource", RE::GFxValue(static_cast<double>(static_cast<std::uint32_t>(e.source))));
			entryObj.SetMember("fecSoul", RE::GFxValue(static_cast<double>(static_cast<std::uint32_t>(e.soul))));
			if (haveChargeInfo) {
				entryObj.SetMember("chargeAdded", RE::GFxValue(e.addedPercent));
			}

			listItems.SetElement(outIndex++, entryObj);
		}

		RE::GFxValue updateObj;
		view->CreateObject(std::addressof(updateObj));
		updateObj.SetMember("type", RE::GFxValue(kICT_LIST));
		updateObj.SetMember("listItems", listItems);
		// ICT_LIST draws the bar from currentCharge; keep charge for code paths that read itemCard.itemInfo.charge.
		updateObj.SetMember("currentCharge", RE::GFxValue(currentChargePct));
		updateObj.SetMember("charge", RE::GFxValue(currentChargePct));

		// Do not patch charge/usedCharge here; the menu is refreshed after apply.

		Pending().onPick = std::move(a_onPick);
		Pending().onCancel = std::move(a_onCancel);

		root.SetMember("__fecWROpen", RE::GFxValue(true));
		itemCard.SetMember("itemInfo", updateObj);

		return true;
	}
}
