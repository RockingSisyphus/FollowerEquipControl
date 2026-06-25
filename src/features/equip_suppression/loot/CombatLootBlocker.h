// Blocks follower combat weapon-acquisition behavior before FindWeapon/FollowPath/PickUpObject starts.

#pragma once

#include "PCH.h"

namespace FEC::CombatLootBlocker
{
	void Install();
	void Uninstall();
}
