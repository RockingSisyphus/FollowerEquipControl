// Gathers sorted filled soul gem options from player and follower inventories.

#pragma once

#include "PCH.h"

#include "WeaponRechargeTypes.h"

#include <vector>

namespace FEC::WeaponRecharge
{
	[[nodiscard]] std::vector<SoulGemOption> GatherFilledSoulGems(RE::Actor* a_player, RE::Actor* a_follower);
}
