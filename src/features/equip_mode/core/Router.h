// Routes mod-key ContainerMenu transfers to EquipMode handlers and strict action execution.

#pragma once

#include "PCH.h"

namespace FEC::EquipMode::Core
{
	class Router
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
