// Null-safe bound-weapon identification used by equip gating and suppression.

#pragma once

#include "PCH.h"

namespace FEC::WeaponBound
{
	[[nodiscard]] inline bool IsBoundWeapon(const RE::TESObjectWEAP* a_weapon) noexcept
	{
		if (!a_weapon) {
			return false;
		}
		return a_weapon->weaponData.flags2.any(RE::TESObjectWEAP::Data::Flag2::kBoundWeapon);
	}

	[[nodiscard]] inline bool IsWeaponAndBound(const RE::TESBoundObject* a_object) noexcept
	{
		auto* weapon = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr;
		return IsBoundWeapon(weapon);
	}

	[[nodiscard]] inline bool IsWeaponAndNotBound(const RE::TESBoundObject* a_object) noexcept
	{
		auto* weapon = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr;
		return weapon && !IsBoundWeapon(weapon);
	}
}
