#include "ContainerLootBlocker.h"

#include "ActorScope.h"
#include "Logging.h"
#include "PluginSettings.h"
#include "Relocations.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace FEC::ContainerLootBlocker
{
	namespace
	{
		std::atomic<bool> g_installed{ false };
		std::mutex g_installLock;

		using Activate_t = bool(RE::TESObjectCONT*, RE::TESObjectREFR*, RE::TESObjectREFR*,
			std::uint8_t, RE::TESBoundObject*, std::int32_t);
		inline static REL::Relocation<Activate_t*> g_origFunc;
		inline static std::uintptr_t g_origAddr{ 0 };

		[[nodiscard]] bool IsEnabled() noexcept
		{
			return PluginSettings::Get().lootBlocking.enablePreventContainerLoot;
		}

		[[nodiscard]] bool ShouldBlock(RE::TESObjectREFR* a_containerRef, RE::Actor* a_actor) noexcept
		{
			if (!a_containerRef || !a_actor) {
				return false;
			}

			if (a_actor->IsPlayerRef()) {
				return false;
			}

			if (!ActorScope::IsAffectedFollower(a_actor)) {
				return false;
			}

			// Allow player-commanded looting. Player commands use temporary FF-prefix packages.
			if (auto* pkg = a_actor->GetCurrentPackage();
				pkg && (pkg->GetFormID() >> 24) == 0xFF) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"ContainerLootBlocker: skip (player-commanded) container={:08X} ({}) actor={:08X} ({}) pkg={:08X}",
						a_containerRef->GetFormID(),
						a_containerRef->GetName(),
						a_actor->GetFormID(),
						a_actor->GetName(),
						pkg->GetFormID());
				}
				return false;
			}

			return true;
		}

		static bool Thunk(RE::TESObjectCONT* a_this, RE::TESObjectREFR* a_targetRef,
			RE::TESObjectREFR* a_activatorRef, std::uint8_t a_arg3,
			RE::TESBoundObject* a_object, std::int32_t a_targetCount)
		{
			if (IsEnabled()) {
				auto* actor = a_activatorRef ? a_activatorRef->As<RE::Actor>() : nullptr;
				if (actor && ShouldBlock(a_targetRef, actor)) {
					logger::info(
						"ContainerLootBlocker: BLOCKED container={:08X} ({}) actor={:08X} ({})",
						a_targetRef ? a_targetRef->GetFormID() : 0,
						a_targetRef ? a_targetRef->GetName() : "NULL",
						actor->GetFormID(),
						actor->GetName());
					return false;
				}
			}

			return g_origFunc(a_this, a_targetRef, a_activatorRef, a_arg3, a_object, a_targetCount);
		}
	}

	void Install()
	{
		if (!IsEnabled()) {
			Uninstall();
			return;
		}

		if (g_installed.load()) {
			return;
		}

		std::lock_guard lock(g_installLock);
		if (g_installed.load()) {
			return;
		}

		if (!IsEnabled()) {
			return;
		}

		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_TESObjectCONT[0] };
		g_origAddr = vtbl.write_vfunc(Relocations::kTESObjectCONT_ActivateVfuncIndex,
			reinterpret_cast<std::uintptr_t>(Thunk));
		g_origFunc = g_origAddr;

		g_installed.store(true);
		logger::info("ContainerLootBlocker: installed (TESObjectCONT::Activate vtable hook)");
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

		REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_TESObjectCONT[0] };
		if (!vtbl.address()) {
			logger::warn("ContainerLootBlocker: uninstall skipped (TESObjectCONT vtable address not found)");
			return;
		}

		const auto idx = Relocations::kTESObjectCONT_ActivateVfuncIndex;
		const auto thunkAddr = reinterpret_cast<std::uintptr_t>(Thunk);
		auto* const slot = reinterpret_cast<std::uintptr_t*>(vtbl.address() + (idx * sizeof(std::uintptr_t)));
		const auto current = *slot;
		if (current != thunkAddr) {
			logger::warn("ContainerLootBlocker: uninstall skipped (vtable slot modified by another plugin)");
			return;
		}

		(void)vtbl.write_vfunc(idx, g_origAddr);
		g_origAddr = 0;
		g_installed.store(false);
	}
}