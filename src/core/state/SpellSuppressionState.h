// Per-actor store for user-suppressed spells.
// Only actor-reference spells are suppressible. Actor::RemoveSpell cannot remove spells stored
// on the TESNPC base record or race, including SPID-distributed base spells. Those are filtered
// at the UI layer and never enter this store.

#pragma once

#include "PCH.h"

#include <functional>

namespace FEC::SpellSuppressionState
{
	[[nodiscard]] bool IsSuppressed(RE::FormID a_actorID, RE::FormID a_spellID) noexcept;

	bool Suppress(RE::FormID a_actorID, RE::FormID a_spellID);
	bool Unsuppress(RE::FormID a_actorID, RE::FormID a_spellID);

	void EraseActor(RE::FormID a_actorID) noexcept;
	void Clear() noexcept;

	// Snapshot of spell FormIDs suppressed for the actor.
	[[nodiscard]] std::vector<RE::FormID> GetSuppressedSpells(RE::FormID a_actorID);

	// Erases the actor record synchronously; spell restore is queued on the game thread if the actor still exists.
	void RestoreAndEraseActor(RE::FormID a_actorID);

	// Serialization helpers.
	using PairVisitor = std::function<void(RE::FormID a_actorID, RE::FormID a_spellID)>;
	void               ForEachSuppressed(const PairVisitor& a_visitor);
	[[nodiscard]] std::uint32_t GetTotalCount() noexcept;
}
