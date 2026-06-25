#include "DisplayFollowerStatsInTradeMenu.h"

#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "KnownFollowerState.h"

#include "PluginSettings.h"

#include "InventoryUtil.h"
#include <cstdint>

namespace FEC
{
	namespace
	{
		constexpr const char* kOriginalUpdatePlayerInfoMember = "__fecOriginalUpdatePlayerInfo";
		constexpr const char* kHookMarkerUpdatePlayerInfoMember = "__fecFollowerInfoUpdatePlayerInfoHook";
		constexpr const char* kOriginalUpdateItemCardInfoMember = "__fecOriginalUpdateItemCardInfo";
		constexpr const char* kHookMarkerUpdateItemCardInfoMember = "__fecFollowerInfoUpdateItemCardInfoHook";

		constexpr int kSkyUI_ICT_NONE = 0;
		constexpr int kSkyUI_ICT_ARMOR = 1;
		constexpr int kSkyUI_ICT_WEAPON = 2;

		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_postDisplayHandle{ 0 };

		[[nodiscard]] bool TryGetMemberBool(const RE::GFxValue& a_obj, const char* a_name, bool& a_out)
		{
			if (!a_obj.IsObject() && !a_obj.IsDisplayObject()) {
				return false;
			}

			RE::GFxValue v;
			if (!a_obj.GetMember(a_name, &v) || !v.IsBool()) {
				return false;
			}

			a_out = v.GetBool();
			return true;
		}

		[[nodiscard]] bool ShouldOverrideForMenu(RE::ContainerMenu* a_menu, RE::Actor*& a_outFollower)
		{
			a_outFollower = nullptr;
			if (!a_menu) {
				return false;
			}

			auto follower = ContainerMenuUtil::GetAffectedTarget(a_menu);
			if (!follower) {
				return false;
			}

			a_outFollower = follower.get();
			return true;
		}

		[[nodiscard]] bool IsTakeTabActive(RE::GFxValue& a_menuRoot)
		{
			// SkyUI ContainerMenu.as uses inventoryLists.categoryList.activeSegment.
			// Segment 0 is Take; segment 1 is Give.
			if (!a_menuRoot.IsObject() && !a_menuRoot.IsDisplayObject()) {
				return false;
			}

			// Read the underlying state directly instead of invoking ActionScript.
			RE::GFxValue invLists;
			if (a_menuRoot.GetMember("inventoryLists", &invLists) && (invLists.IsObject() || invLists.IsDisplayObject())) {
				RE::GFxValue categoryList;
				if (invLists.GetMember("categoryList", &categoryList) && (categoryList.IsObject() || categoryList.IsDisplayObject())) {
					RE::GFxValue activeSegment;
					if (categoryList.GetMember("activeSegment", &activeSegment) && activeSegment.IsNumber()) {
						return static_cast<int>(activeSegment.GetNumber()) == 0;
					}
				}
			}

			static bool logged = false;
			if (!logged) {
				logger::warn("DisplayFollowerStatsInTradeMenu: could not resolve Take/Give tab state; disabling override for safety");
				logged = true;
			}
			return false;
		}

		[[nodiscard]] double ComputeInventoryWeight(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return 0.0;
			}

			double total = 0.0;
			auto inv = a_actor->GetInventory();
			for (const auto& [obj, pair] : inv) {
				const auto count = pair.first;
				const auto& entryPtr = pair.second;
				auto* entry = entryPtr.get();
				if (!entry || count <= 0) {
					continue;
				}
				const double w = static_cast<double>(entry->GetWeight());
				if (w <= 0.0) {
					continue;
				}
				total += w * static_cast<double>(count);
			}

			return total;
		}

