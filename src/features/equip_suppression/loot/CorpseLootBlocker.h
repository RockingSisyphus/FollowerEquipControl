// Blocks AI-driven follower weapon/ammo looting from dead actors via Actor::RemoveItem.
// Blocking happens before removal; container transfers use ContainerLootBlocker instead.

#pragma once

#include "PCH.h"

namespace FEC::CorpseLootBlocker
{
	void Install();
	void Uninstall();
}
