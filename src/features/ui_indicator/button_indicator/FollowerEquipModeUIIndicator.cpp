#include "FollowerEquipModeUIIndicator.h"

#include "ActorScope.h"
#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "Controls.h"
#include "AttemptEquipHook.h"
#include "GamepadEquipHook.h"
#include "KnownFollowerState.h"
#include "Localization.h"
#include "PluginSettings.h"
#include "SkyUiMenuUtil.h"

#include "PCH.h"

#include <RE/B/BSInputDeviceManager.h>
#include <RE/B/ButtonEvent.h>
#include <SKSE/InputMap.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>
#include <string_view>

namespace FEC
{
	namespace
	{
		using namespace FEC::EquipMode::SkyUiMenuUtil;

		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_postDisplayHandle{ 0 };
		ContainerMenuUtil::ListenerHandle g_menuCloseHandle{ 0 };
		std::atomic_bool g_lastModKeyDown{ false };
		std::atomic_bool g_refreshQueued{ false };
		bool g_inputSinkInstalled{ false };
		RE::GPtr<RE::ContainerMenu> g_openMenu;

			void InjectFollowerEquipButton(RE::ContainerMenu* menu, bool callUpdateButtons = true);
			void EnforceEquipModeModKeyAdjacency(RE::ContainerMenu* menu);
		void QueueBottomBarRefresh();
		void InstallInputSink();
		void UninstallInputSink();
			void OverrideSkyUiEquipModeButtonText(RE::ContainerMenu* menu, bool callUpdateButtons = true);

		class ModKeyInputSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static ModKeyInputSink* GetSingleton()
			{
				static ModKeyInputSink s;
				return std::addressof(s);
			}

			RE::BSEventNotifyControl ProcessEvent(
				RE::InputEvent* const* a_event,
				RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto menu = ContainerMenuUtil::GetOpenContainerMenu();
				if (!menu || !ShouldAffectMenu(menu.get())) {
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto modKeyKb = Controls::GetModKeyDik();
				const auto modKeyGp = Controls::GetGamepadModKey();
				for (auto ev = *a_event; ev; ev = ev->next) {
					auto* btn = ev->AsButtonEvent();
					if (!btn) {
						continue;
					}
					const auto device = btn->GetDevice();
					const auto code = btn->GetIDCode();
					bool isModKey = false;
					if (device == RE::INPUT_DEVICE::kKeyboard && code == modKeyKb) {
						isModKey = true;
					} else if (device == RE::INPUT_DEVICE::kGamepad) {
						const auto skseKey = SKSE::InputMap::GamepadMaskToKeycode(code);
						if (skseKey == modKeyGp) {
							isModKey = true;
						}
					}
					if (!isModKey) {
						continue;
					}

					if (btn->IsDown()) {
						logger::trace("ModKeyInputSink: mod key DOWN (device={})", device == RE::INPUT_DEVICE::kGamepad ? "gamepad" : "keyboard");
						bool expected = false;
						if (g_lastModKeyDown.compare_exchange_strong(expected, true)) {
							QueueBottomBarRefresh();
						}
					} else if (btn->IsUp()) {
						logger::trace("ModKeyInputSink: mod key UP (device={})", device == RE::INPUT_DEVICE::kGamepad ? "gamepad" : "keyboard");
						bool expected = true;
						if (g_lastModKeyDown.compare_exchange_strong(expected, false)) {
							QueueBottomBarRefresh();
						}
					}
				}

				return RE::BSEventNotifyControl::kContinue;
			}
		};

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
					if (!params.thisPtr->Invoke("__fecOriginalUpdateButtons", params.retVal, params.args, params.argCount)) {
						if (!loggedFailure) {
							logger::warn("FollowerEquipModeUIIndicator: failed to invoke __fecOriginalUpdateButtons");
							loggedFailure = true;
						}
					}
				};

				// Prevent recursion if our injection refreshes the bottom bar.
				thread_local bool inHook = false;
				if (inHook) {
					callOriginal(a_params);
					return;
				}
				inHook = true;

