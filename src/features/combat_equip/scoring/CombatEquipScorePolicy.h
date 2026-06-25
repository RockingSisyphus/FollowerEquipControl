// Eligibility gate for combat scoring bias.

#pragma once

#include "PCH.h"

namespace FEC::CombatEquipScorePolicy
{
	[[nodiscard]] bool ShouldBiasForActor(RE::Actor* a_actor) noexcept;
}
