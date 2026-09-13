#include "TorchMode.h"

#include "StrictPickUtil.h"

namespace FEC::EquipMode::Modes
{
	std::optional<Core::StrictPick> TorchMode::TryBuildStrictPick(
		RE::InventoryEntryData* a_entry,
		RE::TESBoundObject* a_object,
		Core::Hand a_hand,
		bool a_equipOnly,
		RE::ExtraDataList* a_preferredXList)
	{
		if (!a_entry || !a_object || a_object->GetFormType() != RE::FormType::Light) {
			return std::nullopt;
		}
		if (!a_entry->extraLists || a_entry->extraLists->empty()) {
			return std::nullopt;
		}

		Core::StrictPick out{};
		out.object = a_object;
		out.slot = Core::GetSlotForObject(a_object, a_hand);

		// Torches are effectively left-hand in Skyrim.
		const Core::Hand effectiveHand = Core::Hand::kLeft;

		const bool isHandSlotItem = Core::IsHandSlot(out.slot);
		if (!isHandSlotItem) {
			return std::nullopt;
		}

		if (a_preferredXList) {
			out.xList = a_preferredXList;
			const bool wornL = Core::HasWornLeftExtra(*a_preferredXList);
			const bool wornR = Core::HasWornRightExtra(*a_preferredXList);
			if (!a_equipOnly) {
				if (wornL) { out.unequip = true; return out; }
				if (wornR) { out.swapMove = true; out.fromSlot = Core::GetRightHandSlot(); return out; }
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

		// If the only instance in this row is worn in the other hand, allow a strict move.
		out.xList = Core::PickUniqueWornForHand(a_entry, Core::Hand::kRight);
		if (!out.xList) {
			out.xList = Core::PickFirstWornForHand(a_entry, Core::Hand::kRight);
		}
		if (out.xList) {
			out.swapMove = true;
			out.fromSlot = Core::GetSlotForHand(Core::Hand::kRight);
			return out;
		}

		return std::nullopt;
	}
}
