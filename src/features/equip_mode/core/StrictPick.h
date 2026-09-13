// Carries the strict-pick decision used by EquipMode action execution.

#pragma once

#include "PCH.h"

namespace FEC::EquipMode::Core
{
	struct StrictPick
	{
		RE::TESBoundObject* object{ nullptr };
		RE::ExtraDataList* xList{ nullptr };
		const RE::BGSEquipSlot* slot{ nullptr };
		const RE::BGSEquipSlot* fromSlot{ nullptr };
		bool unequip{ false };
		bool swapMove{ false };
		bool baseFallback{ false };  // When true, xList may be null and action uses non-instance equip/unequip.
	};
}
