#include "PickUpObjectBlocker.h"

#include "ActorScope.h"
#include "Logging.h"
#include "PickUpObjectHook.h"
#include "PluginSettings.h"

#include <atomic>
#include <mutex>

namespace FEC::PickUpObjectBlocker
{
	namespace
	{
		std::atomic<bool> g_installed{ false };
		std::mutex g_installLock;

		PickUpObjectHook::ListenerHandle g_handlerHandle{ 0 };

		[[nodiscard]] bool IsEnabled() noexcept
		{
			return PluginSettings::Get().lootBlocking.enablePreventPickupObject;
		}

		[[nodiscard]] bool OnPickUpObject(const PickUpObjectHook::Context& ctx)
		{
			if (!IsEnabled()) {
				return false;
			}

			if (!ctx.actor || ctx.actor->IsPlayerRef()) {
				return false;
			}

			if (!ActorScope::IsAffectedFollower(ctx.actor)) {
				return false;
			}

			// arg3 appears to be true for AI pickups and false for player-commanded pickups.
			// Only block the AI-driven path so follower pickup commands still work.
			if (!ctx.arg3) {
				if (spdlog::should_log(spdlog::level::debug)) {
					logger::debug(
						"PickUpObjectBlocker: ALLOWED (player-commanded, arg3=false) actor={:08X} ({}) obj={:08X} ({}) count={}",
						ctx.actor->GetFormID(),
						ctx.actor->GetName(),
						ctx.object ? ctx.object->GetFormID() : 0,
						ctx.object ? ctx.object->GetName() : "NULL",
						ctx.count);
				}
				return false;
			}

			logger::info(
				"PickUpObjectBlocker: BLOCKED (AI sandbox, arg3=true) actor={:08X} ({}) obj={:08X} ({}) count={}",
				ctx.actor->GetFormID(),
				ctx.actor->GetName(),
				ctx.object ? ctx.object->GetFormID() : 0,
				ctx.object ? ctx.object->GetName() : "NULL",
				ctx.count);
			return true;
		}
	}

	void Install()
	{
		if (g_installed.load()) {
			return;
		}

		std::lock_guard lock(g_installLock);
		if (g_installed.load()) {
			return;
		}

		if (g_handlerHandle == 0) {
			g_handlerHandle = PickUpObjectHook::AddHandler(OnPickUpObject);
		}
		g_installed.store(g_handlerHandle != 0);
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

		if (g_handlerHandle != 0) {
			PickUpObjectHook::RemoveListener(g_handlerHandle);
			g_handlerHandle = 0;
		}
		g_installed.store(false);
	}
}
