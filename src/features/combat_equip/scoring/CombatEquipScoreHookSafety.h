// Pointer sanity checks for combat scoring vfunc hooks.

#pragma once

#include "PCH.h"

#include <cstdint>

namespace FEC::CombatEquipScoreHookSafety
{
	[[nodiscard]] bool IsPlausibleGamePointer(const void* a_ptr) noexcept;

	template <class T>
	[[nodiscard]] bool IsPlausiblePolymorphic(const T* a_ptr) noexcept
	{
		if (!IsPlausibleGamePointer(a_ptr)) {
			return false;
		}
		auto* vtbl = *reinterpret_cast<void* const* const*>(a_ptr);
		return IsPlausibleGamePointer(vtbl);
	}
}
