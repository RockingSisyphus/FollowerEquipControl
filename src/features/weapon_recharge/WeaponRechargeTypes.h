// Shared soul gem option types and soul-to-recharge-value conversion.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <vector>

namespace FEC::WeaponRecharge
{
	enum class SoulGemSource : std::uint8_t
	{
		kPlayer = 0,
		kFollower = 1,
	};

	struct SoulGemOption
	{
		RE::FormID gemFormID{ 0 };
		SoulGemSource source{ SoulGemSource::kPlayer };
		std::int32_t count{ 0 };
		RE::SOUL_LEVEL soul{ RE::SOUL_LEVEL::kNone };
	};

	[[nodiscard]] double GetSoulRechargeValue(RE::SOUL_LEVEL a_soul);
}
