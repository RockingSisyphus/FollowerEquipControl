// Captures selected ContainerMenu row context for instance-aware EquipMode actions.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <optional>

namespace FEC::EquipMode::Selection
{
	enum class EquipState : std::uint8_t
	{
		kUnknown,
		kNotWorn,
		kWornRight,
		kWornLeft,
		kWornBoth
	};

	struct SelectedInstanceContext
	{
		RE::ExtraDataList* xList{ nullptr };
		bool xListBelongsToTargetActor{ true };
		bool capturedFromNonUniqueRow{ false };
	};

	enum class CaptureContext : std::uint8_t
	{
		kDefault = 0,
		kPlayerToNpcTransfer = 1
	};

	// a_actor is required because UI-clone xList pointers can dangle after baseFallback cycles.
	// Re-resolve the actor-owned entry and walk its extraLists instead of the clone's.
	// Pass the resolved target actor: NPC for NPC-side rows, Player for player-side rows.
	[[nodiscard]] std::optional<SelectedInstanceContext> CaptureSelectedInstance(
		RE::ContainerMenu* a_menu,
		RE::TESBoundObject* a_object,
		CaptureContext a_context,
		RE::Actor* a_actor);
}
