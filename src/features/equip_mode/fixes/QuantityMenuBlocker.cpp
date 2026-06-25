#include "QuantityMenuBlocker.h"

#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "Controls.h"
#include "KnownFollowerState.h"
#include "PluginSettings.h"

#include <cstdint>
#include <optional>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

namespace FEC::EquipMode::Fixes
{
	namespace
	{
		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_postDisplayHandle{ 0 };
		ContainerMenuUtil::ListenerHandle g_menuCloseHandle{ 0 };

		// Push SkyUI's quantity threshold out of reach to skip the quantity dialog.
		constexpr std::int32_t kBigCount{ -1111111111 };

		std::optional<std::int32_t> g_defaultMinCount;
		RE::ContainerMenu* g_lastMenu{ nullptr };

		[[nodiscard]] bool TryGetRootBool(RE::ContainerMenu* a_menu, const char* a_memberName, bool& a_out)
		{
			if (!a_menu || !a_memberName) {
				return false;
			}
			auto& root = a_menu->GetRuntimeData().root;
			RE::GFxValue val;
			if (!root.GetMember(a_memberName, &val) || !val.IsBool()) {
				return false;
			}
			a_out = val.GetBool();
			return true;
		}

		[[nodiscard]] bool IsGamepadPlatform(RE::ContainerMenu* a_menu)
		{
			if (!a_menu) {
				return false;
			}
			auto& root = a_menu->GetRuntimeData().root;
			RE::GFxValue val;
			if (!root.GetMember("_platform", &val) || !val.IsNumber()) {
				return false;
			}
			return static_cast<std::int32_t>(val.GetNumber()) != 0;
		}

		[[nodiscard]] bool IsPlayerEquipModeActive(RE::ContainerMenu* a_menu)
		{
			// On gamepad, SkyUI keeps _bEquipMode true, so the flag cannot indicate player equip mode.
			if (IsGamepadPlatform(a_menu)) {
				return false;
			}
			bool enabled{ false };
			if (TryGetRootBool(a_menu, "_bEquipMode", enabled)) {
				return enabled;
			}
			return false;
		}

		[[nodiscard]] bool ShouldOverrideQuantityMinCount(RE::ContainerMenu* a_menu)
		{
			if (!a_menu) {
				return false;
			}

			if (!PluginSettings::Get().skyUIFixes.enableQuantityMenuBlocker) {
				return false;
			}

			if (Controls::IsModKeyDown()) {
				auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
				if (target) {
					return true;
				}
				// GetAffectedTarget rejects dead actors, but Corpse Equip Mode still needs to block
				// the quantity dialog so OnTransferPre sees the mod key as held.
				if (PluginSettings::Get().corpseEquipMode.enable) {
					auto corpse = ContainerMenuUtil::ResolveActorHandle(a_menu->GetTargetRefHandle());
					if (corpse && !corpse->IsPlayerRef() && corpse->IsDead()) {
						return true;
					}
				}
			}

			{
				auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
				if (target) {
					if (IsPlayerEquipModeActive(a_menu)) {
						return true;
					}
				}
			}

			return false;
		}

		[[nodiscard]] bool TryGetQuantityMinCount(RE::ContainerMenu* a_menu, std::int32_t& a_out)
		{
			if (!a_menu) {
				return false;
			}
			auto& root = a_menu->GetRuntimeData().root;
			RE::GFxValue val;
			if (!root.GetMember("_quantityMinCount", &val) || !val.IsNumber()) {
				return false;
			}
			a_out = static_cast<std::int32_t>(val.GetNumber());
			return true;
		}

		bool TrySetQuantityMinCount(RE::ContainerMenu* a_menu, std::int32_t a_value)
		{
			if (!a_menu) {
				return false;
			}
			auto& root = a_menu->GetRuntimeData().root;
			return root.SetMember("_quantityMinCount", RE::GFxValue(static_cast<double>(a_value)));
		}

		void ClearState()
		{
			g_defaultMinCount.reset();
			g_lastMenu = nullptr;
		}

		void RestoreIfNeeded(RE::ContainerMenu* a_menu)
		{
			if (!g_defaultMinCount.has_value()) {
				return;
			}
			if (!a_menu) {
				ClearState();
				return;
			}
			const auto restored = TrySetQuantityMinCount(a_menu, *g_defaultMinCount);
			logger::debug(
				"QuantityMenuBlocker: restore _quantityMinCount={} ({})",
				*g_defaultMinCount,
				restored ? "ok" : "failed");
			ClearState();
		}
	}

	void QuantityMenuBlocker::Install()
	{
		if (g_installed) {
			return;
		}

		{
			const auto& cfg = PluginSettings::Get();
			if (!cfg.skyUIFixes.enableQuantityMenuBlocker) {
				logger::info("QuantityMenuBlocker: disabled by config");
				return;
			}
		}

		ContainerMenuDisplayHook::Install();
		ContainerMenuUtil::InstallMenuOpenCloseWatcher();
		g_menuCloseHandle = ContainerMenuUtil::AddOnContainerMenuCloseListener([]() {
			ClearState();
		});

		g_postDisplayHandle = ContainerMenuDisplayHook::AddPostDisplayListener([](RE::ContainerMenu* a_menu) {
			// Do not restore a cached _quantityMinCount captured from another menu instance.
			if (a_menu != g_lastMenu) {
				ClearState();
				g_lastMenu = a_menu;
			}

			if (!a_menu) {
				ClearState();
				return;
			}

			if (!ShouldOverrideQuantityMinCount(a_menu)) {
				RestoreIfNeeded(a_menu);
				return;
			}

			if (!g_defaultMinCount.has_value()) {
				std::int32_t current{};
				if (TryGetQuantityMinCount(a_menu, current)) {
					g_defaultMinCount = current;
					logger::debug("QuantityMenuBlocker: captured default _quantityMinCount={}", current);
				} else {
					// Do not override unless we can restore the original value later.
					static std::uint32_t s_readFailCount = 0;
					if ((s_readFailCount++ % 120) == 0) {
						logger::debug("QuantityMenuBlocker: could not read _quantityMinCount (not overriding yet)");
					}
					return;
				}
			}

			const auto ok = TrySetQuantityMinCount(a_menu, kBigCount);
			if (!ok) {
				static std::uint32_t s_failCount = 0;
				if ((s_failCount++ % 120) == 0) {
					logger::warn("QuantityMenuBlocker: failed to set _quantityMinCount");
				}
			}
		});

		g_installed = true;
		logger::info("QuantityMenuBlocker: installed (Scaleform _quantityMinCount override)");
	}

	void QuantityMenuBlocker::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_postDisplayHandle) {
			ContainerMenuDisplayHook::UnregisterListener(g_postDisplayHandle);
			g_postDisplayHandle = 0;
		}
		if (g_menuCloseHandle) {
			ContainerMenuUtil::RemoveOnContainerMenuCloseListener(g_menuCloseHandle);
			g_menuCloseHandle = 0;
		}
		ClearState();

		g_installed = false;
		logger::info("QuantityMenuBlocker: uninstalled");
	}
}
