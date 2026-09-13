// Shared strict-pick helpers for equip-slot resolution, worn-state checks, and instance selection.

#pragma once

#include "PCH.h"

#include "Common.h"

#include <functional>

namespace FEC::EquipMode::Core
{
	[[nodiscard]] const char* HandToStr(Hand a_hand) noexcept;

	[[nodiscard]] bool HasWornExtra(const RE::ExtraDataList& a_list) noexcept;
	[[nodiscard]] bool HasWornRightExtra(const RE::ExtraDataList& a_list) noexcept;
	[[nodiscard]] bool HasWornLeftExtra(const RE::ExtraDataList& a_list) noexcept;

	[[nodiscard]] const RE::BGSEquipSlot* GetRightHandSlot();
	[[nodiscard]] const RE::BGSEquipSlot* GetLeftHandSlot();
	[[nodiscard]] const RE::BGSEquipSlot* GetSlotForHand(Hand a_hand);
	[[nodiscard]] const RE::BGSEquipSlot* GetSlotForObject(RE::TESBoundObject* a_object, Hand a_hand);

	// Null-safe hand-slot checks. Comparing a resolved slot against GetLeftHandSlot() /
	// GetRightHandSlot() directly is unsafe: when both sides are null the comparison is
	// true, which previously let a null slot reach ActorEquipManager::EquipObject and
	// made the engine fall back to the primary (right) hand.
	[[nodiscard]] bool IsLeftHandSlot(const RE::BGSEquipSlot* a_slot) noexcept;
	[[nodiscard]] bool IsRightHandSlot(const RE::BGSEquipSlot* a_slot) noexcept;
	[[nodiscard]] bool IsHandSlot(const RE::BGSEquipSlot* a_slot) noexcept;

	[[nodiscard]] bool IsTwoHandedObject(RE::TESBoundObject* a_object);

	[[nodiscard]] RE::ExtraDataList* PickUniqueByPredicate(
		RE::InventoryEntryData* a_entry,
		const std::function<bool(const RE::ExtraDataList&)>& a_pred);

	[[nodiscard]] RE::ExtraDataList* PickFirstByPredicate(
		RE::InventoryEntryData* a_entry,
		const std::function<bool(const RE::ExtraDataList&)>& a_pred);

	[[nodiscard]] RE::ExtraDataList* PickUniqueNotWorn(RE::InventoryEntryData* a_entry);
	[[nodiscard]] RE::ExtraDataList* PickFirstNotWorn(RE::InventoryEntryData* a_entry);
	[[nodiscard]] RE::ExtraDataList* PickUniqueWornAny(RE::InventoryEntryData* a_entry);
	[[nodiscard]] RE::ExtraDataList* PickFirstWornAny(RE::InventoryEntryData* a_entry);
	[[nodiscard]] RE::ExtraDataList* PickUniqueWornForHand(RE::InventoryEntryData* a_entry, Hand a_hand);
	[[nodiscard]] RE::ExtraDataList* PickFirstWornForHand(RE::InventoryEntryData* a_entry, Hand a_hand);
}
