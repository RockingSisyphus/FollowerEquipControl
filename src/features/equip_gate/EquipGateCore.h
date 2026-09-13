// Internal block/allow policy for EquipGate hooks.
// Handles bypass depth, one-shot permits, and deny-by-default decisions.

#pragma once

#include "EquipGate.h"

namespace FEC::EquipGate::Core
{
	[[nodiscard]] std::uint32_t GetBypassDepth() noexcept;
	[[nodiscard]] bool IncrementBypassDepth() noexcept;
	void DecrementBypassDepth() noexcept;

	[[nodiscard]] bool ConsumePermit(const RE::Actor* a_actor, const RE::TESBoundObject* a_object, Operation a_op);
	[[nodiscard]] bool ShouldBlock(Operation a_op, RE::Actor* a_actor, RE::TESBoundObject* a_object, bool a_ignoreCombatSafety = false);
}
