// Resolves selected inventory rows to concrete ExtraDataList pointers when possible.

#pragma once

#include "PCH.h"

#include "InstanceSelection.h"

#include <optional>

namespace FEC::EquipMode::Selection
{
	// Non-unique rows and stale xList hints may fall back to a matching instance.
	[[nodiscard]] std::optional<RE::ExtraDataList*> ResolveSelectedExtraStrict(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		const std::optional<SelectedInstanceContext>& a_selected,
		std::optional<EquipState> a_desiredState,
		const char* a_site);
}
