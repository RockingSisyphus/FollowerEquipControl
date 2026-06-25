#pragma once

#include "PCH.h"

#include "ContainerMenuTransferHook.h"

namespace FEC::EquipMode::Modes
{
	class SpellTomeMode
	{
	public:
		static void Install();
		static void Uninstall();

		[[nodiscard]] static bool HandleTransfer(const ContainerMenuTransferHook::Context& ctx, RE::Actor* a_target);
	};
}
