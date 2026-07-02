// Centralizes actor-scope decisions.
// Scope combines actor category with the race's equipment capability.

#pragma once

#include "PCH.h"

namespace RE
{
	class Actor;
	class TESObjectARMO;
	class ContainerMenu;
}

namespace FEC::ActorScope
{
	enum class EquipCapability : std::uint8_t
	{
		kNone,
		kFull
	};

	enum class ActorCategory : std::uint8_t
	{
		kOutOfScope,
		kCommanded,
		kKnownFollower,
		kInclusion,
		kCorpse
	};

	struct EquipPolicy
	{
		ActorCategory   category{ ActorCategory::kOutOfScope };
		EquipCapability capability{ EquipCapability::kNone };
		// Follower-like features should apply to this actor.
		bool            inScope{ false };
		// Armor-related systems may run for this actor.
		bool            armorAllowed{ false };
	};

	// Engine-data classification.
	[[nodiscard]] EquipCapability ClassifyEquipCapability(const RE::Actor* a_actor) noexcept;

	// Summons, reanimated actors, and thralls currently commanded by the player.
	[[nodiscard]] bool IsPlayerCommandedActor(RE::Actor* a_actor) noexcept;

	[[nodiscard]] EquipPolicy ResolveEquipPolicy(RE::Actor* a_actor) noexcept;

	[[nodiscard]] bool IsAffectedFollower(RE::Actor* a_actor) noexcept;
	// Actor-level gate; does not validate a specific ARMO.
	[[nodiscard]] bool ArmorValidationAllowed(RE::Actor* a_actor) noexcept;
	// Final armor gate for paths with a known ARMO.
	[[nodiscard]] bool ArmorEquipAllowed(RE::Actor* a_actor, const RE::TESObjectARMO* a_armor) noexcept;
	[[nodiscard]] bool HandEquipAllowed(RE::Actor* a_actor) noexcept;

	// Menu / activation admission helpers.
	[[nodiscard]] bool ShouldAdmitForContainerMenu(
		RE::Actor* a_actor,
		RE::ContainerMenu::ContainerMode a_mode) noexcept;

	[[nodiscard]] bool ShouldAdmitForQuickTrade(RE::Actor* a_actor) noexcept;
}
