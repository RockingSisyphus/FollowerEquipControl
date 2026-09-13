#include "EquipModeKeyOverride.h"

#include "ContainerMenuDisplayHook.h"
#include "PluginSettings.h"

namespace FEC
{
	namespace
	{
		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_listenerHandle{ 0 };
	}

	void EquipModeKeyOverride::Install()
	{
		if (g_installed) {
			return;
		}

		ContainerMenuDisplayHook::Install();
		g_listenerHandle = ContainerMenuDisplayHook::AddPreDisplayListener([](RE::ContainerMenu* a_menu) {
			const auto overrideKey = PluginSettings::Get().keyboardControls.skyuiEquipModeKey;
			if (overrideKey == 0) {
				return;
			}

			if (!a_menu) {
				return;
			}

			auto& root = a_menu->GetRuntimeData().root;
			if (!root.IsObject()) {
				return;
			}

			// SkyUI creates _equipModeKey during setConfig; do not override before that.
			RE::GFxValue currentKey;
			if (!root.GetMember("_equipModeKey", &currentKey) || !currentKey.IsNumber()) {
				return;
			}

			const auto current = static_cast<std::uint32_t>(currentKey.GetNumber());
			if (current == overrideKey) {
				return;
			}

			// SkyUI reads _equipModeKey in handleInput and _equipModeControls in updateBottomBar;
			// patch both so the custom key works and the bottom bar shows the right label.
			root.SetMember("_equipModeKey", RE::GFxValue(static_cast<double>(overrideKey)));

			auto* view = a_menu->uiMovie.get();
			if (view) {
				RE::GFxValue controls;
				view->CreateObject(&controls);
				controls.SetMember("keyCode", RE::GFxValue(static_cast<double>(overrideKey)));
				root.SetMember("_equipModeControls", controls);
			}

			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("EquipModeKeyOverride: overrode _equipModeKey {} -> {}", current, overrideKey);
			}
		});

		g_installed = true;
		logger::info("EquipModeKeyOverride: installed");
	}

	void EquipModeKeyOverride::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_listenerHandle != 0) {
			ContainerMenuDisplayHook::UnregisterListener(g_listenerHandle);
			g_listenerHandle = 0;
		}

		g_installed = false;
		logger::info("EquipModeKeyOverride: uninstalled");
	}
}
