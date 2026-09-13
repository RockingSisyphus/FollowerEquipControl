// Resolves an actor from combat scoring hook contexts.

#pragma once

#include "PCH.h"

namespace FEC::CombatEquipScoreActorResolver
{
	[[nodiscard]] RE::Actor* TryResolveActor(RE::CombatController* a_controller) noexcept;
}
