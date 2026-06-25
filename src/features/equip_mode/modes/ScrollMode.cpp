#include "ScrollMode.h"

#include "InventoryUtil.h"
#include "StrictPickUtil.h"

namespace FEC::EquipMode::Modes
{
	std::optional<Core::StrictPick> ScrollMode::TryBuildStrictPick(
		RE::InventoryEntryData* a_entry,
		RE::TESBoundObject* a_object,
		Core::Hand a_hand,
		bool a_equipOnly,
		RE::Actor* a_actor,
		RE::ExtraDataList* a_preferredXList)
	{
		if (!a_entry || !a_object || a_object->GetFormType() != RE::FormType::Scroll) {
			return std::nullopt;
		}
		if (!a_entry->extraLists || a_entry->extraLists->empty()) {
			return std::nullopt;
		}

		Core::StrictPick out{};
		out.object = a_object;
		out.slot = Core::GetSlotForObject(a_object, a_hand);

		Core::Hand effectiveHand = a_hand;
		if (out.slot == Core::GetLeftHandSlot()) {
			effectiveHand = Core::Hand::kLeft;
		} else if (out.slot == Core::GetRightHandSlot()) {
			effectiveHand = Core::Hand::kRight;
		}

		// Two-handers: always treat as right-hand intent.
		if (Core::IsTwoHandedObject(a_object)) {
			effectiveHand = Core::Hand::kRight;
			out.slot = Core::GetSlotForHand(Core::Hand::kRight);
		}

		const bool isHandSlotItem = (out.slot == Core::GetLeftHandSlot()) || (out.slot == Core::GetRightHandSlot());
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
				// Worn in the other hand: prefer dual-wield via a free not-worn instance or base copy.
				if ((effectiveHand == Core::Hand::kRight && wornL) || (effectiveHand == Core::Hand::kLeft && wornR)) {
					RE::ExtraDataList* notWorn = Core::PickFirstNotWorn(a_entry);
					if (notWorn) {
						out.xList = notWorn;
						out.unequip = false;
						return out;
					}
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

		// Base-copy fallback: extra item count can exist without a manifested xList.
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
