// Removes or retags follower outfit items during coordinated ContainerMenu sanitize passes.

#pragma once

#include "PCH.h"

namespace FEC
{
	class OutfitItemSanitizer
	{
	public:
		static void Install();
		static void Uninstall();

		// Game-thread sanitize pass. If this actor owns the open ContainerMenu,
		// hand off to the coordinated post-display sanitizer instead.
		static void RunForActor(RE::Actor* a_actor);
	};
}
