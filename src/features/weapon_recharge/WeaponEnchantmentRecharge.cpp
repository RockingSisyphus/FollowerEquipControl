#include "WeaponEnchantmentRecharge.h"

#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "Controls.h"
#include "PluginSettings.h"

#include "WeaponRechargeGfxUtil.h"
#include "WeaponRechargeMenuGate.h"
#include "WeaponRechargePickerUI.h"
#include "WeaponRechargeSelection.h"
#include "WeaponRechargeSoulGems.h"
#include "WeaponRechargeTransaction.h"

#include "Notifications.h"
#include "UISounds.h"

#include <array>

namespace FEC::WeaponEnchantmentRecharge
{
	namespace
	{
		constexpr const char* kAttemptChargeInstalledMember = "__fecWRAttemptChargeInstalled";
		constexpr const char* kAttemptChargeHandlerMember = "__fecWRAttemptCharge";
		constexpr const char* kHandleInputHookedMember = "__fecWRHandleInputHooked";
		constexpr const char* kOriginalHandleInputMember = "__fecWROriginalHandleInput";

		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_postDisplayHandle{ 0 };

			[[nodiscard]] std::int32_t GetConfiguredRechargeKeyDik()
			{
				// Prefer our own setting; do not read SkyUI config.
				const auto cfg = static_cast<std::int32_t>(PluginSettings::Get().weaponEnchantmentRecharge.rechargeKeyDik);
				return (cfg > 0) ? cfg : 20;  // Default: DIK_T
			}

		[[nodiscard]] bool TryBeginRechargeFlow(RE::ContainerMenu* a_menu)
		{
			if (!a_menu || !WeaponRecharge::ShouldAffectMenu(a_menu)) {
				return false;
			}

			auto& root = a_menu->GetRuntimeData().root;
			if (!root.IsObject() || !WeaponRecharge::GfxUtil::IsChargeActionAvailable(root)) {
				return false;
			}

			WeaponRecharge::SelectedChargeInfo selected{};
			if (!WeaponRecharge::TryGetSelectedItemChargeInfo(a_menu, selected)) {
				return false;
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			auto follower = ContainerMenuUtil::GetAffectedTarget(a_menu);
			if (!player || !follower) {
				return false;
			}

			auto options = WeaponRecharge::GatherFilledSoulGems(player, follower.get());
			if (options.empty()) {
				Notifications::ToastKey("feedback.weapon_recharge.no_items_to_charge_enchantment", {}, "weapon_recharge.no_soul_gems");
				UISounds::PlaySoundByFormID(UISounds::SoundFormID::kActivateFail);
				return false;
			}

			(void)WeaponRecharge::PickerUI::Open(
				a_menu,
				options,
				selected.currentAbs,
				selected.maxAbs,
				[selected, player, follower = follower.get()](RE::TESSoulGem* gem, WeaponRecharge::SoulGemSource src, RE::SOUL_LEVEL soul) {
					if (!player || !follower || !gem) {
						return;
					}
					auto res = WeaponRecharge::TryApplyRecharge(selected, player, follower, gem, src, soul);
					if (res.applied) {
						UISounds::PlaySoundByFormID(UISounds::SoundFormID::kEnchantRecharge);
						ContainerMenuUtil::QueueRefreshForOpenContainerMenu();
					}
				},
				[]() {});

			return true;
		}

		[[nodiscard]] RE::ContainerMenu* GetOpenMenuRaw()
		{
			auto menu = ContainerMenuUtil::GetOpenContainerMenu();
			return menu ? menu.get() : nullptr;
		}

		class AttemptChargeItemHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				(void)a_params;
				(void)TryBeginRechargeFlow(GetOpenMenuRaw());
			}
		};

		AttemptChargeItemHandler* GetAttemptChargeItemHandler()
		{
			static AttemptChargeItemHandler* handler = []() {
				auto* h = new AttemptChargeItemHandler();
				h->AddRef();
				return h;
			}();
			return handler;
		}

		void InstallChargeCallback(RE::ContainerMenu* a_menu)
		{
			if (!a_menu || !WeaponRecharge::ShouldAffectMenu(a_menu)) {
				return;
			}

			auto& root = a_menu->GetRuntimeData().root;
			if (!root.IsObject()) {
				return;
			}

			bool already = false;
			if (WeaponRecharge::GfxUtil::TryGetMemberBool(root, kAttemptChargeInstalledMember, already) && already) {
				return;
			}

			auto view = a_menu->uiMovie.get();
			if (!view) {
				return;
			}

			RE::GFxValue handlerFn;
			view->CreateFunction(std::addressof(handlerFn), GetAttemptChargeItemHandler());
			root.SetMember(kAttemptChargeHandlerMember, handlerFn);

			RE::GFxValue methodName;
			view->CreateString(std::addressof(methodName), "AttemptChargeItem");
			RE::GFxValue callbackName;
			view->CreateString(std::addressof(callbackName), kAttemptChargeHandlerMember);

			std::array<RE::GFxValue, 3> args{ methodName, root, callbackName };
			RE::GFxValue ignored;
			if (view->Invoke("gfx.io.GameDelegate.addCallBack", std::addressof(ignored), args.data(), static_cast<std::uint32_t>(args.size()))) {
				root.SetMember(kAttemptChargeInstalledMember, RE::GFxValue(true));
			}
		}

