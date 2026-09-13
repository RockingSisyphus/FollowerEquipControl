// Registers SKSE save/load/revert callbacks for persisted plugin state.

#pragma once

#include "PCH.h"

namespace FEC
{
	class Serialization
	{
	public:
		static void Install();
	};
}
