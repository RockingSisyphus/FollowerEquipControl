#include "InstanceSignature.h"

#include <cmath>

#include "RE/E/ExtraCharge.h"
#include "RE/E/ExtraEnchantment.h"
#include "RE/E/ExtraHealth.h"
#include "RE/E/ExtraPoison.h"
#include "RE/E/ExtraTextDisplayData.h"
#include "RE/E/ExtraWorn.h"
#include "RE/E/ExtraWornLeft.h"

namespace FEC
{
	namespace
	{
		[[nodiscard]] InstanceSignature::EquipState GetEquipStateFromExtra(const RE::ExtraDataList& a_list)
		{
			if (a_list.HasType<RE::ExtraWornLeft>()) {
				return InstanceSignature::EquipState::kWornLeft;
			}
			if (a_list.HasType<RE::ExtraWorn>()) {
				return InstanceSignature::EquipState::kWornRight;
			}
			return InstanceSignature::EquipState::kNotWorn;
		}
	}

	bool InstanceSignature::IsMeaningful() const noexcept
	{
		return hasTextDisplayData || hasPoisonExtra || hasEnchantmentExtra || hasChargeExtra || hasHealthExtra;
	}

	bool InstanceSignature::HasStableIdentity() const noexcept
	{
		// Keep this in sync with SignatureResolve's stable-identity subset; volatile extras are intentionally excluded.
		return hasTextDisplayData || hasEnchantmentExtra;
	}

	InstanceSignature BuildInstanceSignature(const RE::ExtraDataList& a_extraList, RE::TESBoundObject* a_baseObject)
	{
		InstanceSignature sig{};

		if (auto* xText = a_extraList.GetByType<RE::ExtraTextDisplayData>()) {
			sig.hasTextDisplayData = true;
			sig.hasTemperFactor = true;
			sig.temperFactor = xText->temperFactor;

			const bool hasCustom = xText->ownerInstance == RE::ExtraTextDisplayData::DisplayDataType::kCustomName &&
			                      xText->displayName.c_str() && *xText->displayName.c_str();
			sig.hasCustomName = hasCustom;
			if (hasCustom) {
				sig.customName = xText->displayName.c_str();
			}
		}

		if (auto* xPoison = a_extraList.GetByType<RE::ExtraPoison>()) {
			sig.hasPoisonExtra = true;
			sig.hasPoisonCount = true;
			sig.poisonCount = xPoison->count;
			if (xPoison->poison) {
				sig.hasPoisonFormID = true;
				sig.poisonFormID = xPoison->poison->GetFormID();
			}
		}

		if (auto* xEnch = a_extraList.GetByType<RE::ExtraEnchantment>()) {
			sig.hasEnchantmentExtra = true;
			sig.hasEnchantmentCharge = true;
			sig.enchantmentCharge = xEnch->charge;
			sig.hasRemoveOnUnequip = true;
			sig.removeOnUnequip = xEnch->removeOnUnequip;
			if (xEnch->enchantment) {
				sig.hasEnchantmentFormID = true;
				sig.enchantmentFormID = xEnch->enchantment->GetFormID();
			}
		}

		if (auto* xCharge = a_extraList.GetByType<RE::ExtraCharge>()) {
			sig.hasChargeExtra = true;
			sig.charge = xCharge->charge;
		}

		if (auto* xHealth = a_extraList.GetByType<RE::ExtraHealth>()) {
			sig.hasHealthExtra = true;
			sig.health = xHealth->health;
		}

		// We intentionally do not include ownership/hotkey in the signature.
		// They are volatile across trade/transfer and are not stable identity constraints.

		sig.equipState = GetEquipStateFromExtra(a_extraList);

		(void)a_baseObject;
		return sig;
	}

	InstanceSignature NormalizeStableIdentity(const InstanceSignature& a_src)
	{
		InstanceSignature out{};
		out.equipState = InstanceSignature::EquipState::kUnknown;
		out.capturedFromNonUniqueRow = false;

		// Stable identity matches combat-equip preference identity: text/custom name,
		// temper factor, and enchantment data.
		out.hasTextDisplayData = a_src.hasTextDisplayData;
		out.hasCustomName = a_src.hasCustomName;
		if (out.hasCustomName) {
			out.customName = a_src.customName;
		}
		if (out.hasTextDisplayData) {
			out.hasTemperFactor = true;
			out.temperFactor = a_src.temperFactor;
		}

		out.hasEnchantmentExtra = a_src.hasEnchantmentExtra;
		out.hasEnchantmentFormID = a_src.hasEnchantmentFormID;
		out.enchantmentFormID = a_src.enchantmentFormID;
		out.hasEnchantmentCharge = a_src.hasEnchantmentCharge;
		out.enchantmentCharge = a_src.enchantmentCharge;

		// Explicitly ignore volatile extras.
		out.hasPoisonExtra = false;
		out.hasPoisonFormID = false;
		out.poisonFormID = 0;
		out.hasPoisonCount = false;
		out.poisonCount = 0;
		out.hasRemoveOnUnequip = false;
		out.removeOnUnequip = false;
		out.hasChargeExtra = false;
		out.charge = 0.0f;
		out.hasHealthExtra = false;
		out.health = 0.0f;

		return out;
	}

	bool StableIdentityEquals(const InstanceSignature& a_lhs, const InstanceSignature& a_rhs) noexcept
	{
		constexpr float kFloatEpsilon = 1e-4f;

		if (a_lhs.hasTextDisplayData != a_rhs.hasTextDisplayData) {
			return false;
		}
		if (a_lhs.hasCustomName != a_rhs.hasCustomName) {
			return false;
		}
		if (a_lhs.hasCustomName && a_lhs.customName != a_rhs.customName) {
			return false;
		}
		if (a_lhs.hasTemperFactor != a_rhs.hasTemperFactor) {
			return false;
		}
		if (a_lhs.hasTemperFactor && std::fabs(a_lhs.temperFactor - a_rhs.temperFactor) > kFloatEpsilon) {
			return false;
		}

		if (a_lhs.hasEnchantmentExtra != a_rhs.hasEnchantmentExtra) {
			return false;
		}
		if (a_lhs.hasEnchantmentFormID != a_rhs.hasEnchantmentFormID) {
			return false;
		}
		if (a_lhs.hasEnchantmentFormID && a_lhs.enchantmentFormID != a_rhs.enchantmentFormID) {
			return false;
		}
		if (a_lhs.hasEnchantmentCharge != a_rhs.hasEnchantmentCharge) {
			return false;
		}
		if (a_lhs.hasEnchantmentCharge && a_lhs.enchantmentCharge != a_rhs.enchantmentCharge) {
			return false;
		}

		return true;
	}

}
