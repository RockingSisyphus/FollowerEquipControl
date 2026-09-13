// Prevents SkyUI's exact-zero weight handling from interfering with follower trade interactions.

#pragma once

#include "PCH.h"

namespace FEC::EquipMode::Fixes
{
	class ZeroWeightTakeAllFix
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
