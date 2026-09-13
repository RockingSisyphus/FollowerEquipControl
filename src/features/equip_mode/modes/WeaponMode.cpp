#include "WeaponMode.h"

#include "InventoryUtil.h"
#include "StrictPickUtil.h"

namespace FEC::EquipMode::Modes
{
	std::optional<Core::StrictPick> WeaponMode::TryBuildStrictPick(
		RE::InventoryEntryData* a_entry,
		RE::TESBoundObject* a_object,
		Core::Hand a_hand,
		bool a_equipOnly,
		RE::Actor* a_actor,
		RE::ExtraDataList* a_preferredXList)
	{
		if (!a_entry || !a_object || !a_object->IsWeapon()) {
			return std::nullopt;
		}
		if (!a_entry->extraLists || a_entry->extraLists->empty()) {
			return std::nullopt;
		}

		Core::StrictPick out{};
		out.object = a_object;
		out.slot = Core::GetSlotForObject(a_object, a_hand);

		Core::Hand effectiveHand = a_hand;
		if (Core::IsLeftHandSlot(out.slot)) {
			effectiveHand = Core::Hand::kLeft;
		} else if (Core::IsRightHandSlot(out.slot)) {
			effectiveHand = Core::Hand::kRight;
		}

		// Two-handers: always treat as right-hand intent.
		if (Core::IsTwoHandedObject(a_object)) {
			effectiveHand = Core::Hand::kRight;
			out.slot = Core::GetSlotForHand(Core::Hand::kRight);
		}

		const bool isHandSlotItem = Core::IsHandSlot(out.slot);
		if (!isHandSlotItem) {
			return std::nullopt;
		}

		if (a_preferredXList) {
			out.xList = a_preferredXList;
			const bool wornR = Core::HasWornRightExtra(*a_preferredXList);
			const bool wornL = Core::HasWornLeftExtra(*a_preferredXList);
			if (!a_equipOnly) {
				if (effectiveHand == Core::Hand::kRight && wornR) { out.unequip = true; return out; }
				if (effectiveHand == Core::Hand::kLeft  && wornL) { out.unequip = true; return out; }
				// If the selected instance is worn in the other hand, dual-wield from a base copy or swap-move it.
				if ((effectiveHand == Core::Hand::kRight && wornL) || (effectiveHand == Core::Hand::kLeft && wornR)) {
					// Do not substitute a different not-worn xList for an explicitly selected worn instance.
					// Base copies without xLists are still valid for dual-wield.
					// Use GetTotalCount, not countDelta; template items can have countDelta == 0.
					std::int32_t xListCount = 0;
					for (auto* x : *a_entry->extraLists) { if (x) ++xListCount; }
					const std::int32_t totalCount = a_actor
						? InventoryUtil::GetTotalCount(a_actor, a_object)
						: a_entry->countDelta;
					if (totalCount > xListCount) {
						out.baseFallback = true;
						out.xList = nullptr;
						out.unequip = false;
						return out;
					}
					if (effectiveHand == Core::Hand::kRight) {
						out.swapMove = true; out.fromSlot = Core::GetLeftHandSlot(); return out;
					}
					out.swapMove = true; out.fromSlot = Core::GetRightHandSlot(); return out;
				}
			}
			out.unequip = false;
			return out;
		}

		if (!a_equipOnly) {
			// Unequip only if the selected row's instance is worn in the intended hand.
			out.xList = Core::PickUniqueWornForHand(a_entry, effectiveHand);
			if (!out.xList) {
				out.xList = Core::PickFirstWornForHand(a_entry, effectiveHand);
			}
			if (out.xList) {
				out.unequip = true;
				return out;
			}
		}

		out.xList = Core::PickUniqueNotWorn(a_entry);
		if (!out.xList) {
			out.xList = Core::PickFirstNotWorn(a_entry);
		}
		if (out.xList) {
			out.unequip = false;
			return out;
		}

		// Base-copy fallback: extra inventory count can exist without a manifested xList.
		// Use GetTotalCount, not countDelta: template items can have countDelta == 0,
		// and post-synth-equip state can reduce countDelta while real count is unchanged.
		// Fall back to countDelta only when no actor is available.
		{
			std::int32_t xListCount = 0;
			for (auto* x : *a_entry->extraLists) {
				if (x) ++xListCount;
			}
			const std::int32_t totalCount = a_actor
				? InventoryUtil::GetTotalCount(a_actor, a_object)
				: a_entry->countDelta;
			if (totalCount > xListCount) {
				out.baseFallback = true;
				out.xList = nullptr;
				out.unequip = false;
				return out;
			}
		}

		// If the only identifiable instance in this row is worn in the other hand, strictly move it.
		const Core::Hand otherHand = (effectiveHand == Core::Hand::kLeft) ? Core::Hand::kRight : Core::Hand::kLeft;
		out.xList = Core::PickUniqueWornForHand(a_entry, otherHand);
		if (!out.xList) {
			out.xList = Core::PickFirstWornForHand(a_entry, otherHand);
		}
		if (out.xList) {
			out.swapMove = true;
			out.fromSlot = Core::GetSlotForHand(otherHand);
			return out;
		}

		return std::nullopt;
	}
}
