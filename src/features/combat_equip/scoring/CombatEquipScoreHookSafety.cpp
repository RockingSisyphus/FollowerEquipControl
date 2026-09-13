#include "CombatEquipScoreHookSafety.h"

#include <cstdint>

namespace FEC::CombatEquipScoreHookSafety
{
	bool IsPlausibleGamePointer(const void* a_ptr) noexcept
	{
		// Tiny invalid values, such as 0x1, can reach scoring hooks; fail open before any TESForm call can AV.
		const auto v = reinterpret_cast<std::uintptr_t>(a_ptr);
		if (v < 0x10000) {
			return false;
		}
		if ((v & (alignof(void*) - 1)) != 0) {
			return false;
		}
		return true;
	}
}
