#include "GamepadEquipHook.h"

#include "ContainerMenuUtil.h"
#include "Controls.h"
#include "SkyUiMenuUtil.h"

#include "PCH.h"

#include <string_view>

namespace FEC::EquipMode::Core::GamepadEquipHook
{
	namespace
	{
		using namespace FEC::EquipMode::SkyUiMenuUtil;

		constexpr const char* kFecHandleInputHooked = "__fecEquipModeHandleInputHooked";
		constexpr const char* kFecOriginalHandleInput = "__fecEquipModeOriginalHandleInput";

		class HandleInputHookHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				auto callOriginal = [&](Params& params) {
					if (!params.thisPtr || !params.thisPtr->IsObject()) {
						return;
					}
					(void)params.thisPtr->Invoke(kFecOriginalHandleInput, params.retVal, params.args, params.argCount);
				};

				thread_local bool inHook = false;
				if (inHook) {
					callOriginal(a_params);
					return;
				}
				inHook = true;

				// Gamepad LT/RT normally reaches AttemptEquip directly and bypasses handleInput.
				// AttemptEquip blocks that path while the mod key is held; this hook then drives
				// SkyUI's transfer path and records the intended hand for the router.
				bool intercepted = false;

				auto* menu = ContainerMenuUtil::GetOpenContainerMenu().get();
				if (menu && ShouldAffectMenu(menu)) {
					auto& root = menu->GetRuntimeData().root;
					if (root.IsObject() && !IsSkyUiPcPlatform(root) && a_params.argCount >= 1) {
						const auto& details = a_params.args[0];
						if (details.IsObject() || details.IsDisplayObject()) {
							std::string_view value;
							bool isKeyDown = true;
							if (TryGetMemberString(details, "value", value)) {
								isKeyDown = (value == "keyDown");
							}

							if (isKeyDown) {
								double skseKeycodeNum = -1.0;
								{
									RE::GFxValue v;
									if (details.GetMember("skseKeycode", &v) && v.IsNumber()) {
										skseKeycodeNum = v.GetNumber();
									}
								}
								const auto skseKeycode = static_cast<std::uint32_t>(skseKeycodeNum);
								const auto rightHandKey = Controls::GetGamepadRightHandKey();
								const auto leftHandKey = Controls::GetGamepadLeftHandKey();

								const bool isActionButton = (skseKeycode == leftHandKey || skseKeycode == rightHandKey);
								const bool modKeyHeld = Controls::IsModKeyDown();
								const bool hasSelection = IsItemSelected(root);

								if (isActionButton && spdlog::should_log(spdlog::level::trace)) {
									logger::trace("GamepadInput: key={} modKey={} selected={}", skseKeycode, modKeyHeld, hasSelection);
								}

								if (isActionButton && modKeyHeld && hasSelection) {
									// Clear stale _equipHand so startItemTransfer uses the ItemTransfer delegate path.
									root.SetMember("_equipHand", RE::GFxValue());

									if (skseKeycode == leftHandKey) {
										logger::debug("GamepadInput: intercepted LT+X (left hand equip)");
										Controls::SetHandOverrideLeft();
										(void)root.Invoke("startItemTransfer", nullptr, nullptr, 0);
										intercepted = true;
									} else {
										logger::debug("GamepadInput: intercepted LT+{} (right hand equip)", skseKeycode);
										Controls::ClearHandOverride();
										(void)root.Invoke("startItemTransfer", nullptr, nullptr, 0);
										intercepted = true;
									}
								}
							}
						}
					}
				}

				if (intercepted) {
					if (a_params.retVal) {
						a_params.retVal->SetBoolean(true);
					}
				} else {
					callOriginal(a_params);
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
	}

	void TryInstallHooks(RE::ContainerMenu* menu)
	{
		if (!menu) {
			return;
		}

		auto& root = menu->GetRuntimeData().root;
		if (!root.IsObject() || !IsSkyUiPresent(root)) {
			return;
		}

		auto view = menu->uiMovie.get();
		if (!view) {
			return;
		}

		bool handleInputHooked = false;
		if (!TryGetMemberBool(root, kFecHandleInputHooked, handleInputHooked) || !handleInputHooked) {
			RE::GFxValue orig;
			if (root.GetMember("handleInput", &orig) && (orig.IsObject() || orig.IsDisplayObject())) {
				root.SetMember(kFecOriginalHandleInput, orig);
				RE::GFxValue hookFn;
				view->CreateFunction(&hookFn, GetHandleInputHookHandler());
				root.SetMember("handleInput", hookFn);
				root.SetMember(kFecHandleInputHooked, RE::GFxValue(true));
				logger::trace("GamepadEquipHook: hooked root.handleInput");
			}
		}
	}

	void TryUninstallHooks(RE::ContainerMenu* menu)
	{
		if (!menu) {
			return;
		}
		auto& root = menu->GetRuntimeData().root;
		if (!root.IsObject()) {
			return;
		}
		bool hooked = false;
		if (TryGetMemberBool(root, kFecHandleInputHooked, hooked) && hooked) {
			RE::GFxValue original;
			if (root.GetMember(kFecOriginalHandleInput, &original)) {
				root.SetMember("handleInput", original);
			}
			root.SetMember(kFecHandleInputHooked, RE::GFxValue(false));
			logger::trace("GamepadEquipHook: unhooked root.handleInput");
		}
	}
}
