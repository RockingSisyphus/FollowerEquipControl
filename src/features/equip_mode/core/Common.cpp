#include "Common.h"

namespace FEC::EquipMode::Core
{
	const char* HandName(Hand a_hand) noexcept
	{
		return a_hand == Hand::kLeft ? "left" : "right";
	}
}
