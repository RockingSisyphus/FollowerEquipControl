// Resolves and updates selected enchanted weapon charge through stable inventory instances.

#pragma once

#include "PCH.h"

#include <optional>

namespace FEC::WeaponRecharge
{
	struct SelectedChargeInfo
	{
		RE::InventoryEntryData* entry{ nullptr };
		RE::ExtraDataList* xList{ nullptr };
		RE::TESBoundObject* object{ nullptr };
		RE::ActorHandle owner{};
		double currentAbs{ 0.0 };
		double maxAbs{ 0.0 };
		double currentPercent{ 0.0 };
	};

	// Reads charge info for the currently selected enchanted weapon in ContainerMenu.
	[[nodiscard]] bool TryGetSelectedItemChargeInfo(RE::ContainerMenu* a_menu, SelectedChargeInfo& a_out);

	// If a_xList is null, updates all instances in the selected entry.
	[[nodiscard]] bool TrySetSelectedItemChargeAbs(RE::InventoryEntryData* a_entry, RE::ExtraDataList* a_xList, double a_newAbs);
}