				// During menu teardown, navPanel may be dangling; do not call back into GFx.
				auto menu = ContainerMenuUtil::GetOpenContainerMenu();
				if (!menu || !menu->uiMovie.get()) {
					inHook = false;
					return;
				}
				const bool enabled = PluginSettings::Get().buttonIndicators.enableModKeyIndicator;
				if (enabled && menu && Controls::IsModKeyDown() && ShouldAffectMenu(menu.get())) {
					auto& root = menu->GetRuntimeData().root;
					const bool selected = root.IsObject() && IsItemSelected(root);
					// Keyboard needs temporary _bEquipMode spoofing; gamepad keeps it true.
					if (selected && IsSkyUiPcPlatform(root) && !IsPlayerEquipModeActive(root)) {
						bool oldEquipMode = false;
						if (TryGetMemberBool(root, "_bEquipMode", oldEquipMode)) {
							root.SetMember("_bEquipMode", RE::GFxValue(true));
							std::array<RE::GFxValue, 1> args{ RE::GFxValue(selected) };
							(void)root.Invoke("updateBottomBar", args);
							root.SetMember("_bEquipMode", RE::GFxValue(oldEquipMode));
						}
					}
				}

				// Apply our button changes before the original updateButtons lays out visuals.
				if (enabled && menu) {
					OverrideSkyUiEquipModeButtonText(menu.get(), false);
					InjectFollowerEquipButton(menu.get(), false);
					EnforceEquipModeModKeyAdjacency(menu.get());
				}

				callOriginal(a_params);

