#include "KnownFollowerState.h"

#include <unordered_set>

namespace FEC
{
	namespace
	{
		std::unordered_set<RE::FormID> g_knownFollowers;

		[[nodiscard]] bool IsValidNonPlayerActor(const RE::Actor* a_actor) noexcept
		{
			return a_actor != nullptr && !a_actor->IsPlayerRef();
		}

		[[nodiscard]] RE::FormID GetActorID(const RE::Actor* a_actor) noexcept
		{
			if (!a_actor) {
				return 0;
			}
			return a_actor->GetFormID();
		}
	}

	void KnownFollowerState::Clear() noexcept
	{
		g_knownFollowers.clear();
	}

	bool KnownFollowerState::EraseActor(RE::FormID a_formID) noexcept
	{
		return g_knownFollowers.erase(a_formID) > 0;
	}

	bool KnownFollowerState::AddKnownFollower(RE::Actor* a_actor)
	{
		if (!IsValidNonPlayerActor(a_actor)) {
			return false;
		}

		const auto formID = GetActorID(a_actor);
		if (formID == 0) {
			return false;
		}

		return AddKnownFollower(formID);
	}

	bool KnownFollowerState::AddKnownFollower(RE::FormID a_formID)
	{
		if (a_formID == 0) {
			return false;
		}

		return g_knownFollowers.insert(a_formID).second;
	}

	bool KnownFollowerState::IsPlayerTeammate(const RE::Actor* a_actor)
	{
		if (!IsValidNonPlayerActor(a_actor)) {
			return false;
		}
		return a_actor->IsPlayerTeammate();
	}

	bool KnownFollowerState::IsPersistedKnown(const RE::Actor* a_actor)
	{
		if (!IsValidNonPlayerActor(a_actor)) {
			return false;
		}

		const auto formID = GetActorID(a_actor);
		if (formID == 0) {
			return false;
		}

		return g_knownFollowers.contains(formID);
	}

	bool KnownFollowerState::IsPlayerTeammateOrPersistedKnown(const RE::Actor* a_actor)
	{
		return IsPlayerTeammate(a_actor) || IsPersistedKnown(a_actor);
	}

	bool KnownFollowerState::IsPlayerTeammateAndPersistedKnown(const RE::Actor* a_actor)
	{
		return IsPlayerTeammate(a_actor) && IsPersistedKnown(a_actor);
	}

	bool KnownFollowerState::IsNotPlayerTeammateAndPersistedKnown(const RE::Actor* a_actor)
	{
		return !IsPlayerTeammate(a_actor) && IsPersistedKnown(a_actor);
	}

	bool KnownFollowerState::IsPlayerTeammateAndNotPersistedKnown(const RE::Actor* a_actor)
	{
		return IsPlayerTeammate(a_actor) && !IsPersistedKnown(a_actor);
	}

	std::uint32_t KnownFollowerState::GetKnownFollowerCount() noexcept
	{
		return static_cast<std::uint32_t>(g_knownFollowers.size());
	}

	void KnownFollowerState::ForEachKnownFollower(std::function<void(RE::FormID)> a_visitor)
	{
		for (const auto formID : g_knownFollowers) {
			a_visitor(formID);
		}
	}
}
