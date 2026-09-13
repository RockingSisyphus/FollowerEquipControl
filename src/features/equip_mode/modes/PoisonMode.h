#pragma once

#include "PCH.h"

#include "Common.h"
#include "ContainerMenuTransferHook.h"

namespace FEC::EquipMode::Modes
{
	class PoisonMode
	{
	public:
		enum class Mode : std::uint8_t
		{
			kApplyToWeapon,
			kConsume
		};

		static void Install();
		static void Uninstall();

		[[nodiscard]] static bool HandleTransfer(const ContainerMenuTransferHook::Context& ctx, RE::Actor* a_target, Core::Hand a_hand);

		static void SetMode(Mode a_mode);
		[[nodiscard]] static Mode GetMode();
	};
}
