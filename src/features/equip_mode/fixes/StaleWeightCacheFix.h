// Corrects stale cached weight values that can gray out weighted Give-tab items.

#pragma once

#include "PCH.h"

namespace FEC::EquipMode::Fixes
{
	class StaleWeightCacheFix
	{
	public:
		static void Install();
		static void Uninstall();

		// Must match the engine signature of InventoryChanges::GetInventoryWeight.
		static float Thunk(RE::InventoryChanges* a_this);
	};
}
