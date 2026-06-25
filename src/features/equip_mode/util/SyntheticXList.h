// Provides synthetic ExtraDataList objects for plain item stacks that need instance-targeted equip calls.

#pragma once

#include "PCH.h"

namespace FEC::SyntheticXList
{
	// AE >= 1.6.629: allocates 0x20 and copies vptr from a donor ExtraDataList.
	// SE / AE < 1.6.629: allocates 0x18; BaseExtraList has no vtable.
	// Do not add ExtraInventoryChanges; it is container-level and can CTD on per-item xLists.
	// Returns nullptr on allocation failure or when no donor vptr is available on AE.
	[[nodiscard]] RE::ExtraDataList* Create(RE::Actor* a_actor, std::int32_t a_count);

	// Creates and injects a synthetic xList only when the entry has implicit plain copies.
	// Returns an existing or synthetic xList, or nullptr on failure.
	[[nodiscard]] RE::ExtraDataList* EnsureXList(RE::Actor* a_actor, RE::InventoryEntryData* a_entry);

	[[nodiscard]] bool IsSynthetic(RE::ExtraDataList* a_xList);
}