#include "CombatEquipScorePolicy.h"

#include "ContainerMenuUtil.h"

#include "ActorScope.h"
#include "CombatEquipScore.h"
#include "CombatEquipScoreHookSafety.h"

namespace FEC::CombatEquipScorePolicy
{
	bool ShouldBiasForActor(RE::Actor* a_actor) noexcept
	{
		using FEC::CombatEquipScoreHookSafety::IsPlausiblePolymorphic;

		if (!a_actor) {
			return false;
		}
		if (!IsPlausiblePolymorphic(a_actor)) {
			return false;
		}
		if (a_actor->IsPlayerRef()) {
			return false;
		}
		if (!FEC::CombatEquipScoreController::IsEnabled()) {
			return false;
		}
		if (ContainerMenuUtil::IsContainerMenuOpen()) {
			return false;
		}
		if (!ActorScope::IsAffectedFollower(a_actor)) {
			return false;
		}

		auto* st = a_actor->AsActorState();
		const bool drawn = (st && st->IsWeaponDrawn());
		const bool inCombat = a_actor->IsInCombat();
		return drawn || inCombat;
	}
}
