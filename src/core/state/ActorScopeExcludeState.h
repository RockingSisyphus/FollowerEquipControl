// Persistent per-actor flag that excludes an actor from all mod-managed scope (FormID set plus serialization).

#pragma once

#include "PCH.h"

#include <functional>

namespace FEC::ActorScopeExcludeState
{
	// Query
	[[nodiscard]] bool IsExcluded(RE::FormID a_actorID) noexcept;

	// Mutation
	bool Exclude(RE::FormID a_actorID);
	bool Include(RE::FormID a_actorID);

	// Cleanup
	void Clear() noexcept;

	// Serialization helpers
	[[nodiscard]] std::uint32_t GetCount() noexcept;
	using Visitor = std::function<void(RE::FormID)>;
	void ForEach(const Visitor& a_visitor);
}
