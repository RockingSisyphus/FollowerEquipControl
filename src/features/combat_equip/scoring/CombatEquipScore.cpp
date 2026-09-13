#include "CombatEquipScore.h"

#include "CombatEquipScoreVTableHook.h"
#include "PluginSettings.h"

#include <atomic>
#include <mutex>

namespace FEC::CombatEquipScoreController
{
	namespace
	{
		std::atomic<bool> g_installed{ false };
		std::mutex g_installLock;
	}

	bool IsEnabled() noexcept
	{
		return PluginSettings::Get().combatEquipPreference.enableScoring;
	}

	void Install()
	{
		std::lock_guard lock(g_installLock);
		const bool want = IsEnabled();
		const bool have = g_installed.load();

		if (want && !have) {
			CombatEquipScoreVTableHook::Install();
			g_installed.store(true);
			logger::info("CombatEquipScoreController: installed (enabled)");
			return;
		}
		if (!want && have) {
			CombatEquipScoreVTableHook::Uninstall();
			g_installed.store(false);
			logger::info("CombatEquipScoreController: uninstalled (disabled)");
			return;
		}
	}

	void Uninstall()
	{
		if (!g_installed.load()) {
			return;
		}

		std::lock_guard lock(g_installLock);
		if (!g_installed.load()) {
			return;
		}

		CombatEquipScoreVTableHook::Uninstall();
		g_installed.store(false);
	}

	void ApplyIfNeeded(RE::Actor* a_actor, const char* a_reason)
	{
		// The hook is global and self-gated. This entrypoint exists for future tuning/warm-up,
		// and to keep callers decoupled from hook internals.
		(void)a_actor;
		(void)a_reason;
	}
}