		void PatchUpdateObjForFollower(RE::GFxMovieView* a_view, RE::GFxValue& a_updateObj, RE::Actor* a_follower)
		{
			if (!a_view || !a_follower || (!a_updateObj.IsObject() && !a_updateObj.IsDisplayObject())) {
				return;
			}
			auto* avOwner = a_follower->AsActorValueOwner();
			if (!avOwner) {
				return;
			}

			// Fields consumed by BottomBar.as.

			const double maxEnc = static_cast<double>(avOwner->GetActorValue(RE::ActorValue::kCarryWeight));
			double enc = static_cast<double>(avOwner->GetActorValue(RE::ActorValue::kInventoryWeight));
			// kInventoryWeight can be stale or zero for NPCs in some menu states.
			if (enc <= 0.01 && maxEnc > 0.0) {
				enc = ComputeInventoryWeight(a_follower);
			}

			std::int32_t goldCount = 0;
			if (auto* gold = RE::TESForm::LookupByID<RE::TESBoundObject>(0x0000000F)) {
				goldCount = InventoryUtil::GetTotalCount(a_follower, gold);
			}

			const double health = static_cast<double>(avOwner->GetActorValue(RE::ActorValue::kHealth));
			const double magicka = static_cast<double>(avOwner->GetActorValue(RE::ActorValue::kMagicka));
			const double stamina = static_cast<double>(avOwner->GetActorValue(RE::ActorValue::kStamina));

			const double maxHealth = static_cast<double>(avOwner->GetPermanentActorValue(RE::ActorValue::kHealth));
			const double maxMagicka = static_cast<double>(avOwner->GetPermanentActorValue(RE::ActorValue::kMagicka));
			const double maxStamina = static_cast<double>(avOwner->GetPermanentActorValue(RE::ActorValue::kStamina));
			a_updateObj.SetMember("encumbrance", RE::GFxValue(enc));
			a_updateObj.SetMember("maxEncumbrance", RE::GFxValue(maxEnc));
			a_updateObj.SetMember("gold", RE::GFxValue(static_cast<double>(goldCount)));

			a_updateObj.SetMember("health", RE::GFxValue(health));
			a_updateObj.SetMember("maxHealth", RE::GFxValue(maxHealth));

			a_updateObj.SetMember("magicka", RE::GFxValue(magicka));
			a_updateObj.SetMember("maxMagicka", RE::GFxValue(maxMagicka));

			a_updateObj.SetMember("stamina", RE::GFxValue(stamina));
			a_updateObj.SetMember("maxStamina", RE::GFxValue(maxStamina));
		}

		void SuppressArmorAndDamageInBottomBar(RE::ContainerMenu* a_menu, const RE::GFxValue& a_menuRoot, int a_itemType)
		{
			if (!a_menu || !a_menu->uiMovie) {
				return;
			}
			if (a_itemType != kSkyUI_ICT_ARMOR && a_itemType != kSkyUI_ICT_WEAPON) {
				return;
			}

			RE::GFxValue bottomBar;
			if (!a_menuRoot.GetMember("bottomBar", &bottomBar) || (!bottomBar.IsObject() && !bottomBar.IsDisplayObject())) {
				return;
			}

			RE::GFxValue perItem;
			a_menu->uiMovie->CreateObject(&perItem);
			perItem.SetMember("type", RE::GFxValue(static_cast<double>(kSkyUI_ICT_NONE)));

			RE::GFxValue args[1]{ perItem };
			(void)bottomBar.Invoke("updatePerItemInfo", nullptr, args, 1);
		}

		class UpdatePlayerInfoHookHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				auto callOriginal = [&](Params& params, RE::GFxValue* args, std::uint32_t argCount) {
					static bool loggedFailure = false;
					if (!params.thisPtr || !params.thisPtr->IsObject()) {
						return;
					}
					if (!params.thisPtr->Invoke(kOriginalUpdatePlayerInfoMember, params.retVal, args, argCount)) {
						if (!loggedFailure) {
							logger::warn("DisplayFollowerStatsInTradeMenu: failed to invoke {}", kOriginalUpdatePlayerInfoMember);
							loggedFailure = true;
						}
					}
				};

				thread_local bool inHook = false;
				if (inHook) {
					callOriginal(a_params, a_params.args, a_params.argCount);
					return;
				}
				inHook = true;

