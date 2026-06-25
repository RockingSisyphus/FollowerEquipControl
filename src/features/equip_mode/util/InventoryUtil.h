// Shared actor/object total-count helper for EquipMode transfer actions.

#pragma once

#include "PCH.h"

#include <cstdint>

namespace FEC::InventoryUtil
{
	[[nodiscard]] std::int32_t GetTotalCount(RE::Actor* a_actor, RE::TESBoundObject* a_object);
}
