// Suppresses vanilla best-weapon auto-equip during follower-to-player weapon transfers.

#pragma once

#include "PCH.h"

namespace FEC::BestWeaponAutoEquipSuppressor
{
	void Install();
	void Uninstall();

	[[nodiscard]] bool ShouldSuppressEquip(RE::Actor* a_actor, RE::TESBoundObject* a_object);
}
