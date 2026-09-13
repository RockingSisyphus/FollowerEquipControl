#include "BestWeaponAutoEquipSuppressor.h"

#include "ContainerMenuTransferHook.h"
#include "ContainerMenuUtil.h"
#include "KnownFollowerState.h"
#include "PluginSettings.h"
#include "WeaponBound.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_set>

namespace FEC::BestWeaponAutoEquipSuppressor
{
	namespace
	{
		std::mutex g_mutex;
		std::unordered_set<std::uint32_t> g_suppressedActors;

		std::atomic_bool g_installed{ false };
		ContainerMenuUtil::ListenerHandle g_closeListenerHandle{ 0 };
		ContainerMenuTransferHook::ListenerHandle g_preTransferHandle{ 0 };

		void ClearAll()
		{
			std::lock_guard lock(g_mutex);
			const auto count = g_suppressedActors.size();
			g_suppressedActors.clear();
			if (count > 0) {
				logger::debug("BestWeaponAutoEquipSuppressor: ClearAll cleared={}", count);
			}
		}

		void SuppressForActor(RE::Actor* a_actor)
		{
			if (!a_actor || a_actor->IsPlayerRef()) {
				return;
			}
			const auto formID = a_actor->GetFormID();
			std::lock_guard lock(g_mutex);
			const auto [it, inserted] = g_suppressedActors.insert(formID);
			logger::debug(
				"BestWeaponAutoEquipSuppressor: SuppressForActor actor={:08X} ({}) inserted={} setSize={}",
				formID,
				a_actor->GetName(),
				inserted,
				g_suppressedActors.size());
		}
	}

	void Install()
	{
		if (!PluginSettings::Get().autoEquipBlocking.enableBestWeaponAutoEquipSuppressor) {
			Uninstall();
			return;
		}

		bool expected = false;
		if (!g_installed.compare_exchange_strong(expected, true)) {
			return;
		}

		ContainerMenuUtil::InstallMenuOpenCloseWatcher();

		g_closeListenerHandle = ContainerMenuUtil::AddOnContainerMenuCloseListener([]() {
			ClearAll();
		});

		// Arm suppression before vanilla best-weapon auto-equip reacts to NPC->Player transfers.
		ContainerMenuTransferHook::Install();
		g_preTransferHandle = ContainerMenuTransferHook::AddPreListener([](const ContainerMenuTransferHook::Context& ctx) {
			if (!ctx.menu || !ctx.object || ctx.count == 0) {
				return;
			}

			const auto containerMode = ctx.menu->GetContainerMode();
			const bool isNpcMode = (containerMode == RE::ContainerMenu::ContainerMode::kNPCMode);
			const bool isNpcToPlayer = (ctx.mode == 0x01U);
			const bool isWeaponNotBound = WeaponBound::IsWeaponAndNotBound(ctx.object);

			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug(
					"BestWeaponAutoEquipSuppressor: pre-transfer obj={:08X} ({}) count={} mode=0x{:02X} containerMode={} isNpcMode={} isNpcToPlayer={} isWeaponNotBound={}",
					ctx.object->GetFormID(),
					ctx.object->GetName(),
					ctx.count,
					ctx.mode,
					static_cast<std::uint32_t>(containerMode),
					isNpcMode,
					isNpcToPlayer,
					isWeaponNotBound);
			}

			if (!isNpcMode || !isNpcToPlayer || !isWeaponNotBound) {
				return;
			}

			auto target = ContainerMenuUtil::GetAffectedTarget(ctx.menu);
			if (!target) {
				logger::debug("BestWeaponAutoEquipSuppressor: skipped -- target null or not affected");
				return;
			}

			logger::debug(
				"BestWeaponAutoEquipSuppressor: arming suppressor for actor={:08X} ({}) due to NPC->Player weapon transfer obj={:08X} ({})",
				target->GetFormID(),
				target->GetName(),
				ctx.object->GetFormID(),
				ctx.object->GetName());
			SuppressForActor(target.get());
		});

		logger::info("BestWeaponAutoEquipSuppressor: installed (close listener handle={}, transfer listener handle={})",
			g_closeListenerHandle, g_preTransferHandle);
	}

	void Uninstall()
	{
		bool expected = true;
		if (!g_installed.compare_exchange_strong(expected, false)) {
			return;
		}

		if (g_preTransferHandle != 0) {
			ContainerMenuTransferHook::RemoveListener(g_preTransferHandle);
			g_preTransferHandle = 0;
		}

		if (g_closeListenerHandle != 0) {
			ContainerMenuUtil::RemoveOnContainerMenuCloseListener(g_closeListenerHandle);
			g_closeListenerHandle = 0;
		}
		ClearAll();
		logger::info("BestWeaponAutoEquipSuppressor: uninstalled");
	}

	bool ShouldSuppressEquip(RE::Actor* a_actor, RE::TESBoundObject* a_object)
	{
		if (!g_installed.load()) {
			return false;
		}

		if (!a_actor || a_actor->IsPlayerRef() || !a_object || !a_object->IsWeapon()) {
			return false;
		}

		// Bound weapons are transient; do not suppress their equips.
		if (WeaponBound::IsWeaponAndBound(a_object)) {
			return false;
		}

		// Drop suppression as soon as the ContainerMenu lifetime ends.
		if (!ContainerMenuUtil::IsContainerMenuOpen()) {
			logger::debug(
				"BestWeaponAutoEquipSuppressor: ShouldSuppressEquip menu closed, clearing all for actor={:08X} obj={:08X}",
				a_actor->GetFormID(),
				a_object->GetFormID());
			ClearAll();
			return false;
		}

		std::lock_guard lock(g_mutex);
		const auto it = g_suppressedActors.find(a_actor->GetFormID());
		if (it == g_suppressedActors.end()) {
			return false;
		}
		return true;
	}
}
