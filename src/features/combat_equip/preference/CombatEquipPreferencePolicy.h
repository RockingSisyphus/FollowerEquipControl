// Category classification and conflict-clearing policy for combat-equip preferences.

#pragma once

#include "PCH.h"

#include "CombatEquipPreference.h"

#include <cstdint>
#include <optional>

namespace FEC::CombatEquip::Preference::Policy
{
	using Category = ::FEC::CombatEquipPreference::Category;

	// Strict capture classification; std::nullopt means no preference should be recorded.
	[[nodiscard]] std::optional<Category> TryClassifyCEPCategory(RE::TESBoundObject* a_object, bool a_leftHand);

	// Mask bits correspond to Category underlying values.
	[[nodiscard]] std::uint16_t CrossClearMaskOnCaptureEquip(Category a_cat) noexcept;

	// Unequip capture only clears categories that depend on hand direction.
	[[nodiscard]] bool ShouldClearOnCaptureUnequip(Category a_cat, bool a_leftHand) noexcept;
}
