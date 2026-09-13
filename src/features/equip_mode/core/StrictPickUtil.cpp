#include "StrictPickUtil.h"

#include <RE/B/BGSDefaultObjectManager.h>
#include <RE/E/ExtraWorn.h>
#include <RE/E/ExtraWornLeft.h>
#include <RE/S/ScrollItem.h>
#include <RE/T/TESForm.h>

namespace FEC::EquipMode::Core
{
	const char* HandToStr(Hand a_hand) noexcept
	{
		return a_hand == Hand::kLeft ? "left" : "right";
	}

	bool HasWornExtra(const RE::ExtraDataList& a_list) noexcept
	{
		return a_list.HasType<RE::ExtraWorn>() || a_list.HasType<RE::ExtraWornLeft>();
	}

	bool HasWornRightExtra(const RE::ExtraDataList& a_list) noexcept
	{
		return a_list.HasType<RE::ExtraWorn>();
	}

	bool HasWornLeftExtra(const RE::ExtraDataList& a_list) noexcept
	{
		return a_list.HasType<RE::ExtraWornLeft>();
	}

	namespace
	{
		// Vanilla BGSEquipSlot forms, used as a last-resort fallback when neither the
		// default object manager nor the editor-ID string table can resolve them.
		constexpr RE::FormID kLeftHandSlotFormID = 0x00013F43;   // EQUP "LeftHand" (Skyrim.esm)
		constexpr RE::FormID kRightHandSlotFormID = 0x00013F42;  // EQUP "RightHand" (Skyrim.esm)

		[[nodiscard]] const RE::BGSEquipSlot* LookupDefaultEquipSlot(RE::DEFAULT_OBJECT a_object) noexcept
		{
			auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
			return dom ? dom->GetObject<RE::BGSEquipSlot>(a_object) : nullptr;
		}

		[[nodiscard]] bool IsUsableEquipSlot(const RE::BGSEquipSlot* a_slot) noexcept
		{
			// A null slot, or a form without a FormID, is not a usable equip slot.
			// Passing either to ActorEquipManager::EquipObject makes the engine silently
			// fall back to the primary (right) hand, which is exactly the failure this
			// guard exists to prevent.
			return a_slot && a_slot->GetFormID() != 0;
		}

		[[nodiscard]] const RE::BGSEquipSlot* ResolveHandSlot(bool a_leftHand) noexcept
		{
			// Resolution order matters: the default object manager is the authoritative
			// engine reference and does not depend on the editor-ID string table, so try
			// it first instead of relying on LookupByEditorID alone.
			if (const auto* fromDefaultObject = LookupDefaultEquipSlot(
					a_leftHand ? RE::DEFAULT_OBJECT::kLeftHandEquip : RE::DEFAULT_OBJECT::kRightHandEquip);
				IsUsableEquipSlot(fromDefaultObject)) {
				return fromDefaultObject;
			}

			if (const auto* fromEditorID =
					RE::TESForm::LookupByEditorID<RE::BGSEquipSlot>(a_leftHand ? "LeftHand" : "RightHand");
				IsUsableEquipSlot(fromEditorID)) {
				return fromEditorID;
			}

			if (const auto* fromFormID =
					RE::TESForm::LookupByID<RE::BGSEquipSlot>(a_leftHand ? kLeftHandSlotFormID : kRightHandSlotFormID);
				IsUsableEquipSlot(fromFormID)) {
				return fromFormID;
			}

			logger::warn("GetSlotForHand: could not resolve the {} hand equip slot",
				a_leftHand ? "left" : "right");
			return nullptr;
		}
	}

	const RE::BGSEquipSlot* GetRightHandSlot()
	{
		static const auto* slot = ResolveHandSlot(false);
		return slot;
	}

	const RE::BGSEquipSlot* GetLeftHandSlot()
	{
		static const auto* slot = ResolveHandSlot(true);
		return slot;
	}

	const RE::BGSEquipSlot* GetSlotForHand(Hand a_hand)
	{
		return a_hand == Hand::kLeft ? GetLeftHandSlot() : GetRightHandSlot();
	}

	bool IsLeftHandSlot(const RE::BGSEquipSlot* a_slot) noexcept
	{
		const auto* left = GetLeftHandSlot();
		return a_slot && left && a_slot == left;
	}

	bool IsRightHandSlot(const RE::BGSEquipSlot* a_slot) noexcept
	{
		const auto* right = GetRightHandSlot();
		return a_slot && right && a_slot == right;
	}

	bool IsHandSlot(const RE::BGSEquipSlot* a_slot) noexcept
	{
		return IsLeftHandSlot(a_slot) || IsRightHandSlot(a_slot);
	}

