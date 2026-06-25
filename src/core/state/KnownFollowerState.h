// Save-persisted FormID set for followers the mod has seen or managed.

#pragma once

#include "PCH.h"

namespace FEC
{
	class KnownFollowerState
	{
	public:
		static void Clear() noexcept;
		static bool EraseActor(RE::FormID a_formID) noexcept;

		static bool AddKnownFollower(RE::Actor* a_actor);
		static bool AddKnownFollower(RE::FormID a_formID);

		// Runtime teammate state and persisted-known state are intentionally separate:
		// - IsPlayerTeammate: current teammate status, including temporary followers.
		// - IsPersistedKnown: FormID stored in the save-persisted KnownFollowers set.
		static bool IsPlayerTeammate(const RE::Actor* a_actor);
		static bool IsPersistedKnown(const RE::Actor* a_actor);
		static bool IsPlayerTeammateOrPersistedKnown(const RE::Actor* a_actor);
		static bool IsPlayerTeammateAndPersistedKnown(const RE::Actor* a_actor);
		static bool IsNotPlayerTeammateAndPersistedKnown(const RE::Actor* a_actor);
		static bool IsPlayerTeammateAndNotPersistedKnown(const RE::Actor* a_actor);

		static std::uint32_t GetKnownFollowerCount() noexcept;

		static void ForEachKnownFollower(std::function<void(RE::FormID)> a_visitor);
	};
}