				auto menu = ContainerMenuUtil::GetOpenContainerMenu();
				RE::Actor* follower = nullptr;
				const auto mode = PluginSettings::Get().statsDisplay.followerStatsInTradeMenuMode;
				const bool shouldOverride = (mode != PluginSettings::FollowerStatsInTradeMenuMode::kDisable) &&
					menu && a_params.thisPtr && a_params.thisPtr->IsObject() &&
					(mode == PluginSettings::FollowerStatsInTradeMenuMode::kAlways || IsTakeTabActive(*a_params.thisPtr)) &&
					a_params.argCount >= 1 && (a_params.args[0].IsObject() || a_params.args[0].IsDisplayObject()) &&
					ShouldOverrideForMenu(menu.get(), follower);
				if (shouldOverride) {
					PatchUpdateObjForFollower(menu->uiMovie.get(), a_params.args[0], follower);
				}

				callOriginal(a_params, a_params.args, a_params.argCount);

				// SkyUI reads Armor/Damage from _playerInfoObj without an undefined guard.
				// Force ICT_NONE after SkyUI updates to hide those lines for follower trade.
				if (shouldOverride && a_params.thisPtr && a_params.thisPtr->IsObject()) {
					RE::GFxValue itemCard;
					RE::GFxValue itemInfo;
					RE::GFxValue typeVal;
					if (a_params.thisPtr->GetMember("itemCard", &itemCard) && (itemCard.IsObject() || itemCard.IsDisplayObject()) &&
						itemCard.GetMember("itemInfo", &itemInfo) && (itemInfo.IsObject() || itemInfo.IsDisplayObject()) &&
						itemInfo.GetMember("type", &typeVal) && typeVal.IsNumber()) {
						SuppressArmorAndDamageInBottomBar(menu.get(), *a_params.thisPtr, static_cast<int>(typeVal.GetNumber()));
					}
				}
				inHook = false;
			}
		};

		class UpdateItemCardInfoHookHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				auto callOriginal = [&](Params& params, RE::GFxValue* args, std::uint32_t argCount) {
					static bool loggedFailure = false;
					if (!params.thisPtr || !params.thisPtr->IsObject()) {
						return;
					}
					if (!params.thisPtr->Invoke(kOriginalUpdateItemCardInfoMember, params.retVal, args, argCount)) {
						if (!loggedFailure) {
							logger::warn("DisplayFollowerStatsInTradeMenu: failed to invoke {}", kOriginalUpdateItemCardInfoMember);
							loggedFailure = true;
						}
					}
				};

				thread_local bool inHook = false;
				if (inHook) {
					callOriginal(a_params, a_params.args, a_params.argCount);
					return;
				}
				inHook = true;

				callOriginal(a_params, a_params.args, a_params.argCount);

				auto menu = ContainerMenuUtil::GetOpenContainerMenu();
				RE::Actor* follower = nullptr;
				const auto mode = PluginSettings::Get().statsDisplay.followerStatsInTradeMenuMode;
				if ((mode != PluginSettings::FollowerStatsInTradeMenuMode::kDisable) &&
					menu && a_params.thisPtr && a_params.thisPtr->IsObject() &&
					(mode == PluginSettings::FollowerStatsInTradeMenuMode::kAlways || IsTakeTabActive(*a_params.thisPtr)) &&
					a_params.argCount >= 1 && (a_params.args[0].IsObject() || a_params.args[0].IsDisplayObject()) &&
					ShouldOverrideForMenu(menu.get(), follower)) {
					RE::GFxValue typeVal;
					if (a_params.args[0].GetMember("type", &typeVal) && typeVal.IsNumber() && a_params.thisPtr && a_params.thisPtr->IsObject()) {
						SuppressArmorAndDamageInBottomBar(menu.get(), *a_params.thisPtr, static_cast<int>(typeVal.GetNumber()));
					}
				}

				inHook = false;
			}
		};

		UpdatePlayerInfoHookHandler* GetUpdatePlayerInfoHookHandler()
		{
			static UpdatePlayerInfoHookHandler* handler = []() {
				auto* h = new UpdatePlayerInfoHookHandler();
				h->AddRef();
				return h;
			}();
			return handler;
		}

		UpdateItemCardInfoHookHandler* GetUpdateItemCardInfoHookHandler()
		{
			static UpdateItemCardInfoHookHandler* handler = []() {
				auto* h = new UpdateItemCardInfoHookHandler();
				h->AddRef();
				return h;
			}();
			return handler;
		}

		void TryHookUpdatePlayerInfo(RE::ContainerMenu* a_menu)
		{
			if (!a_menu) {
				return;
			}

			auto& root = a_menu->GetRuntimeData().root;
			if (!root.IsObject()) {
				return;
			}

			// Reinstall if another mod overwrote this Scaleform hook.
			RE::GFxValue cur;
			if (!root.GetMember("UpdatePlayerInfo", &cur) || (!cur.IsObject() && !cur.IsDisplayObject())) {
				return;
			}
			bool isOurs = false;
			(void)TryGetMemberBool(cur, kHookMarkerUpdatePlayerInfoMember, isOurs);
			if (isOurs) {
				return;
			}

			auto view = a_menu->uiMovie.get();
			if (!view) {
				return;
			}

			root.SetMember(kOriginalUpdatePlayerInfoMember, cur);

			RE::GFxValue hookFn;
			view->CreateFunction(&hookFn, GetUpdatePlayerInfoHookHandler());
			// Marker used to detect hook replacement.
			hookFn.SetMember(kHookMarkerUpdatePlayerInfoMember, RE::GFxValue(true));
			root.SetMember("UpdatePlayerInfo", hookFn);
			logger::trace("DisplayFollowerStatsInTradeMenu: hooked UpdatePlayerInfo");
		}

		void TryHookUpdateItemCardInfo(RE::ContainerMenu* a_menu)
		{
			if (!a_menu) {
				return;
			}

			auto& root = a_menu->GetRuntimeData().root;
			if (!root.IsObject()) {
				return;
			}

			RE::GFxValue cur;
			if (!root.GetMember("UpdateItemCardInfo", &cur) || (!cur.IsObject() && !cur.IsDisplayObject())) {
				return;
			}
			bool isOurs = false;
			(void)TryGetMemberBool(cur, kHookMarkerUpdateItemCardInfoMember, isOurs);
			if (isOurs) {
				return;
			}

			auto view = a_menu->uiMovie.get();
			if (!view) {
				return;
			}

			root.SetMember(kOriginalUpdateItemCardInfoMember, cur);

			RE::GFxValue hookFn;
			view->CreateFunction(&hookFn, GetUpdateItemCardInfoHookHandler());
			hookFn.SetMember(kHookMarkerUpdateItemCardInfoMember, RE::GFxValue(true));
			root.SetMember("UpdateItemCardInfo", hookFn);
			logger::trace("DisplayFollowerStatsInTradeMenu: hooked UpdateItemCardInfo");
		}
	}

	void DisplayFollowerStatsInTradeMenu::Install()
	{
		if (g_installed) {
			return;
		}

		ContainerMenuDisplayHook::Install();
		ContainerMenuUtil::InstallMenuOpenCloseWatcher();

		g_postDisplayHandle = ContainerMenuDisplayHook::AddPostDisplayListener([](RE::ContainerMenu* a_menu) {
			TryHookUpdatePlayerInfo(a_menu);
			TryHookUpdateItemCardInfo(a_menu);
		});

		g_installed = true;
		logger::trace("DisplayFollowerStatsInTradeMenu: installed");
	}

	void DisplayFollowerStatsInTradeMenu::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_postDisplayHandle != 0) {
			ContainerMenuDisplayHook::UnregisterListener(g_postDisplayHandle);
			g_postDisplayHandle = 0;
		}

		g_installed = false;
		logger::trace("DisplayFollowerStatsInTradeMenu: uninstalled");
	}
}
