#pragma once

#include "PCH.h"

#include "ContainerMenuTransferHook.h"

namespace FEC::EquipMode::Modes
{
	class ConsumableMode
	{
	public:
		static void Install();
		static void Uninstall();

		[[nodiscard]] static bool HandleTransfer(const ContainerMenuTransferHook::Context& ctx, RE::Actor* a_target);

		static void SetPotionsEnabled(bool a_enabled);
		static void SetFoodsEnabled(bool a_enabled);
		static void SetDrinksEnabled(bool a_enabled);
		static void SetIngredientsEnabled(bool a_enabled);
		static void SetIngredientDiscoveryForPlayerEnabled(bool a_enabled);

		[[nodiscard]] static bool ArePotionsEnabled();
		[[nodiscard]] static bool AreFoodsEnabled();
		[[nodiscard]] static bool AreDrinksEnabled();
		[[nodiscard]] static bool AreIngredientsEnabled();
		[[nodiscard]] static bool IsIngredientDiscoveryForPlayerEnabled();
	};
}
