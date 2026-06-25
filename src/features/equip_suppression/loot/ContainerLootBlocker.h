// Blocks AI-driven follower weapon/ammo looting from containers via TESObjectCONT::Activate.
// Blocking happens before item transfer; corpse transfers use CorpseLootBlocker instead.

#pragma once

#include "PCH.h"

namespace FEC::ContainerLootBlocker
{
	void Install();
	void Uninstall();
}