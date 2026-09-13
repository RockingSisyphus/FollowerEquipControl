// Blocks InventoryChanges::InitOutfitItems for affected followers so only player-given items remain.

#pragma once

#include "PCH.h"

namespace FEC
{
	class OutfitItemBlocker
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
