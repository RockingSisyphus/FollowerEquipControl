#include "CombatEquipScoreTelemetry.h"

#include "ActorScope.h"
#include "CombatEquipScoreEquipSlotCache.h"
#include "CombatEquipScoreHookSafety.h"

#include <mutex>
#include <unordered_map>

namespace FEC::CombatEquipScoreTelemetry
{
	namespace
	{
		constexpr std::uint32_t kBiasTraceEveryN = 8;
		constexpr std::uint32_t kHookTraceEveryN = 40;

		HookTraceCounters g_counters;
	}

	HookTraceCounters& Counters() noexcept
	{
		return g_counters;
	}

	bool ShouldTraceBias(RE::Actor* a_actor) noexcept
	{
		if (!a_actor) {
			return false;
		}
		if (!spdlog::should_log(spdlog::level::trace)) {
			return false;
		}
		// Keep it follower-scoped to avoid log spam.
		return ActorScope::IsAffectedFollower(a_actor);
	}

	void TraceBiasApplied(RE::Actor* a_actor, RE::TESForm* a_form, const RE::BGSEquipSlot* a_slot, float a_before, float a_after)
	{
		if (!ShouldTraceBias(a_actor)) {
			return;
		}

		static std::mutex lock;
		static std::unordered_map<RE::FormID, std::uint32_t> countByActor;

		const auto actorID = a_actor ? a_actor->GetFormID() : 0;
		if (actorID == 0) {
			return;
		}

		{
			std::lock_guard g(lock);
			auto& c = countByActor[actorID];
			++c;
			if ((c % kBiasTraceEveryN) != 0) {
				return;
			}
		}

		const auto& slots = CombatEquipScoreEquipSlotCache::GetHandSlots();
		const char* hand = (a_slot && slots.left && a_slot == slots.left) ? "L" :
			(a_slot && slots.right && a_slot == slots.right) ? "R" : "?";

		logger::trace(
			"CombatEquipScore: bias applied slot={} actor={:08X} ({}) item={:08X} ({}) score={:.2f}->{:.2f}",
			hand,
			actorID,
			a_actor ? a_actor->GetName() : "?",
			a_form ? a_form->GetFormID() : 0,
			a_form ? a_form->GetName() : "NONE",
			a_before,
			a_after);
	}

	bool ShouldTraceHook() noexcept
	{
		return spdlog::should_log(spdlog::level::trace);
	}

	void TraceHookStatus(
		const char* a_event,
		RE::CombatController* a_controller,
		RE::Actor* a_actor,
		RE::TESForm* a_form,
		const RE::BGSEquipSlot* a_slot,
		float a_score,
		std::uintptr_t a_vptr)
	{
		if (!ShouldTraceHook()) {
			return;
		}

		static std::mutex lock;
		static std::uint32_t count = 0;

		{
			std::lock_guard g(lock);
			++count;
			if ((count % kHookTraceEveryN) != 0) {
				return;
			}
		}

		const auto actorID = a_actor ? a_actor->GetFormID() : 0;
		const auto itemID = a_form ? a_form->GetFormID() : 0;

		const auto& slots = CombatEquipScoreEquipSlotCache::GetHandSlots();
		const char* hand = (a_slot && slots.left && a_slot == slots.left) ? "L" :
			(a_slot && slots.right && a_slot == slots.right) ? "R" : "?";

		using FEC::CombatEquipScoreHookSafety::IsPlausibleGamePointer;
		const std::uint32_t aH = (a_controller && IsPlausibleGamePointer(a_controller)) ? a_controller->attackerHandle.native_handle() : 0;
		const std::uint32_t tH = (a_controller && IsPlausibleGamePointer(a_controller)) ? a_controller->targetHandle.native_handle() : 0;

		a_event = a_event ? a_event : "?";

		logger::trace(
			"CombatEquipScore: CalculateScore {} entered={} applied={} pass(disabled/noActor/badActor/player/menu/notPersisted/notCombatOrDrawn/badItem/noMatch)={}/{}/{}/{}/{}/{}/{}/{}/{} err(badThis/badVptr/noOriginal)={}/{}/{}",
			a_event,
			g_counters.entered.load(),
			g_counters.applied.load(),
			g_counters.passDisabled.load(),
			g_counters.passNoActor.load(),
			g_counters.passBadActor.load(),
			g_counters.passPlayer.load(),
			g_counters.passMenuOpen.load(),
			g_counters.passNotPersistedFollower.load(),
			g_counters.passNotCombatOrDrawn.load(),
			g_counters.passBadItem.load(),
			g_counters.passNoMatch.load(),
			g_counters.errBadThis.load(),
			g_counters.errBadVptr.load(),
			g_counters.errNoOriginal.load());
		logger::trace(
			"CombatEquipScore:   context ctrl=0x{:X} aH=0x{:08X} tH=0x{:08X} actor={:08X} ({}) item={:08X} ({}) slot={} score={:.2f} vptr=0x{:X}",
			reinterpret_cast<std::uintptr_t>(a_controller),
			aH,
			tH,
			actorID,
			a_actor ? a_actor->GetName() : "?",
			itemID,
			a_form ? a_form->GetName() : "NONE",
			hand,
			a_score,
			a_vptr);
	}
}
