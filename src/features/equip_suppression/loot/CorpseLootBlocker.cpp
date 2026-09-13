#include "CorpseLootBlocker.h"

#include "ActorScope.h"
#include "Logging.h"
#include "PluginSettings.h"
#include "RemoveActorItemHook.h"
#include "WeaponBound.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace FEC::CorpseLootBlocker
{
	namespace
	{
		std::atomic<bool> g_installed{ false };
		std::mutex g_installLock;

		RemoveActorItemHook::ListenerHandle g_handlerHandle{ 0 };

		[[nodiscard]] bool IsEnabled() noexcept
		{
			return PluginSettings::Get().lootBlocking.enablePreventContainerLoot;
		}

		[[nodiscard]] bool IsWeaponOrAmmo(RE::TESBoundObject* a_object) noexcept
		{
			if (!a_object) {
				return false;
			}

			if (a_object->IsWeapon()) {
				auto* weap = a_object->As<RE::TESObjectWEAP>();
				if (!weap) {
					return false;
				}
				if (WeaponBound::IsBoundWeapon(weap)) {
					return false;
				}
				if (weap->IsHandToHandMelee()) {
					return false;
				}
				return true;
			}

			if (a_object->As<RE::TESAmmo>() != nullptr) {
				return true;
			}

			return false;
		}

		[[nodiscard]] bool OnRemoveItem(const RemoveActorItemHook::Context& ctx)
		{
			if (!IsEnabled()) {
				return false;
			}

			// Only block transfers into another ref, not drops or sells.
			if (!ctx.moveToRefr) {
				return false;
			}

			auto* destActor = ctx.moveToRefr->As<RE::Actor>();
			if (!destActor || destActor->IsPlayerRef()) {
				return false;
			}

			if (!ActorScope::IsAffectedFollower(destActor)) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"CorpseLootBlocker: skip (not affected follower) source={:08X} item={:08X} ({}) dest={:08X} ({})",
						ctx.sourceRefr ? ctx.sourceRefr->GetFormID() : 0,
						ctx.item ? ctx.item->GetFormID() : 0,
						ctx.item ? ctx.item->GetName() : "NULL",
						destActor->GetFormID(),
						destActor->GetName());
				}
				return false;
			}

			if (!IsWeaponOrAmmo(ctx.item)) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"CorpseLootBlocker: skip (not weapon/ammo) source={:08X} item={:08X} ({}) dest={:08X} ({})",
						ctx.sourceRefr ? ctx.sourceRefr->GetFormID() : 0,
						ctx.item ? ctx.item->GetFormID() : 0,
						ctx.item ? ctx.item->GetName() : "NULL",
						destActor->GetFormID(),
						destActor->GetName());
				}
				return false;
			}

			// Player-source removals are player-given transfers, not corpse looting.
			if (ctx.sourceRefr && ctx.sourceRefr->IsPlayerRef()) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"CorpseLootBlocker: skip (player source) item={:08X} ({}) dest={:08X} ({})",
						ctx.item ? ctx.item->GetFormID() : 0,
						ctx.item ? ctx.item->GetName() : "NULL",
						destActor->GetFormID(),
						destActor->GetName());
				}
				return false;
			}

			// Living actor sources are quest/script-initiated, not AI corpse looting.
			if (ctx.sourceRefr) {
				if (auto* sourceActor = ctx.sourceRefr->As<RE::Actor>(); sourceActor && !sourceActor->IsDead()) {
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace(
							"CorpseLootBlocker: skip (living source) source={:08X} ({}) item={:08X} ({}) dest={:08X} ({})",
							sourceActor->GetFormID(),
							sourceActor->GetName(),
							ctx.item ? ctx.item->GetFormID() : 0,
							ctx.item ? ctx.item->GetName() : "NULL",
							destActor->GetFormID(),
							destActor->GetName());
					}
					return false;
				}
			}

			// Allow player-commanded corpse looting from third-party mods.
			// Player commands use temporary FF-prefix packages.
			if (auto* pkg = destActor->GetCurrentPackage();
				pkg && (pkg->GetFormID() >> 24) == 0xFF) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"CorpseLootBlocker: skip (player-commanded) source={:08X} ({}) item={:08X} ({}) dest={:08X} ({}) pkg={:08X}",
						ctx.sourceRefr ? ctx.sourceRefr->GetFormID() : 0,
						ctx.sourceRefr ? ctx.sourceRefr->GetName() : "NULL",
						ctx.item ? ctx.item->GetFormID() : 0,
						ctx.item ? ctx.item->GetName() : "NULL",
						destActor->GetFormID(),
						destActor->GetName(),
						pkg->GetFormID());
				}
				return false;
			}

			logger::info(
				"CorpseLootBlocker: BLOCKED source={:08X} ({}) item={:08X} ({}) count={} dest={:08X} ({}) reason={}",
				ctx.sourceRefr ? ctx.sourceRefr->GetFormID() : 0,
				ctx.sourceRefr ? ctx.sourceRefr->GetName() : "NULL",
				ctx.item ? ctx.item->GetFormID() : 0,
				ctx.item ? ctx.item->GetName() : "NULL",
				ctx.count,
				destActor->GetFormID(),
				destActor->GetName(),
				static_cast<std::uint32_t>(ctx.reason));
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
			g_handlerHandle = RemoveActorItemHook::AddHandler(OnRemoveItem);
		}
		g_installed.store(g_handlerHandle != 0);
		logger::info("CorpseLootBlocker: installed (handler={}, enabled={})", g_handlerHandle, IsEnabled());
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
			RemoveActorItemHook::RemoveListener(g_handlerHandle);
			g_handlerHandle = 0;
		}
		g_installed.store(false);
	}
}
