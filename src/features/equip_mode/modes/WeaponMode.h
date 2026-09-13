#pragma once

#include "PCH.h"

#include "Common.h"
#include "StrictPick.h"

#include <optional>

namespace FEC::EquipMode::Modes
{
	class WeaponMode
	{
	public:
		// a_actor provides authoritative total count for baseFallback; template-owned
		// items can have countDelta == 0, which would block stacked dual-wield.
		// Null falls back to countDelta; EquipModeAction passes a non-null actor.
	[[nodiscard]] static std::optional<Core::StrictPick> TryBuildStrictPick(
			RE::InventoryEntryData* a_entry,
			RE::TESBoundObject* a_object,
			Core::Hand a_hand,
			bool a_equipOnly,
			RE::Actor* a_actor = nullptr,
			RE::ExtraDataList* a_preferredXList = nullptr);
	};
}
