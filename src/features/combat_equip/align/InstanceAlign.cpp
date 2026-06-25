#include "InstanceAlign.h"

#include "CombatEquipOverrideUtil.h"

#include "CombatEquipPreference.h"
#include "SignatureResolve.h"
#include "WeaponBound.h"

namespace FEC::CombatEquipOverride::InstanceAlign
{
	namespace
	{
		using Cat = CombatEquipPreference::Category;

		[[nodiscard]] std::optional<Cat> TryGetCategoryForAttempt(
			RE::TESBoundObject* a_object,
			const RE::BGSEquipSlot* a_slot) noexcept
		{
			if (!a_object) {
				return std::nullopt;
			}

			if (a_object->GetFormType() == RE::FormType::Scroll) {
				if (auto* scroll = a_object->As<RE::ScrollItem>(); scroll && scroll->IsTwoHanded()) {
					return Cat::kScrollBoth;
				}
				if (Util::IsLeftHandSlot(a_slot)) {
					return Cat::kScrollLeft;
				}
				if (Util::IsRightHandSlot(a_slot)) {
					return Cat::kScrollRight;
				}
				return std::nullopt;
			}

			if (auto* ammo = a_object->As<RE::TESAmmo>()) {
				return ammo->IsBolt() ? Cat::kBolt : Cat::kArrow;
			}

			if (auto* armo = a_object->As<RE::TESObjectARMO>()) {
				if (armo->IsShield()) {
					return Cat::kShieldLeft;
				}
				return std::nullopt;
			}

			auto* weap = a_object->As<RE::TESObjectWEAP>();
			if (!weap) {
				return std::nullopt;
			}

			if (weap->IsStaff()) {
				if (Util::IsLeftHandSlot(a_slot)) {
					return Cat::kStaffLeft;
				}
				if (Util::IsRightHandSlot(a_slot)) {
					return Cat::kStaffRight;
				}
				return std::nullopt;
			}

			if (weap->IsBow()) {
				return Cat::kBow;
			}
			if (weap->IsCrossbow()) {
				return Cat::kCrossbow;
			}

			if (weap->IsTwoHandedSword() || weap->IsTwoHandedAxe()) {
				return Cat::kTwoHand;
			}

			if (Util::IsLeftHandSlot(a_slot)) {
				return Cat::kOneHandLeft;
			}
			if (Util::IsRightHandSlot(a_slot)) {
				return Cat::kOneHandRight;
			}

			return std::nullopt;
		}

		[[nodiscard]] std::optional<InstanceSignature::EquipState> PreferWornStateFromSlot(const RE::BGSEquipSlot* a_slot) noexcept
		{
			if (Util::IsLeftHandSlot(a_slot)) {
				return InstanceSignature::EquipState::kWornLeft;
			}
			if (Util::IsRightHandSlot(a_slot)) {
				return InstanceSignature::EquipState::kWornRight;
			}
			return std::nullopt;
		}
	}

	std::optional<Decision> DecideInstanceAlign(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count,
		bool a_aiDriven,
		bool a_drawn)
	{
		(void)a_aiDriven;
		(void)a_drawn;

		if (!a_actor || !a_object) {
			return std::nullopt;
		}

		if (WeaponBound::IsWeaponAndBound(a_object)) {
			return std::nullopt;
		}

		const auto actorID = a_actor->GetFormID();
		if (actorID == 0) {
			return std::nullopt;
		}

		const auto cat = TryGetCategoryForAttempt(a_object, a_slot);
		if (!cat.has_value()) {
			return std::nullopt;
		}

		auto entry = CombatEquipPreference::GetEntry(actorID, *cat);
		if (!entry.has_value() || entry->baseObjectID == 0) {
			return std::nullopt;
		}

		// Only align instances when the attempted base item already matches the preferred base.
		if (entry->baseObjectID != a_object->GetFormID()) {
			return std::nullopt;
		}

		// No identity means nothing to align.
		if (!entry->signature.HasStableIdentity()) {
			return std::nullopt;
		}

		const auto preferWornState = PreferWornStateFromSlot(a_slot);
		const auto resolved = SignatureResolve::Resolve(
			a_actor,
			a_object,
			entry->signature,
			preferWornState,
			SignatureResolve::Policy::kIdentityThenAnyBase);
		if (!resolved.HasXList()) {
			return std::nullopt;
		}
		auto* preferredXList = resolved.xList;

		if (preferredXList == a_extraData) {
			return std::nullopt;
		}

		Decision d;
		d.action = DecisionAction::kSwapToPreferred;
		d.preferred = PreferredItem{ a_object, preferredXList };
		d.slotToUse = a_slot;
		d.countToUse = (a_count > 0 ? a_count : 1);
		d.queueEquip = false;
		d.forceEquip = false;
		d.applyNow = true;
		d.reason = "instance_align";
		return d;
	}
}
