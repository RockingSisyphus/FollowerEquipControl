#include "WeaponEnchantmentRechargeUIIndicator.h"

#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "Controls.h"
#include "PluginSettings.h"

#include "WeaponRechargeGfxUtil.h"
#include "WeaponRechargeMenuGate.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace FEC
{
	namespace
	{
		constexpr const char* kNavPanelMember = "navPanel";
		constexpr const char* kUpdateButtonsMember = "updateButtons";
		constexpr const char* kHookedFlagMember = "__fecRechargeUpdateButtonsHooked";
		constexpr const char* kOriginalUpdateButtonsMember = "__fecRechargeOriginalUpdateButtons";
		constexpr std::string_view kChargeText{ "$Charge" };
		constexpr double kDefaultChargeKeyCode = 20.0;  // DIK_T
		constexpr std::int32_t kDeDupSearchDepth = 4;

		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_postDisplayHandle{ 0 };
		ContainerMenuUtil::ListenerHandle g_menuCloseHandle{ 0 };
		std::atomic_bool g_refreshQueued{ false };

		void InjectChargeButton(RE::ContainerMenu* a_menu);
		void TryHookNavPanelUpdateButtons(RE::ContainerMenu* a_menu);

		[[nodiscard]] bool IsIndicatorEnabled()
		{
			const auto& cfg = PluginSettings::Get().weaponEnchantmentRecharge;
			return cfg.enableRecharge && cfg.enableUIIndicator;
		}

		[[nodiscard]] bool IsEligibleMenu(RE::ContainerMenu* a_menu)
		{
			return a_menu && IsIndicatorEnabled() && WeaponRecharge::ShouldAffectMenu(a_menu);
		}

		[[nodiscard]] bool TryGetNavPanel(const RE::GFxValue& a_root, RE::GFxValue& a_out)
		{
			return a_root.GetMember(kNavPanelMember, std::addressof(a_out)) && (a_out.IsObject() || a_out.IsDisplayObject());
		}

		[[nodiscard]] bool HasButtonTextDeep(const RE::GFxValue& a_root, std::string_view a_wanted, std::int32_t a_depth);

		[[nodiscard]] bool HasChargeButtonAlready(const RE::GFxValue& a_navPanel)
		{
			return HasButtonTextDeep(a_navPanel, kChargeText, kDeDupSearchDepth);
		}

		[[nodiscard]] bool TryGetChargeControls(RE::GFxMovieView* a_view, const RE::GFxValue& a_root, RE::GFxValue& a_out)
		{
			if (!a_view) {
				return false;
			}

			double keyCode = kDefaultChargeKeyCode;
			if (WeaponRecharge::GfxUtil::IsSkyUiPcPlatform(a_root)) {
				// Keyboard uses the configured DIK recharge key.
				const auto cfg = static_cast<std::int32_t>(PluginSettings::Get().weaponEnchantmentRecharge.rechargeKeyDik);
				if (cfg > 0) {
					keyCode = static_cast<double>(cfg);
				}
			} else {
				// Gamepad uses the configured recharge key so SkyUI shows the correct icon.
				keyCode = static_cast<double>(Controls::GetGamepadRechargeKey());
			}

			a_view->CreateObject(std::addressof(a_out));
			a_out.SetMember("keyCode", RE::GFxValue(keyCode));
			return true;
		}

		void AddChargeButton(RE::GFxMovieView* a_view, const RE::GFxValue& a_root, RE::GFxValue& a_navPanel)
		{
			RE::GFxValue chargeControls;
			if (!TryGetChargeControls(a_view, a_root, chargeControls)) {
				return;
			}

			RE::GFxValue button;
			a_view->CreateObject(std::addressof(button));
			button.SetMember("text", RE::GFxValue(kChargeText.data()));
			button.SetMember("controls", chargeControls);

			std::array<RE::GFxValue, 1> addArgs{ button };
			(void)a_navPanel.Invoke("addButton", nullptr, addArgs.data(), static_cast<std::uint32_t>(addArgs.size()));
		}

		[[nodiscard]] bool ValueHasButtonText(const RE::GFxValue& a_v, std::string_view a_wanted)
		{
			if (!a_v.IsObject() && !a_v.IsDisplayObject()) {
				return false;
			}
			RE::GFxValue text;
			if (a_v.GetMember("text", std::addressof(text)) && text.IsString()) {
				const auto* s = text.GetString();
				if (s && std::string_view(s) == a_wanted) {
					return true;
				}
			}
			RE::GFxValue label;
			if (a_v.GetMember("label", std::addressof(label)) && label.IsString()) {
				const auto* s = label.GetString();
				if (s && std::string_view(s) == a_wanted) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool HasButtonTextDeep(const RE::GFxValue& a_root, std::string_view a_wanted, std::int32_t a_depth)
		{
			if (a_depth <= 0) {
				return false;
			}

			if (a_root.IsObject() || a_root.IsDisplayObject()) {
				if (ValueHasButtonText(a_root, a_wanted)) {
					return true;
				}

				bool found = false;
				a_root.VisitMembers([&](const char*, const RE::GFxValue& v) {
					if (found) {
						return;
					}
					if (HasButtonTextDeep(v, a_wanted, a_depth - 1)) {
						found = true;
					}
				});
				return found;
			}

			if (a_root.IsArray()) {
				const auto n = a_root.GetArraySize();
				for (std::uint32_t i = 0; i < n; ++i) {
					RE::GFxValue elem;
					if (!a_root.GetElement(i, std::addressof(elem))) {
						continue;
					}
					if (HasButtonTextDeep(elem, a_wanted, a_depth - 1)) {
						return true;
					}
				}
			}

			return false;
		}

		class UpdateButtonsHookHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				auto callOriginal = [&](Params& params) {
					static bool loggedFailure = false;
					if (!params.thisPtr || !params.thisPtr->IsObject()) {
						return;
					}
					if (!params.thisPtr->Invoke(kOriginalUpdateButtonsMember, params.retVal, params.args, params.argCount)) {
						if (!loggedFailure) {
							logger::warn("WeaponEnchantmentRechargeUIIndicator: failed to invoke {}", kOriginalUpdateButtonsMember);
							loggedFailure = true;
						}
					}
				};

				thread_local bool inHook = false;
				if (inHook) {
					callOriginal(a_params);
					return;
				}
				inHook = true;

				// Inject before the original layout pass so the button is ordered correctly.
				auto menu = ContainerMenuUtil::GetOpenContainerMenu();
				if (menu && IsEligibleMenu(menu.get())) {
					InjectChargeButton(menu.get());
				}

				callOriginal(a_params);
				inHook = false;
			}
		};

		UpdateButtonsHookHandler* GetUpdateButtonsHookHandler()
		{
			static UpdateButtonsHookHandler* handler = []() {
				auto* h = new UpdateButtonsHookHandler();
				h->AddRef();
				return h;
			}();
			return handler;
		}

		void TryHookNavPanelUpdateButtons(RE::ContainerMenu* a_menu)
		{
			if (!a_menu) {
				return;
			}

			auto& root = a_menu->GetRuntimeData().root;
			if (!root.IsObject() || !WeaponRecharge::GfxUtil::IsSkyUiPresent(root)) {
				return;
			}

			RE::GFxValue navPanel;
			if (!TryGetNavPanel(root, navPanel)) {
				return;
			}

			bool already = false;
			if (WeaponRecharge::GfxUtil::TryGetMemberBool(navPanel, kHookedFlagMember, already) && already) {
				return;
			}

			RE::GFxValue orig;
			if (!navPanel.GetMember(kUpdateButtonsMember, std::addressof(orig)) || (!orig.IsObject() && !orig.IsDisplayObject())) {
				// Fail open if the SkyUI function cannot be captured.
				return;
			}

			auto view = a_menu->uiMovie.get();
			if (!view) {
				return;
			}

			navPanel.SetMember(kOriginalUpdateButtonsMember, orig);
			RE::GFxValue hookFn;
			view->CreateFunction(std::addressof(hookFn), GetUpdateButtonsHookHandler());
			navPanel.SetMember(kUpdateButtonsMember, hookFn);
			navPanel.SetMember(kHookedFlagMember, RE::GFxValue(true));
			logger::trace("WeaponEnchantmentRechargeUIIndicator: hooked navPanel.updateButtons");
		}

		void InjectChargeButton(RE::ContainerMenu* a_menu)
		{
			if (!IsEligibleMenu(a_menu)) {
				return;
			}

			auto& root = a_menu->GetRuntimeData().root;
			if (!root.IsObject() || !WeaponRecharge::GfxUtil::IsChargeActionAvailable(root)) {
				return;
			}

			RE::GFxValue navPanel;
			if (!TryGetNavPanel(root, navPanel)) {
				return;
			}

			if (HasChargeButtonAlready(navPanel)) {
				return;
			}

			auto view = a_menu->uiMovie.get();
			if (!view) {
				return;
			}

			AddChargeButton(view, root, navPanel);
		}

		void QueueBottomBarRefresh()
		{
			bool expected = false;
			if (!g_refreshQueued.compare_exchange_strong(expected, true)) {
				return;
			}

			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				g_refreshQueued.store(false);
				return;
			}

			taskInterface->AddTask([]() {
				auto menu = ContainerMenuUtil::GetOpenContainerMenu();
				if (!menu || !WeaponRecharge::ShouldAffectMenu(menu.get())) {
					g_refreshQueued.store(false);
					return;
				}

				auto& root = menu->GetRuntimeData().root;
				const bool selected = root.IsObject() && WeaponRecharge::GfxUtil::IsItemSelected(root);
				std::array<RE::GFxValue, 1> args{ RE::GFxValue(selected) };
				(void)root.Invoke("updateBottomBar", args);
				g_refreshQueued.store(false);
			});
		}
	}

	void WeaponEnchantmentRechargeUIIndicator::Install()
	{
		if (g_installed) {
			return;
		}

		ContainerMenuDisplayHook::Install();
		ContainerMenuUtil::InstallMenuOpenCloseWatcher();

		g_postDisplayHandle = ContainerMenuDisplayHook::AddPostDisplayListener([](RE::ContainerMenu* a_menu) {
			if (!a_menu) {
				return;
			}
			TryHookNavPanelUpdateButtons(a_menu);
			// The first real bottom-bar refresh runs updateButtons, which our hook intercepts.
		});

		g_menuCloseHandle = ContainerMenuUtil::AddOnContainerMenuCloseListener([]() {
			g_refreshQueued.store(false);
		});

		g_installed = true;
		logger::trace("WeaponEnchantmentRechargeUIIndicator: installed");
	}

	void WeaponEnchantmentRechargeUIIndicator::Uninstall()
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

		g_refreshQueued.store(false);
		g_installed = false;
		logger::trace("WeaponEnchantmentRechargeUIIndicator: uninstalled");
	}

	void WeaponEnchantmentRechargeUIIndicator::RefreshOpenMenu()
	{
		QueueBottomBarRefresh();
	}
}