		class HandleInputHookHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				auto callOriginal = [&](Params& params) {
					if (!params.thisPtr || !params.thisPtr->IsObject()) {
						return;
					}
					(void)params.thisPtr->Invoke(kOriginalHandleInputMember, params.retVal, params.args, params.argCount);
				};

				thread_local bool inHook = false;
				if (inHook) {
					callOriginal(a_params);
					return;
				}
				inHook = true;

				// Let SkyUI handle its own input before opening the recharge flow.
				callOriginal(a_params);

				auto* menu = GetOpenMenuRaw();
				if (!menu || !WeaponRecharge::ShouldAffectMenu(menu)) {
					inHook = false;
					return;
				}

				auto& root = menu->GetRuntimeData().root;
				if (!root.IsObject() || !WeaponRecharge::GfxUtil::IsChargeActionAvailable(root)) {
					inHook = false;
					return;
				}
				if (a_params.argCount < 1) {
					inHook = false;
					return;
				}
				const auto& details = a_params.args[0];
				if (!details.IsObject() && !details.IsDisplayObject()) {
					inHook = false;
					return;
				}

				std::string_view value;
				if (WeaponRecharge::GfxUtil::TryGetMemberString(details, "value", value)) {
					if (value != "keyDown" && value != "keyHold") {
						inHook = false;
						return;
					}
				}

				std::string_view control;
				(void)WeaponRecharge::GfxUtil::TryGetMemberString(details, "control", control);
				double skseKeycodeNum = -1.0;
				(void)WeaponRecharge::GfxUtil::TryGetMemberNumber(details, "skseKeycode", skseKeycodeNum);
				const auto skseKeycode = static_cast<std::int32_t>(skseKeycodeNum);

				const auto chargeKeyKb = GetConfiguredRechargeKeyDik();
				const auto chargeKeyGp = Controls::GetGamepadRechargeKey();

				// Keyboard uses the recharge key alone; gamepad requires Mod Key + Recharge Key.
				bool isChargeKey = false;
				if (skseKeycode > 0 && skseKeycode == chargeKeyKb) {
					isChargeKey = true;
				} else if (skseKeycode > 0 && static_cast<std::uint32_t>(skseKeycode) == chargeKeyGp) {
					const bool modHeld = Controls::IsGamepadButtonDown(Controls::GetGamepadModKey());
					logger::trace("RechargeInput: gamepad Y detected, modKey={}", modHeld);
					if (modHeld) {
						isChargeKey = true;
					}
				}
				if (!isChargeKey) {
					inHook = false;
					return;
				}

				logger::debug("RechargeInput: charge key accepted (skse={})", skseKeycode);
				const bool handled = TryBeginRechargeFlow(menu);
				if (handled && a_params.retVal) {
					a_params.retVal->SetBoolean(true);
				}
				inHook = false;
			}
		};

		HandleInputHookHandler* GetHandleInputHookHandler()
		{
			static HandleInputHookHandler* handler = []() {
				auto* h = new HandleInputHookHandler();
				h->AddRef();
				return h;
			}();
			return handler;
		}

		void TryHookRootHandleInput(RE::ContainerMenu* a_menu)
		{
			if (!a_menu || !WeaponRecharge::ShouldAffectMenu(a_menu)) {
				return;
			}

			auto& root = a_menu->GetRuntimeData().root;
			if (!root.IsObject() || !WeaponRecharge::GfxUtil::IsSkyUiPresent(root)) {
				return;
			}

			bool already = false;
			if (WeaponRecharge::GfxUtil::TryGetMemberBool(root, kHandleInputHookedMember, already) && already) {
				return;
			}

			RE::GFxValue orig;
			if (!root.GetMember("handleInput", std::addressof(orig)) || (!orig.IsObject() && !orig.IsDisplayObject())) {
				return;
			}

			auto view = a_menu->uiMovie.get();
			if (!view) {
				return;
			}

			root.SetMember(kOriginalHandleInputMember, orig);
			RE::GFxValue hookFn;
			view->CreateFunction(std::addressof(hookFn), GetHandleInputHookHandler());
			root.SetMember("handleInput", hookFn);
			root.SetMember(kHandleInputHookedMember, RE::GFxValue(true));
		}
	}

	void Install()
	{
		if (g_installed) {
			return;
		}

		WeaponRecharge::PickerUI::EnsureHandlersCreated();
		ContainerMenuDisplayHook::Install();
		ContainerMenuUtil::InstallMenuOpenCloseWatcher();

		g_postDisplayHandle = ContainerMenuDisplayHook::AddPostDisplayListener([](RE::ContainerMenu* a_menu) {
			if (!a_menu || !PluginSettings::Get().weaponEnchantmentRecharge.enableRecharge) {
				return;
			}
			InstallChargeCallback(a_menu);
			TryHookRootHandleInput(a_menu);
		});

		g_installed = true;
		logger::trace("WeaponEnchantmentRecharge: installed");
	}

	void Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_postDisplayHandle != 0) {
			ContainerMenuDisplayHook::UnregisterListener(g_postDisplayHandle);
			g_postDisplayHandle = 0;
		}

		g_installed = false;
		logger::trace("WeaponEnchantmentRecharge: uninstalled");
	}
}
