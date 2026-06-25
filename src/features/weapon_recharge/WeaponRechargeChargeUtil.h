// Reads current and maximum charge for enchanted weapon inventory instances.
// Equipped weapons can use the actor's equipped-charge cache while worn.

#pragma once

#include "PCH.h"

#include <RE/A/Actor.h>
#include <RE/A/ActorValueOwner.h>
#include <RE/E/ExtraCharge.h>
#include <RE/E/ExtraEnchantment.h>
#include <RE/E/ExtraWorn.h>
#include <RE/E/ExtraWornLeft.h>

#include <algorithm>

namespace FEC::WeaponRecharge::ChargeUtil
{
	// ExtraEnchantment::charge overrides TESEnchantableForm::amountofEnchantment for max charge.
	// Missing ExtraCharge means the instance is treated as fully charged.
	[[nodiscard]] inline bool TryReadChargeAbs(RE::TESBoundObject* a_object, RE::ExtraDataList* a_xList, double& a_outCurrent, double& a_outMax)
	{
		a_outCurrent = 0.0;
		a_outMax = 0.0;

		if (!a_object || !a_xList) {
			return false;
		}

		auto* ench = a_object->As<RE::TESEnchantableForm>();
		if (!ench) {
			return false;
		}

		// Player-enchanted items store their enchantment on ExtraEnchantment, not the base form.
		if (auto* xEnch = a_xList->GetByType<RE::ExtraEnchantment>(); xEnch && xEnch->enchantment && xEnch->charge != 0) {
			a_outMax = static_cast<double>(xEnch->charge);
		} else if (ench->formEnchanting && ench->amountofEnchantment != 0) {
			a_outMax = static_cast<double>(ench->amountofEnchantment);
		}

		if (a_outMax <= 0.0) {
			return false;
		}

		if (auto* xCharge = a_xList->GetByType<RE::ExtraCharge>()) {
			a_outCurrent = static_cast<double>(xCharge->charge);
		} else {
			a_outCurrent = a_outMax;
		}

		a_outCurrent = std::clamp(a_outCurrent, 0.0, a_outMax);
		return true;
	}

	// While worn, RightItemCharge / LeftItemCharge can be the authoritative current value.
	[[nodiscard]] inline bool TryReadChargeAbsPreferEquippedCache(
		RE::Actor* a_owner,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_xList,
		double& a_outCurrent,
		double& a_outMax)
	{
		if (!TryReadChargeAbs(a_object, a_xList, a_outCurrent, a_outMax)) {
			return false;
		}
		if (!a_owner || !a_xList) {
			return true;
		}
		const bool wornR = a_xList->HasType<RE::ExtraWorn>();
		const bool wornL = a_xList->HasType<RE::ExtraWornLeft>();
		if (!wornR && !wornL) {
			return true;
		}

		auto* avOwner = a_owner->AsActorValueOwner();
		if (!avOwner) {
			return true;
		}

		float cached = -1.0f;
		if (wornR) {
			cached = avOwner->GetActorValue(RE::ActorValue::kRightItemCharge);
		}
		if (wornL) {
			const float left = avOwner->GetActorValue(RE::ActorValue::kLeftItemCharge);
			cached = (cached < 0.0f) ? left : std::max(cached, left);
		}

		if (cached >= 0.0f) {
			a_outCurrent = std::clamp(static_cast<double>(cached), 0.0, a_outMax);
		}
		return true;
	}
}
