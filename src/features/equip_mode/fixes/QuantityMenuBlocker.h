// Suppresses SkyUI's quantity slider during EquipMode-owned ContainerMenu transfers.

#pragma once

#include "PCH.h"

namespace FEC::EquipMode::Fixes
{
	class QuantityMenuBlocker
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
