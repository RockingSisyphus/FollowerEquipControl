// Shared helpers for combat equip override classification and forced unequip.

#pragma once

#include "PCH.h"

#include "CombatEquipOverride.h"
#include "WeaponBound.h"

namespace FEC::CombatEquipOverride::Util
{
	[[nodiscard]] inline const RE::BGSEquipSlot* GetDefaultEquipSlot(RE::DEFAULT_OBJECT a_id)
	{
		auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
		if (!dom) {
			return nullptr;
		}
		return dom->GetObject<RE::BGSEquipSlot>(a_id);
	}

	[[nodiscard]] inline const RE::BGSEquipSlot* GetLeftHandSlot()
	{
		return GetDefaultEquipSlot(RE::DEFAULT_OBJECT::kLeftHandEquip);
	}

	[[nodiscard]] inline const RE::BGSEquipSlot* GetRightHandSlot()
	{
		return GetDefaultEquipSlot(RE::DEFAULT_OBJECT::kRightHandEquip);
	}

	[[nodiscard]] inline bool IsLeftHandSlot(const RE::BGSEquipSlot* a_slot)
	{
		const auto* left = GetLeftHandSlot();
		return a_slot && left && a_slot == left;
	}

	[[nodiscard]] inline bool IsRightHandSlot(const RE::BGSEquipSlot* a_slot)
	{
		const auto* right = GetRightHandSlot();
		return a_slot && right && a_slot == right;
	}

	[[nodiscard]] inline EquipClass ClassifyEquipClass(RE::TESBoundObject* a_object)
	{
		if (!a_object) {
			return EquipClass::kUnknown;
		}

		if (a_object->GetFormType() == RE::FormType::Ammo) {
			return EquipClass::kAmmo;
		}

		if (auto* weap = a_object->As<RE::TESObjectWEAP>()) {
			switch (weap->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandSword:
			case RE::WEAPON_TYPE::kOneHandDagger:
			case RE::WEAPON_TYPE::kOneHandAxe:
			case RE::WEAPON_TYPE::kOneHandMace:
			case RE::WEAPON_TYPE::kTwoHandSword:
			case RE::WEAPON_TYPE::kTwoHandAxe:
				return EquipClass::kMelee;
			default:
				break;
			}
		}

		if (auto* armo = a_object->As<RE::TESObjectARMO>()) {
			if (armo->IsShield()) {
				return EquipClass::kMelee;
			}
		}

		return EquipClass::kUnknown;
	}

	[[nodiscard]] inline bool IsNonShieldArmor(RE::TESBoundObject* a_object)
	{
		auto* armor = a_object ? a_object->As<RE::TESObjectARMO>() : nullptr;
		return armor && !armor->IsShield();
	}

	[[nodiscard]] inline bool IsMeleeLeftEligible(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}

		auto* leftForm = a_actor->GetEquippedObject(true);
		if (!leftForm) {
			return true;
		}

		auto* leftObj = leftForm->As<RE::TESBoundObject>();
		if (!leftObj) {
			return false;
		}

		return ClassifyEquipClass(leftObj) == EquipClass::kMelee;
	}

	inline void ForceUnequipLeft(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}

		auto* leftForm = a_actor->GetEquippedObject(true);
		auto* leftObj = leftForm ? leftForm->As<RE::TESBoundObject>() : nullptr;
		if (!leftObj) {
			return;
		}

		if (leftObj->IsWeapon()) {
			auto* weap = leftObj->As<RE::TESObjectWEAP>();
			// Bound weapons are transient; forcing unequip can end the conjuration.
			if (WeaponBound::IsBoundWeapon(weap)) {
				return;
			}
		} else if (auto* armor = leftObj->As<RE::TESObjectARMO>(); armor && armor->IsShield()) {
		} else {
			return;
		}

		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return;
		}

		equipManager->UnequipObject(
			a_actor,
			leftObj,
			nullptr,
			1,
			GetLeftHandSlot(),
			true,
			false,
			true,
			false,
			nullptr);
	}

	inline void ForceUnequipRight(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}

		auto* rightForm = a_actor->GetEquippedObject(false);
		auto* rightObj = rightForm ? rightForm->As<RE::TESBoundObject>() : nullptr;
		if (!rightObj) {
			return;
		}

		if (rightObj->IsWeapon()) {
			auto* weap = rightObj->As<RE::TESObjectWEAP>();
			// Bound weapons are transient; forcing unequip can end the conjuration.
			if (WeaponBound::IsBoundWeapon(weap)) {
				return;
			}
		} else {
			return;
		}

		auto* equipManager = RE::ActorEquipManager::GetSingleton();
		if (!equipManager) {
			return;
		}

		equipManager->UnequipObject(
			a_actor,
			rightObj,
			nullptr,
			1,
			GetRightHandSlot(),
			true,
			false,
			true,
			false,
			nullptr);
	}

	[[nodiscard]] inline bool IsRangedWeaponEquipped(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return false;
		}
		auto* rightForm = a_actor->GetEquippedObject(false);
		auto* rightWeap = rightForm ? rightForm->As<RE::TESObjectWEAP>() : nullptr;
		if (!rightWeap) {
			return false;
		}
		switch (rightWeap->GetWeaponType()) {
		case RE::WEAPON_TYPE::kBow:
		case RE::WEAPON_TYPE::kCrossbow:
			return true;
		default:
			return false;
		}
	}
}
