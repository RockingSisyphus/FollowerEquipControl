// Verbose debug logging for EquipGate hook decisions and AI observations.

#pragma once

#include "PCH.h"

namespace FEC::EquipGate::Telemetry
{
	void LogCombatObsCombatInventoryScores(const char* a_outcome, RE::Actor* a_actor);

	void LogCombatObsEquipObject(
		const char* a_outcome,
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		bool a_aiDriven,
		std::uint32_t a_bypassDepth,
		std::uint32_t a_count,
		bool a_queueEquip,
		bool a_forceEquip,
		bool a_applyNow);

	void LogCombatObsUnequipObject(
		const char* a_outcome,
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		bool a_aiDriven,
		std::uint32_t a_bypassDepth,
		std::uint32_t a_count,
		bool a_queueEquip,
		bool a_forceEquip,
		bool a_applyNow,
		const RE::BGSEquipSlot* a_slotToReplace);

	void LogCombatObsEquipSpell(
		const char* a_outcome,
		RE::Actor* a_actor,
		RE::SpellItem* a_spell,
		const RE::BGSEquipSlot* a_slot,
		bool a_aiDriven,
		std::uint32_t a_bypassDepth);

	// Save/load boundary.
	void Clear();

	// Actor deletion cleanup.
	void EraseActor(RE::FormID a_actorID);
}
