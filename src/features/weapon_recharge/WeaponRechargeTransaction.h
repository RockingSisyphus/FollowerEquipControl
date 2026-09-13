// Applies weapon recharge transactions and reports charge/gem consumption results.

#pragma once

#include "PCH.h"

#include "WeaponRechargeSelection.h"
#include "WeaponRechargeTypes.h"

namespace FEC::WeaponRecharge
{
	struct ApplyResult
	{
		bool applied{ false };
		bool consumedGem{ false };
	};

	[[nodiscard]] ApplyResult TryApplyRecharge(
		const SelectedChargeInfo& a_selected,
		RE::Actor* a_player,
		RE::Actor* a_follower,
		RE::TESSoulGem* a_gem,
		SoulGemSource a_gemSource,
		RE::SOUL_LEVEL a_gemSoul);
}
