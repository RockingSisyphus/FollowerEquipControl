// Shared EquipMode hand vocabulary.

#pragma once

#include <cstdint>

namespace FEC::EquipMode::Core
{
	enum class Hand : std::uint8_t
	{
		kRight,
		kLeft
	};

	[[nodiscard]] const char* HandName(Hand a_hand) noexcept;
}
