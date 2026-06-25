#pragma once

#include <optional>

#include "PCH.h"

#include "Common.h"
#include "StrictPick.h"

namespace RE
{
	class InventoryEntryData;
	class TESBoundObject;
}

namespace FEC::EquipMode::Modes
{
	class ArmorMode
	{
	public:
		[[nodiscard]] static std::optional<Core::StrictPick> TryBuildStrictPick(
			RE::InventoryEntryData* a_entry,
			RE::TESBoundObject* a_object,
			Core::Hand a_hand,
			bool a_equipOnly,
			RE::ExtraDataList* a_preferredXList = nullptr);
	};
}
