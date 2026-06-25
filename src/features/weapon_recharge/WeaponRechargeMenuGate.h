// Centralizes menu eligibility checks for the weapon recharge feature.

#pragma once

#include "PCH.h"

namespace FEC::WeaponRecharge
{
	[[nodiscard]] bool ShouldAffectMenu(RE::ContainerMenu* a_menu);
}
