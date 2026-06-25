#pragma once

#include "PCH.h"

namespace FEC
{
	// Runs one-time side effects when ContainerMenu opens for an NPC in NPCMode.
	class ContainerMenuOpenActions
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
