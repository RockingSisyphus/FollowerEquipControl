// Suppresses or observes vanilla UpdateNPCOutfit for follower outfit and hand-item refresh handling.
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
