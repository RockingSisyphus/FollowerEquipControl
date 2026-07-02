// Hooks CreateCtxAcqW Evaluate and Update plus Actor::UpdateCombat.
// Blocking only Evaluate is unsafe: Evaluate can start FindWeapon/FollowPath side effects,
// and Update later reads state that Evaluate normally initializes. When blocking, skip both.
// Actor::UpdateCombat provides a thread-local owner actor because the behavior tree runs
// synchronously inside its call stack. Missing breadcrumb falls through to avoid false blocks.

#include "CombatLootBlocker.h"

#include "ActorScope.h"
#include "Logging.h"
#include "PluginSettings.h"

#include <atomic>
#include <cstdint>
#include <mutex>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <detours/detours.h>

namespace FEC::CombatLootBlocker
{
	namespace
	{
		using EvalFn = std::uint64_t (*)(void*, std::uint64_t, std::uint64_t, std::uint64_t);

		std::atomic<bool> g_installed{ false };
		std::mutex g_lock;
		EvalFn g_origEval{ nullptr };   // slot 2 = Evaluate
		EvalFn g_origUpdate{ nullptr }; // slot 3 = Update

		[[nodiscard]] bool IsEnabled() noexcept
		{
			return PluginSettings::Get().lootBlocking.enablePreventCombatLoot;
		}

		// Cache the actor scope once in Actor::UpdateCombat (vtable slot 0xE4).

		using UpdateCombatFn = void (*)(RE::Actor*);
		UpdateCombatFn g_origUpdateCombat{ nullptr };

		thread_local RE::Actor* tl_combatActor = nullptr;
		thread_local bool       tl_shouldBlock = false;

		void Hook_UpdateCombat(RE::Actor* a_this)
		{
			const bool block = IsEnabled() && ActorScope::IsAffectedFollower(a_this);
			tl_shouldBlock = block;
			tl_combatActor = block ? a_this : nullptr;
			g_origUpdateCombat(a_this);
			tl_combatActor = nullptr;
			tl_shouldBlock = false;
		}

		// Avoid per-frame trace spam for the same actor.
		thread_local RE::FormID tl_lastLoggedBlockActor{ 0 };

		std::uint64_t Hook_Evaluate(void* a_this, std::uint64_t a1, std::uint64_t a2, std::uint64_t a3)
		{
			if (tl_shouldBlock) {
				if (spdlog::should_log(spdlog::level::trace)) {
					auto* actor = tl_combatActor;
					const auto id = actor ? actor->GetFormID() : RE::FormID(0);
					if (id != tl_lastLoggedBlockActor) {
						tl_lastLoggedBlockActor = id;
						logger::trace("CombatLootBlocker: Evaluate BLOCKED [per-actor] actor={:08X} ({})",
							id, actor ? actor->GetName() : "?");
					}
				}
				return 0;
			}
			tl_lastLoggedBlockActor = 0;
			return g_origEval(a_this, a1, a2, a3);
		}

		std::uint64_t Hook_Update(void* a_this, std::uint64_t a1, std::uint64_t a2, std::uint64_t a3)
		{
			if (tl_shouldBlock) {
				// Update reads state initialized by Evaluate; skip it after a blocked Evaluate.
				return 0;
			}
			return g_origUpdate(a_this, a1, a2, a3);
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

		std::lock_guard lock(g_lock);
		if (g_installed.load()) {
			return;
		}

		REL::Relocation<std::uintptr_t> vtbl{
			RE::VTABLE_CombatBehaviorTreeCreateContextNode_CombatBehaviorContextAcquireWeapon_[0]
		};
		auto* vptr = reinterpret_cast<std::uintptr_t*>(vtbl.address());
		g_origEval   = reinterpret_cast<EvalFn>(vptr[2]);  // slot 2 = Evaluate
		g_origUpdate = reinterpret_cast<EvalFn>(vptr[3]);  // slot 3 = Update

		const auto evalRVA   = vptr[2] - REL::Module::get().base();
		const auto updateRVA = vptr[3] - REL::Module::get().base();

		// Actor::UpdateCombat is used as the TLS owner breadcrumb.
		REL::Relocation<std::uintptr_t> actorVtbl{ RE::VTABLE_Character[0] };
		auto* actorVptr = reinterpret_cast<std::uintptr_t*>(actorVtbl.address());
		g_origUpdateCombat = reinterpret_cast<UpdateCombatFn>(actorVptr[0xE4]);

		const auto ucRVA = actorVptr[0xE4] - REL::Module::get().base();

		// Guard against vtable layout mismatches before patching UpdateCombat.
		const auto textSize = REL::Module::get().segment(REL::Segment::Name::textx).size();
		if (ucRVA == 0 || ucRVA >= textSize) {
			logger::error(
				"CombatLootBlocker: UpdateCombat RVA=0x{:X} outside .text (size=0x{:X}). "
				"Possible vtable layout mismatch — aborting install.",
				ucRVA, textSize);
			return;
		}

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());

