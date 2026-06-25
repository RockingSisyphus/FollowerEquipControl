#include "StrictPickUtil.h"

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

	const RE::BGSEquipSlot* GetRightHandSlot()
	{
		static const auto* slot = RE::TESForm::LookupByEditorID<RE::BGSEquipSlot>("RightHand");
		return slot;
	}

	const RE::BGSEquipSlot* GetLeftHandSlot()
	{
		static const auto* slot = RE::TESForm::LookupByEditorID<RE::BGSEquipSlot>("LeftHand");
		return slot;
	}

	const RE::BGSEquipSlot* GetSlotForHand(Hand a_hand)
	{
		return a_hand == Hand::kLeft ? GetLeftHandSlot() : GetRightHandSlot();
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
