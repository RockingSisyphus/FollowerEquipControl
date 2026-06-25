// Removes unsafe non-playable follower gear at trade time so AI evaluates playable items.

#pragma once

#include "PCH.h"

namespace FEC
{
	class NonPlayableItemSanitizer
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
