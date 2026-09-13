// Facade for combat-time follower equip override decisions.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <optional>

namespace FEC::CombatEquipOverride
{
	struct PreferredItem
	{
		RE::TESBoundObject* item{ nullptr };
		RE::ExtraDataList* extraData{ nullptr };
	};

	enum class EquipClass : std::uint8_t
	{
		kUnknown,
		kMelee,
		kAmmo,
	};

	enum class DecisionAction : std::uint8_t
	{
		kAllowVanilla,
		kSwapToPreferred,
		kBlockEquipAttempt,
	};

	struct Decision
	{
		DecisionAction action{ DecisionAction::kAllowVanilla };
		PreferredItem preferred{};
		const RE::BGSEquipSlot* slotToUse{ nullptr };
		std::uint32_t countToUse{ 1 };
		bool queueEquip{ false };
		bool forceEquip{ false };
		bool applyNow{ true };
		const char* reason{ "" };
	};

	enum class UnequipDecisionAction : std::uint8_t
	{
		kAllowVanilla,
		kBlockUnequipAttempt,
	};

	struct UnequipDecision
	{
		UnequipDecisionAction action{ UnequipDecisionAction::kAllowVanilla };
		const char* reason{ "" };
	};

	[[nodiscard]] bool IsEnabled() noexcept;

	// Called from the EquipObject detour; std::nullopt means vanilla should proceed.
	[[nodiscard]] std::optional<Decision> DecideEquipObject(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count,
		bool a_aiDriven,
		bool a_drawn);

	// Called from the UnequipObject detour; std::nullopt means vanilla should proceed.
	[[nodiscard]] std::optional<UnequipDecision> DecideUnequipObject(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count,
		bool a_aiDriven);

	// Called after vanilla UnequipObject completes.
	void OnUnequipObject(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		bool a_aiDriven);
}