	namespace
	{
		[[nodiscard]] bool IsTwoHandedWeapon(const RE::TESObjectWEAP& a_weap)
		{
			return a_weap.IsTwoHandedSword() || a_weap.IsTwoHandedAxe() || a_weap.IsBow() || a_weap.IsCrossbow();
		}
	}

	bool IsTwoHandedObject(RE::TESBoundObject* a_object)
	{
		if (!a_object) {
			return false;
		}
		if (auto* weap = a_object->As<RE::TESObjectWEAP>()) {
			return IsTwoHandedWeapon(*weap);
		}
		if (a_object->GetFormType() == RE::FormType::Scroll) {
			auto* scroll = a_object->As<RE::ScrollItem>();
			return scroll && scroll->IsTwoHanded();
		}
		return false;
	}

	// Returns the equip slot for a hand item, or null when the item is not a hand item.
	// Callers must treat null as "not a hand item" and must never forward it to
	// ActorEquipManager::EquipObject, which would silently target the right hand instead.
	const RE::BGSEquipSlot* GetSlotForObject(RE::TESBoundObject* a_object, Hand a_hand)
	{
		if (!a_object) {
			return nullptr;
		}
		if (a_object->GetFormType() == RE::FormType::Light) {
			return GetLeftHandSlot();
		}
		if (a_object->IsWeapon() || a_object->GetFormType() == RE::FormType::Scroll) {
			if (IsTwoHandedObject(a_object)) {
				return GetRightHandSlot();
			}
			return GetSlotForHand(a_hand);
		}
		return nullptr;
	}

	RE::ExtraDataList* PickUniqueByPredicate(
		RE::InventoryEntryData* a_entry,
		const std::function<bool(const RE::ExtraDataList&)>& a_pred)
	{
		if (!a_entry || !a_entry->extraLists || a_entry->extraLists->empty()) {
			return nullptr;
		}
		RE::ExtraDataList* picked = nullptr;
		std::uint32_t matches = 0;
		for (auto* x : *a_entry->extraLists) {
			if (!x) {
				continue;
			}
			if (!a_pred(*x)) {
				continue;
			}
			picked = x;
			++matches;
			if (matches > 1) {
				return nullptr;
			}
		}
		return (matches == 1) ? picked : nullptr;
	}

	RE::ExtraDataList* PickFirstByPredicate(
		RE::InventoryEntryData* a_entry,
		const std::function<bool(const RE::ExtraDataList&)>& a_pred)
	{
		if (!a_entry || !a_entry->extraLists || a_entry->extraLists->empty()) {
			return nullptr;
		}
		for (auto* x : *a_entry->extraLists) {
			if (!x) {
				continue;
			}
			if (!a_pred(*x)) {
				continue;
			}
			return x;
		}
		return nullptr;
	}

	RE::ExtraDataList* PickUniqueNotWorn(RE::InventoryEntryData* a_entry)
	{
		return PickUniqueByPredicate(a_entry, [](const RE::ExtraDataList& x) {
			return !HasWornExtra(x);
		});
	}

	RE::ExtraDataList* PickFirstNotWorn(RE::InventoryEntryData* a_entry)
	{
		return PickFirstByPredicate(a_entry, [](const RE::ExtraDataList& x) {
			return !HasWornExtra(x);
		});
	}

	RE::ExtraDataList* PickUniqueWornAny(RE::InventoryEntryData* a_entry)
	{
		return PickUniqueByPredicate(a_entry, [](const RE::ExtraDataList& x) {
			return HasWornExtra(x);
		});
	}

	RE::ExtraDataList* PickFirstWornAny(RE::InventoryEntryData* a_entry)
	{
		return PickFirstByPredicate(a_entry, [](const RE::ExtraDataList& x) {
			return HasWornExtra(x);
		});
	}

	RE::ExtraDataList* PickUniqueWornForHand(RE::InventoryEntryData* a_entry, Hand a_hand)
	{
		return PickUniqueByPredicate(a_entry, [&](const RE::ExtraDataList& x) {
			return a_hand == Hand::kLeft ? HasWornLeftExtra(x) : HasWornRightExtra(x);
		});
	}

	RE::ExtraDataList* PickFirstWornForHand(RE::InventoryEntryData* a_entry, Hand a_hand)
	{
		return PickFirstByPredicate(a_entry, [&](const RE::ExtraDataList& x) {
			return a_hand == Hand::kLeft ? HasWornLeftExtra(x) : HasWornRightExtra(x);
		});
	}
}
