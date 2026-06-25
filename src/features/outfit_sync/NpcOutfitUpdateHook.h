// Optionally suppresses vanilla UpdateNPCOutfit during follower equipment manipulation.
// Fails open when the hook address cannot be resolved.

#pragma once

#include "PCH.h"

namespace FEC
{
	class NpcOutfitUpdateHook
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