				inHook = false;
			}
		};

		class AddButtonHookHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				auto callOriginal = [&](Params& params) {
					static bool loggedFailure = false;
					if (!params.thisPtr || !params.thisPtr->IsObject()) {
						return;
					}
					if (!params.thisPtr->Invoke("__fecOriginalAddButton", params.retVal, params.args, params.argCount)) {
						if (!loggedFailure) {
							logger::warn("FollowerEquipModeUIIndicator: failed to invoke __fecOriginalAddButton");
							loggedFailure = true;
						}
					}
				};

				// Our own injection calls addButton.
				thread_local bool inHook = false;
				if (inHook) {
					callOriginal(a_params);
					return;
				}
				inHook = true;

				// During menu teardown, navPanel may be dangling; do not call back into GFx.
				{
					auto menuCheck = ContainerMenuUtil::GetOpenContainerMenu();
					if (!menuCheck || !menuCheck->uiMovie.get()) {
						inHook = false;
						return;
					}
				}

				callOriginal(a_params);

				// Keep Equip Mode and Mod Key adjacent even when other mods add indicators.
				auto menu = ContainerMenuUtil::GetOpenContainerMenu();
				if (menu && PluginSettings::Get().buttonIndicators.enableModKeyIndicator) {
					EnforceEquipModeModKeyAdjacency(menu.get());
				}

				inHook = false;
			}
		};

		class DoUpdateButtonsHookHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				auto callOriginal = [&](Params& params) {
					static bool loggedFailure = false;
					if (!params.thisPtr || !params.thisPtr->IsObject()) {
						return;
					}
					if (!params.thisPtr->Invoke("__fecOriginalDoUpdateButtons", params.retVal, params.args, params.argCount)) {
						if (!loggedFailure) {
							logger::warn("FollowerEquipModeUIIndicator: failed to invoke __fecOriginalDoUpdateButtons");
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

				// During menu teardown, navPanel may be dangling; do not call back into GFx.
				// Reorder during layout so other button injections cannot split the pair.
				auto menu = ContainerMenuUtil::GetOpenContainerMenu();
				if (!menu || !menu->uiMovie.get()) {
					inHook = false;
					return;
				}
				if (menu && PluginSettings::Get().buttonIndicators.enableModKeyIndicator) {
					EnforceEquipModeModKeyAdjacency(menu.get());
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

		DoUpdateButtonsHookHandler* GetDoUpdateButtonsHookHandler()
		{
			static DoUpdateButtonsHookHandler* handler = []() {
				auto* h = new DoUpdateButtonsHookHandler();
				h->AddRef();
				return h;
			}();
			return handler;
		}

		AddButtonHookHandler* GetAddButtonHookHandler()
		{
			static AddButtonHookHandler* handler = []() {
				auto* h = new AddButtonHookHandler();
				h->AddRef();
				return h;
			}();
			return handler;
		}

		void QueueBottomBarRefresh()
		{
			// Coalesce input edges into one next-frame refresh.
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
				if (!menu || !menu->uiMovie.get()) {
					g_refreshQueued.store(false);
					return;
				}
				if (!ShouldAffectMenu(menu.get())) {
					g_refreshQueued.store(false);
					return;
				}

				auto& root = menu->GetRuntimeData().root;
				const bool selected = IsItemSelected(root);

				// Match SkyUI Equip Mode visuals without changing click semantics.
				// Gamepad keeps _bEquipMode true, so no spoofing is needed.
				bool didSpoof = false;
				if (Controls::IsModKeyDown() && root.IsObject() && selected) {
					if (IsSkyUiPcPlatform(root) && !IsPlayerEquipModeActive(root)) {
						bool oldEquipMode = false;
						if (TryGetMemberBool(root, "_bEquipMode", oldEquipMode)) {
							root.SetMember("_bEquipMode", RE::GFxValue(true));
							didSpoof = true;
							std::array<RE::GFxValue, 1> args{ RE::GFxValue(selected) };
							(void)root.Invoke("updateBottomBar", args);
							root.SetMember("_bEquipMode", RE::GFxValue(oldEquipMode));
						}
					}
				}
				if (!didSpoof) {
					std::array<RE::GFxValue, 1> args{ RE::GFxValue(selected) };
					(void)root.Invoke("updateBottomBar", args);
				}
				g_refreshQueued.store(false);
			});
		}

		void InstallInputSink()
		{
			if (g_inputSinkInstalled) {
				return;
			}
			auto* input = RE::BSInputDeviceManager::GetSingleton();
			if (!input) {
				return;
			}
			input->AddEventSink(ModKeyInputSink::GetSingleton());
			g_inputSinkInstalled = true;
			logger::trace("FollowerEquipModeUIIndicator: installed input sink");
		}

		void UninstallInputSink()
		{
			if (!g_inputSinkInstalled) {
				return;
			}
			auto* input = RE::BSInputDeviceManager::GetSingleton();
			if (input) {
				input->RemoveEventSink(ModKeyInputSink::GetSingleton());
			}
			g_inputSinkInstalled = false;
			logger::trace("FollowerEquipModeUIIndicator: uninstalled input sink");
		}

		[[nodiscard]] bool TryGetNavPanel(const RE::GFxValue& root, RE::GFxValue& out)
		{
			RE::GFxValue nav;
			if (!root.GetMember("navPanel", &nav) || !nav.IsObject()) {
				return false;
			}
			out = nav;
			return true;
		}

		[[nodiscard]] bool TryGetNavPanelButtonsArray(const RE::GFxValue& navPanel, RE::GFxValue& out)
		{
			RE::GFxValue buttons;
			if (navPanel.GetMember("buttons", &buttons) && buttons.IsArray()) {
				out = buttons;
				return true;
			}
			if (navPanel.GetMember("_buttons", &buttons) && buttons.IsArray()) {
				out = buttons;
				return true;
			}
			return false;
		}

		[[nodiscard]] bool ValueHasButtonText(const RE::GFxValue& v, std::string_view wanted)
		{
			RE::GFxValue text;
			if (v.GetMember("text", &text) && text.IsString()) {
				const auto* s = text.GetString();
				if (s && std::string_view(s) == wanted) {
					return true;
				}
			}
			RE::GFxValue label;
			if (v.GetMember("label", &label) && label.IsString()) {
				const auto* s = label.GetString();
				if (s && std::string_view(s) == wanted) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool HasButtonTextDeep(const RE::GFxValue& root, std::string_view wanted, std::int32_t depth)
		{
			if (depth <= 0) {
				return false;
			}

			if (root.IsObject() || root.IsDisplayObject()) {
				if (ValueHasButtonText(root, wanted)) {
					return true;
				}

				bool found = false;
				root.VisitMembers([&](const char*, const RE::GFxValue& v) {
					if (found) {
						return;
					}
					if (HasButtonTextDeep(v, wanted, depth - 1)) {
						found = true;
					}
				});
				return found;
			}

			if (root.IsArray()) {
				const auto n = root.GetArraySize();
				for (std::uint32_t i = 0; i < n; ++i) {
					RE::GFxValue elem;
					if (!root.GetElement(i, &elem)) {
						continue;
					}
					if (HasButtonTextDeep(elem, wanted, depth - 1)) {
						return true;
					}
				}
			}

			return false;
		}

		[[nodiscard]] bool ValueHasControlsKeyCode(const RE::GFxValue& v, double wanted)
		{
			RE::GFxValue controls;
			if (!v.GetMember("controls", &controls) || !controls.IsObject()) {
				return false;
			}
			RE::GFxValue keyCode;
			if (!controls.GetMember("keyCode", &keyCode) || !keyCode.IsNumber()) {
				return false;
			}
			return keyCode.GetNumber() == wanted;
		}

		[[nodiscard]] bool TryFindButtonByControlsKeyCodeDeep(const RE::GFxValue& root, double wanted, std::int32_t depth, RE::GFxValue& out)
		{
			if (depth <= 0) {
				return false;
			}

			if (root.IsObject() || root.IsDisplayObject()) {
				if (ValueHasControlsKeyCode(root, wanted)) {
					out = root;
					return true;
				}

				bool found = false;
				root.VisitMembers([&](const char*, const RE::GFxValue& v) {
					if (found) {
						return;
					}
					RE::GFxValue inner;
					if (TryFindButtonByControlsKeyCodeDeep(v, wanted, depth - 1, inner)) {
						out = inner;
						found = true;
					}
				});
				return found;
			}

			if (root.IsArray()) {
				const auto n = root.GetArraySize();
				for (std::uint32_t i = 0; i < n; ++i) {
					RE::GFxValue elem;
					if (!root.GetElement(i, &elem)) {
						continue;
					}
					if (TryFindButtonByControlsKeyCodeDeep(elem, wanted, depth - 1, out)) {
						return true;
					}
				}
			}

			return false;
		}

		[[nodiscard]] bool TryFindButtonByTextDeep(const RE::GFxValue& root, std::string_view wanted, std::int32_t depth, RE::GFxValue& out)
		{
			if (depth <= 0) {
				return false;
			}

			if (root.IsObject() || root.IsDisplayObject()) {
				if (ValueHasButtonText(root, wanted)) {
					out = root;
					return true;
				}

				bool found = false;
				root.VisitMembers([&](const char*, const RE::GFxValue& v) {
					if (found) {
						return;
					}
					RE::GFxValue inner;
					if (TryFindButtonByTextDeep(v, wanted, depth - 1, inner)) {
						out = inner;
						found = true;
					}
				});
				return found;
			}

			if (root.IsArray()) {
				const auto n = root.GetArraySize();
				for (std::uint32_t i = 0; i < n; ++i) {
					RE::GFxValue elem;
					if (!root.GetElement(i, &elem)) {
						continue;
					}
					if (TryFindButtonByTextDeep(elem, wanted, depth - 1, out)) {
						return true;
					}
				}
			}

			return false;
		}

		void OverrideSkyUiEquipModeButtonText(RE::ContainerMenu* menu, bool callUpdateButtons)
		{
			if (!menu) {
				return;
			}
			const auto& overrideText = PluginSettings::Get().buttonIndicators.equipModeTextOverride;
			const char* effectiveOverrideText = overrideText.empty()
				? Localization::CStr("ui.controls.equip_mode_text_override.default")
				: overrideText.c_str();

			auto& root = menu->GetRuntimeData().root;
			if (!root.IsObject() || !IsSkyUiPcPlatform(root)) {
				return;
			}

			RE::GFxValue navPanel;
			if (!TryGetNavPanel(root, navPanel)) {
				return;
			}

			RE::GFxValue equipModeButton;

			// Prefer keyCode so localized SkyUI text still matches.
			double equipModeKey = 0.0;
			if (TryGetMemberNumber(root, "_equipModeKey", equipModeKey)) {
				(void)TryFindButtonByControlsKeyCodeDeep(navPanel, equipModeKey, 8, equipModeButton);
			}

			// Fallback to text when keyCode is unavailable or unusual.
			if (!equipModeButton.IsObject() && !equipModeButton.IsDisplayObject()) {
				if (!TryFindButtonByTextDeep(navPanel, "$Equip Mode", 8, equipModeButton)) {
					(void)TryFindButtonByTextDeep(navPanel, "Equip Mode", 8, equipModeButton);
				}
			}
			if (!equipModeButton.IsObject() && !equipModeButton.IsDisplayObject()) {
				return;
			}

			equipModeButton.SetMember("text", RE::GFxValue(effectiveOverrideText));
			equipModeButton.SetMember("label", RE::GFxValue(effectiveOverrideText));

			if (callUpdateButtons) {
				std::array<RE::GFxValue, 1> updateArgs{ RE::GFxValue(true) };
				navPanel.Invoke("updateButtons", updateArgs);
			}
		}

		void TryHookNavPanelUpdateButtons(RE::ContainerMenu* menu)
		{
			if (!menu) {
				return;
			}

			auto& root = menu->GetRuntimeData().root;
			if (!root.IsObject()) {
				return;
			}
			if (!IsSkyUiPresent(root)) {
				return;
			}

			RE::GFxValue navPanel;
			if (!TryGetNavPanel(root, navPanel)) {
				return;
			}

			// Store the hook marker on navPanel. If another hook overwrote ours,
			// re-hooking can create a circular call chain and stack overflow.
			bool alreadyHooked = false;
			if (TryGetMemberBool(navPanel, "__fecEquipModeUpdateButtonsInstalled", alreadyHooked) && alreadyHooked) {
				return;
			}

			RE::GFxValue cur;
			if (!navPanel.GetMember("updateButtons", &cur) || (!cur.IsObject() && !cur.IsDisplayObject())) {
				return;
			}

			auto view = menu->uiMovie.get();
			if (!view) {
				return;
			}

			navPanel.SetMember("__fecOriginalUpdateButtons", cur);

			RE::GFxValue hookFn;
			view->CreateFunction(&hookFn, GetUpdateButtonsHookHandler());
			navPanel.SetMember("updateButtons", hookFn);
			navPanel.SetMember("__fecEquipModeUpdateButtonsInstalled", RE::GFxValue(true));
			logger::trace("FollowerEquipModeUIIndicator: hooked navPanel.updateButtons");
		}

		void TryHookNavPanelDoUpdateButtons(RE::ContainerMenu* menu)
		{
			if (!menu) {
				return;
			}

			auto& root = menu->GetRuntimeData().root;
			if (!root.IsObject() || !IsSkyUiPresent(root)) {
				return;
			}

			RE::GFxValue navPanel;
			if (!TryGetNavPanel(root, navPanel)) {
				return;
			}

			bool alreadyHooked = false;
			if (TryGetMemberBool(navPanel, "__fecEquipModeDoUpdateButtonsInstalled", alreadyHooked) && alreadyHooked) {
				return;
			}

			RE::GFxValue cur;
			if (!navPanel.GetMember("doUpdateButtons", &cur) || (!cur.IsObject() && !cur.IsDisplayObject())) {
				// Some SkyUI forks do not expose doUpdateButtons.
				return;
			}

			auto view = menu->uiMovie.get();
			if (!view) {
				return;
			}

			navPanel.SetMember("__fecOriginalDoUpdateButtons", cur);

			RE::GFxValue hookFn;
			view->CreateFunction(&hookFn, GetDoUpdateButtonsHookHandler());
			navPanel.SetMember("doUpdateButtons", hookFn);
			navPanel.SetMember("__fecEquipModeDoUpdateButtonsInstalled", RE::GFxValue(true));
			logger::trace("FollowerEquipModeUIIndicator: hooked navPanel.doUpdateButtons");
		}

		void TryHookNavPanelAddButton(RE::ContainerMenu* menu)
		{
			if (!menu) {
				return;
			}

			auto& root = menu->GetRuntimeData().root;
			if (!root.IsObject() || !IsSkyUiPresent(root)) {
				return;
			}

			RE::GFxValue navPanel;
			if (!TryGetNavPanel(root, navPanel)) {
				return;
			}

			bool alreadyHooked = false;
			if (TryGetMemberBool(navPanel, "__fecEquipModeAddButtonInstalled", alreadyHooked) && alreadyHooked) {
				return;
			}

			RE::GFxValue cur;
			if (!navPanel.GetMember("addButton", &cur) || (!cur.IsObject() && !cur.IsDisplayObject())) {
				return;
			}

			auto view = menu->uiMovie.get();
			if (!view) {
				return;
			}

			navPanel.SetMember("__fecOriginalAddButton", cur);

			RE::GFxValue hookFn;
			view->CreateFunction(&hookFn, GetAddButtonHookHandler());
			navPanel.SetMember("addButton", hookFn);
			navPanel.SetMember("__fecEquipModeAddButtonInstalled", RE::GFxValue(true));
			logger::trace("FollowerEquipModeUIIndicator: hooked navPanel.addButton");
		}

		void InjectFollowerEquipButton(RE::ContainerMenu* menu, bool callUpdateButtons)
		{
			if (!menu) {
				return;
			}

			auto target = ContainerMenuUtil::GetAffectedTarget(menu);
			if (!target) {
				// Corpse Equip Mode can show the indicator for dead actors.
				if (PluginSettings::Get().corpseEquipMode.enable) {
					auto corpse = ContainerMenuUtil::ResolveActorHandle(menu->GetTargetRefHandle());
					if (!corpse || corpse->IsPlayerRef() || !corpse->IsDead()) {
						return;
					}
					// kNone corpses cannot hold items.
					if (!ActorScope::HandEquipAllowed(corpse.get())) {
						return;
					}
				} else {
					return;
				}
			}

			auto& root = menu->GetRuntimeData().root;
			if (!root.IsObject()) {
				return;
			}
			if (!IsSkyUiPresent(root)) {
				return;
			}
			if (Controls::IsModKeyDown()) {
				// While the mod key is held, Equip Mode visuals already represent this state.
				return;
			}
			// Keyboard can use SkyUI's own Equip Mode; gamepad keeps _bEquipMode true.
			if (IsSkyUiPcPlatform(root) && IsPlayerEquipModeActive(root)) {
				return;
			}
			if (!IsItemSelected(root)) {
				return;
			}

			RE::GFxValue navPanel;
			if (!TryGetNavPanel(root, navPanel)) {
				return;
			}

			auto indicatorTextCStr = [&target]() -> const char* {
				if (!target) {
					const auto& s = PluginSettings::Get().corpseEquipMode.indicatorText;
					return s.empty() ? Localization::CStr("ui.corpse_equip.indicator_text.default") : s.c_str();
				}
				// Actor categories can use separate configurable labels.
				const auto category = ActorScope::ResolveEquipPolicy(target.get()).category;
				if (category == ActorScope::ActorCategory::kCommanded) {
					const auto& s = PluginSettings::Get().buttonIndicators.summonIndicatorText;
					return s.empty() ? Localization::CStr("ui.controls.summon_indicator_text.default") : s.c_str();
				}
				if (category == ActorScope::ActorCategory::kInclusion) {
					const auto& s = PluginSettings::Get().buttonIndicators.inclusionIndicatorText;
					return s.empty() ? Localization::CStr("ui.controls.inclusion_indicator_text.default") : s.c_str();
				}
				const auto& s = PluginSettings::Get().buttonIndicators.modKeyIndicatorText;
				return s.empty() ? Localization::CStr("ui.controls.indicator_text.default") : s.c_str();
			};

			// De-dup across SkyUI forks with a shallow-bounded object graph scan.
			if (HasButtonTextDeep(navPanel, std::string_view(indicatorTextCStr()), 4)) {
				return;
			}

			auto view = menu->uiMovie.get();
			if (!view) {
				return;
			}

			RE::GFxValue controls;
			view->CreateObject(&controls);
			// Use the SKSE gamepad keycode so SkyUI shows the correct button icon.
			const double keyCode = IsSkyUiPcPlatform(root)
				? static_cast<double>(Controls::GetModKeyDik())
				: static_cast<double>(Controls::GetGamepadModKey());
			controls.SetMember("keyCode", RE::GFxValue(keyCode));

			RE::GFxValue button;
			view->CreateObject(&button);
			button.SetMember("text", RE::GFxValue(indicatorTextCStr()));
			button.SetMember("controls", controls);
			button.SetMember("__fecModKeyIndicator", RE::GFxValue(true));

			std::array<RE::GFxValue, 1> addArgs{ button };
			if (!navPanel.Invoke("addButton", addArgs)) {
				return;
			}

			if (callUpdateButtons) {
				std::array<RE::GFxValue, 1> updateArgs{ RE::GFxValue(true) };
				navPanel.Invoke("updateButtons", updateArgs);
			}
		}

		void EnforceEquipModeModKeyAdjacency(RE::ContainerMenu* menu)
		{
			if (!menu || !ShouldAffectMenu(menu)) {
				return;
			}

			auto& root = menu->GetRuntimeData().root;
			if (!root.IsObject()) {
				return;
			}

			RE::GFxValue navPanel;
			if (!TryGetNavPanel(root, navPanel)) {
				return;
			}

			RE::GFxValue buttons;
			if (!TryGetNavPanelButtonsArray(navPanel, buttons)) {
				return;
			}

			const auto wantedModKey = IsSkyUiPcPlatform(root)
				? static_cast<double>(Controls::GetModKeyDik())
				: static_cast<double>(Controls::GetGamepadModKey());
			double equipModeKey = 0.0;
			const bool haveEquipModeKey = TryGetMemberNumber(root, "_equipModeKey", equipModeKey);

			auto resolveIndicatorText = [menu]() -> const char* {
				auto liveTarget = ContainerMenuUtil::GetAffectedTarget(menu);
				if (!liveTarget) {
					const auto& s = PluginSettings::Get().corpseEquipMode.indicatorText;
					return s.empty() ? Localization::CStr("ui.corpse_equip.indicator_text.default") : s.c_str();
				}
				const auto category = ActorScope::ResolveEquipPolicy(liveTarget.get()).category;
				if (category == ActorScope::ActorCategory::kCommanded) {
					const auto& s = PluginSettings::Get().buttonIndicators.summonIndicatorText;
					return s.empty() ? Localization::CStr("ui.controls.summon_indicator_text.default") : s.c_str();
				}
				if (category == ActorScope::ActorCategory::kInclusion) {
					const auto& s = PluginSettings::Get().buttonIndicators.inclusionIndicatorText;
					return s.empty() ? Localization::CStr("ui.controls.inclusion_indicator_text.default") : s.c_str();
				}
				const auto& s = PluginSettings::Get().buttonIndicators.modKeyIndicatorText;
				return s.empty() ? Localization::CStr("ui.controls.indicator_text.default") : s.c_str();
			};

			const char* wantedModKeyText = resolveIndicatorText();
			const auto& equipModeOverrideSetting = PluginSettings::Get().buttonIndicators.equipModeTextOverride;
			const std::string_view effectiveOverrideText = equipModeOverrideSetting.empty()
				? std::string_view(Localization::CStr("ui.controls.equip_mode_text_override.default"))
				: std::string_view(equipModeOverrideSetting);

			const auto n = buttons.GetArraySize();
			if (n < 2) {
				return;
			}

			std::vector<RE::GFxValue> elems;
			elems.reserve(n);
			for (std::uint32_t i = 0; i < n; ++i) {
				RE::GFxValue elem;
				if (!buttons.GetElement(i, &elem)) {
					continue;
				}
				elems.push_back(elem);
			}

			auto isEquipModeElem = [&](const RE::GFxValue& v) -> bool {
				if (haveEquipModeKey && ValueHasControlsKeyCode(v, equipModeKey)) {
					return true;
				}
				if (!effectiveOverrideText.empty() && ValueHasButtonText(v, effectiveOverrideText)) {
					return true;
				}
				return ValueHasButtonText(v, "$Equip Mode") || ValueHasButtonText(v, "Equip Mode");
			};

			auto isModKeyElem = [&](const RE::GFxValue& v) -> bool {
				bool tagged = false;
				if (TryGetMemberBool(v, "__fecModKeyIndicator", tagged) && tagged) {
					return true;
				}
				if (!ValueHasControlsKeyCode(v, wantedModKey)) {
					return false;
				}
				return ValueHasButtonText(v, wantedModKeyText);
			};

			std::int32_t equipIndex = -1;
			std::int32_t modIndex = -1;
			for (std::uint32_t i = 0; i < n; ++i) {
				if (equipIndex < 0 && isEquipModeElem(elems[i])) {
					equipIndex = static_cast<std::int32_t>(i);
				}
				if (modIndex < 0 && isModKeyElem(elems[i])) {
					modIndex = static_cast<std::int32_t>(i);
				}
				if (equipIndex >= 0 && modIndex >= 0) {
					break;
				}
			}

			if (equipIndex < 0 || modIndex < 0) {
				return;
			}
			if (modIndex == equipIndex + 1) {
				return;
			}

			RE::GFxValue modElem = elems[static_cast<std::uint32_t>(modIndex)];
			elems.erase(elems.begin() + modIndex);
			const std::int32_t equipAfterErase = (modIndex < equipIndex) ? (equipIndex - 1) : equipIndex;
			std::int32_t insertPos = equipAfterErase + 1;
			if (insertPos < 0) {
				insertPos = 0;
			}
			if (insertPos > static_cast<std::int32_t>(elems.size())) {
				insertPos = static_cast<std::int32_t>(elems.size());
			}
			elems.insert(elems.begin() + insertPos, modElem);

			for (std::uint32_t i = 0; i < n; ++i) {
				buttons.SetElement(i, elems[i]);
			}
		}
	}

	void TryUnhookNavPanel(RE::ContainerMenu* menu)
	{
		if (!menu) {
			return;
		}
		auto& root = menu->GetRuntimeData().root;
		if (!root.IsObject()) {
			return;
		}
		RE::GFxValue navPanel;
		if (!TryGetNavPanel(root, navPanel)) {
			return;
		}

		bool installed = false;
		if (TryGetMemberBool(navPanel, "__fecEquipModeUpdateButtonsInstalled", installed) && installed) {
			RE::GFxValue original;
			if (navPanel.GetMember("__fecOriginalUpdateButtons", &original)) {
				navPanel.SetMember("updateButtons", original);
			}
			navPanel.SetMember("__fecEquipModeUpdateButtonsInstalled", RE::GFxValue(false));
			logger::trace("FollowerEquipModeUIIndicator: unhooked navPanel.updateButtons");
		}

		installed = false;
		if (TryGetMemberBool(navPanel, "__fecEquipModeDoUpdateButtonsInstalled", installed) && installed) {
			RE::GFxValue original;
			if (navPanel.GetMember("__fecOriginalDoUpdateButtons", &original)) {
				navPanel.SetMember("doUpdateButtons", original);
			}
			navPanel.SetMember("__fecEquipModeDoUpdateButtonsInstalled", RE::GFxValue(false));
			logger::trace("FollowerEquipModeUIIndicator: unhooked navPanel.doUpdateButtons");
		}

		installed = false;
		if (TryGetMemberBool(navPanel, "__fecEquipModeAddButtonInstalled", installed) && installed) {
			RE::GFxValue original;
			if (navPanel.GetMember("__fecOriginalAddButton", &original)) {
				navPanel.SetMember("addButton", original);
			}
			navPanel.SetMember("__fecEquipModeAddButtonInstalled", RE::GFxValue(false));
			logger::trace("FollowerEquipModeUIIndicator: unhooked navPanel.addButton");
		}
	}

	void FollowerEquipModeUIIndicator::Install()
	{
		if (g_installed) {
			return;
		}

		ContainerMenuDisplayHook::Install();
		ContainerMenuUtil::InstallMenuOpenCloseWatcher();

		g_postDisplayHandle = ContainerMenuDisplayHook::AddPostDisplayListener([](RE::ContainerMenu* menu) {
			g_openMenu = RE::GPtr<RE::ContainerMenu>(menu);
			InstallInputSink();
			TryHookNavPanelUpdateButtons(menu);
			TryHookNavPanelDoUpdateButtons(menu);
			TryHookNavPanelAddButton(menu);
			EquipMode::Core::GamepadEquipHook::TryInstallHooks(menu);
			EquipMode::Core::AttemptEquipHook::TryInstallHook(menu);
			if (PluginSettings::Get().buttonIndicators.enableModKeyIndicator) {
				OverrideSkyUiEquipModeButtonText(menu);
				InjectFollowerEquipButton(menu);
			}
		});

		g_menuCloseHandle = ContainerMenuUtil::AddOnContainerMenuCloseListener([]() {
			// Restore GFx hooks before GFxMovieRoot::dtor_impl runs.
			// GetOpenContainerMenu() is null while closing, so the cached GPtr keeps the menu alive.
			RE::GPtr<RE::ContainerMenu> menu = g_openMenu;
			if (menu) {
				g_openMenu = nullptr;

				TryUnhookNavPanel(menu.get());
				EquipMode::Core::AttemptEquipHook::TryUninstallHook(menu.get());
				EquipMode::Core::GamepadEquipHook::TryUninstallHooks(menu.get());

				if (auto* taskInterface = SKSE::GetTaskInterface()) {
					// Release the cached menu after the close callback returns.
					taskInterface->AddTask([menu]() mutable {
						menu = nullptr;
					});
				}
			}
			UninstallInputSink();
			g_lastModKeyDown.store(false);
			g_refreshQueued.store(false);
		});

		g_installed = true;
		logger::trace("FollowerEquipModeUIIndicator: installed (post display handle={})", g_postDisplayHandle);
	}

	void FollowerEquipModeUIIndicator::RefreshOpenMenu()
	{
		QueueBottomBarRefresh();
	}

	void FollowerEquipModeUIIndicator::Uninstall()
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

		UninstallInputSink();
		g_openMenu = nullptr;
		g_lastModKeyDown.store(false);
		g_refreshQueued.store(false);

		g_installed = false;
		logger::trace("FollowerEquipModeUIIndicator: uninstalled");
	}
}
