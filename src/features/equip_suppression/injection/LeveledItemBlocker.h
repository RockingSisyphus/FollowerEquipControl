// Blocks InventoryChanges::InitLeveledItems for affected followers so only player-given items remain.

#pragma once

#include "PCH.h"

namespace FEC
{
	class LeveledItemBlocker
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