		if (const auto err = DetourAttach(reinterpret_cast<PVOID*>(&g_origEval),
				reinterpret_cast<PVOID>(&Hook_Evaluate)); err != NO_ERROR) {
			DetourTransactionAbort();
			logger::error("CombatLootBlocker: DetourAttach(Evaluate) failed (err={})", err);
			return;
		}
		if (const auto err = DetourAttach(reinterpret_cast<PVOID*>(&g_origUpdate),
				reinterpret_cast<PVOID>(&Hook_Update)); err != NO_ERROR) {
			DetourTransactionAbort();
			logger::error("CombatLootBlocker: DetourAttach(Update) failed (err={})", err);
			return;
		}
		if (const auto err = DetourAttach(reinterpret_cast<PVOID*>(&g_origUpdateCombat),
				reinterpret_cast<PVOID>(&Hook_UpdateCombat)); err != NO_ERROR) {
			DetourTransactionAbort();
			logger::error("CombatLootBlocker: DetourAttach(UpdateCombat) failed (err={})", err);
			return;
		}

		if (DetourTransactionCommit() != NO_ERROR) {
			logger::error("CombatLootBlocker: DetourTransactionCommit failed");
			return;
		}

		g_installed.store(true);
		logger::info(
			"CombatLootBlocker: installed (Evaluate RVA=0x{:X}, Update RVA=0x{:X}, UpdateCombat RVA=0x{:X}, "
			"runtime=0x{:X}, version={})",
			evalRVA, updateRVA, ucRVA,
			REL::Module::get().base(),
			REL::Module::get().version().string());
	}

	void Uninstall()
	{
		if (!g_installed.load()) {
			return;
		}

		std::lock_guard lock(g_lock);
		if (!g_installed.load()) {
			return;
		}

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		if (const auto err = DetourDetach(reinterpret_cast<PVOID*>(&g_origEval),
				reinterpret_cast<PVOID>(&Hook_Evaluate)); err != NO_ERROR) {
			DetourTransactionAbort();
			logger::warn("CombatLootBlocker: DetourDetach(Evaluate) failed (err={})", err);
			return;
		}
		if (const auto err = DetourDetach(reinterpret_cast<PVOID*>(&g_origUpdate),
				reinterpret_cast<PVOID>(&Hook_Update)); err != NO_ERROR) {
			DetourTransactionAbort();
			logger::warn("CombatLootBlocker: DetourDetach(Update) failed (err={})", err);
			return;
		}
		if (const auto err = DetourDetach(reinterpret_cast<PVOID*>(&g_origUpdateCombat),
				reinterpret_cast<PVOID>(&Hook_UpdateCombat)); err != NO_ERROR) {
			DetourTransactionAbort();
			logger::warn("CombatLootBlocker: DetourDetach(UpdateCombat) failed (err={})", err);
			return;
		}
		if (DetourTransactionCommit() != NO_ERROR) {
			logger::warn("CombatLootBlocker: failed to commit detour removal");
			return;
		}

		g_origEval = nullptr;
		g_origUpdate = nullptr;
		g_origUpdateCombat = nullptr;

		g_installed.store(false);
		logger::info("CombatLootBlocker: uninstalled");
	}
}
