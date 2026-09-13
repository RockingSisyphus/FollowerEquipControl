// Lifecycle gate for combat inventory score-bias hooks.

#pragma once

#include "PCH.h"

namespace FEC::CombatEquipScoreController
{
	// Installs vfunc hooks. Safe to call multiple times.
	void Install();
	void Uninstall();

	// Global hook entry point; currently self-gated by IsEnabled().
	void ApplyIfNeeded(RE::Actor* a_actor, const char* a_reason);

	[[nodiscard]] bool IsEnabled() noexcept;
}
