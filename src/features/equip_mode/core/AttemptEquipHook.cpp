#include "AttemptEquipHook.h"

#include "ContainerMenuUtil.h"
#include "Controls.h"
#include "SkyUiMenuUtil.h"

#include "PCH.h"

namespace FEC::EquipMode::Core::AttemptEquipHook
{
	namespace
	{
		using namespace FEC::EquipMode::SkyUiMenuUtil;

		constexpr const char* kFecAttemptEquipHooked = "__fecEquipModeAttemptEquipHooked";
		constexpr const char* kFecOriginalAttemptEquip = "__fecEquipModeOriginalAttemptEquip";

		class AttemptEquipHookHandler final : public RE::GFxFunctionHandler
		{
		public:
			void Call(Params& a_params) override
			{
				// Engine AttemptEquip slot: 1 = left hand, 0 = right hand.
				auto* menu = ContainerMenuUtil::GetOpenContainerMenu().get();
				if (menu && ShouldAffectMenu(menu) && Controls::IsModKeyDown()) {
					auto& root = menu->GetRuntimeData().root;
					if (root.IsObject()) {
						if (!IsSkyUiPcPlatform(root)) {
							// Gamepad hand keys drive transfer directly; block engine AttemptEquip while the mod key is held.
							logger::trace("GamepadAttemptEquip: blocked (mod key held)");
							return;
						}

						// Latch the engine slot before transfer fires; fast RMB releases can be missed before ContainerMenuTransferHook runs.
						if (a_params.argCount >= 1 && a_params.args[0].IsNumber()) {
							const auto slot = static_cast<int>(a_params.args[0].GetNumber());
							if (slot == 1) {
								Controls::SetHandOverrideLeft();
							} else if (slot == 0) {
								Controls::SetHandOverrideRight();
							}
							logger::trace("AttemptEquip: PC hand override for slot={}", slot);
						}
					}
				}

				if (a_params.thisPtr && a_params.thisPtr->IsObject()) {
					(void)a_params.thisPtr->Invoke(
						kFecOriginalAttemptEquip, a_params.retVal,
						a_params.args, a_params.argCount);
				}
			}
		};

		AttemptEquipHookHandler* GetAttemptEquipHookHandler()
		{
			static AttemptEquipHookHandler* handler = []() {
				auto* h = new AttemptEquipHookHandler();
				h->AddRef();
				return h;
			}();
			return handler;
		}
	}

	void TryInstallHook(RE::ContainerMenu* menu)
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

		bool attemptEquipHooked = false;
		if (!TryGetMemberBool(root, kFecAttemptEquipHooked, attemptEquipHooked) || !attemptEquipHooked) {
			RE::GFxValue origAttemptEquip;
			if (root.GetMember("AttemptEquip", &origAttemptEquip) && (origAttemptEquip.IsObject() || origAttemptEquip.IsDisplayObject())) {
				root.SetMember(kFecOriginalAttemptEquip, origAttemptEquip);
				RE::GFxValue hookAttemptEquipFn;
				view->CreateFunction(&hookAttemptEquipFn, GetAttemptEquipHookHandler());
				root.SetMember("AttemptEquip", hookAttemptEquipFn);
				root.SetMember(kFecAttemptEquipHooked, RE::GFxValue(true));
				logger::trace("AttemptEquipHook: hooked root.AttemptEquip");
			}
		}
	}

	void TryUninstallHook(RE::ContainerMenu* menu)
	{
		if (!menu) {
			return;
		}
		auto& root = menu->GetRuntimeData().root;
		if (!root.IsObject()) {
			return;
		}
		bool hooked = false;
		if (TryGetMemberBool(root, kFecAttemptEquipHooked, hooked) && hooked) {
			RE::GFxValue original;
			if (root.GetMember(kFecOriginalAttemptEquip, &original)) {
				root.SetMember("AttemptEquip", original);
			}
			root.SetMember(kFecAttemptEquipHooked, RE::GFxValue(false));
			logger::trace("AttemptEquipHook: unhooked root.AttemptEquip");
		}
	}
}
