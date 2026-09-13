// Diagnostic counters and trace helpers for combat scoring hooks.

#pragma once

#include "PCH.h"

#include <atomic>
#include <cstdint>

namespace FEC::CombatEquipScoreTelemetry
{
	struct HookTraceCounters
	{
		std::atomic<std::uint64_t> entered{ 0 };
		std::atomic<std::uint64_t> applied{ 0 };
		std::atomic<std::uint64_t> passDisabled{ 0 };
		std::atomic<std::uint64_t> passNoActor{ 0 };
		std::atomic<std::uint64_t> passBadActor{ 0 };
		std::atomic<std::uint64_t> passPlayer{ 0 };
		std::atomic<std::uint64_t> passMenuOpen{ 0 };
		std::atomic<std::uint64_t> passNotPersistedFollower{ 0 };
		std::atomic<std::uint64_t> passNotCombatOrDrawn{ 0 };
		std::atomic<std::uint64_t> passBadItem{ 0 };
		std::atomic<std::uint64_t> passNoMatch{ 0 };
		std::atomic<std::uint64_t> errBadThis{ 0 };
		std::atomic<std::uint64_t> errBadVptr{ 0 };
		std::atomic<std::uint64_t> errNoOriginal{ 0 };
	};

	[[nodiscard]] HookTraceCounters& Counters() noexcept;

	[[nodiscard]] bool ShouldTraceBias(RE::Actor* a_actor) noexcept;
	void TraceBiasApplied(RE::Actor* a_actor, RE::TESForm* a_form, const RE::BGSEquipSlot* a_slot, float a_before, float a_after);

	[[nodiscard]] bool ShouldTraceHook() noexcept;
	void TraceHookStatus(
		const char* a_event,
		RE::CombatController* a_controller,
		RE::Actor* a_actor,
		RE::TESForm* a_form,
		const RE::BGSEquipSlot* a_slot,
		float a_score,
		std::uintptr_t a_vptr);
}
