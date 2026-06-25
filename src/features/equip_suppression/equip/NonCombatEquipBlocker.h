// Suppresses AI-driven weapon and ammo equips outside combat to prevent idle twitch-equipping.

#pragma once

#include "PCH.h"

namespace FEC::NonCombatEquipBlocker
{
	[[nodiscard]] bool ShouldSuppressEquip(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		bool a_aiDriven,
		bool a_drawn) noexcept;
}
