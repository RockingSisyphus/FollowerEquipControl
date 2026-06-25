// Cached equip-slot pointers used by combat scoring classification.

#pragma once

#include "PCH.h"

namespace FEC::CombatEquipScoreEquipSlotCache
{
	struct HandSlots
	{
		const RE::BGSEquipSlot* left{ nullptr };
		const RE::BGSEquipSlot* right{ nullptr };
	};

	[[nodiscard]] const HandSlots& GetHandSlots() noexcept;
}
