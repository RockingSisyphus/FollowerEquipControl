#include "AmmoMode.h"

#include "StrictPickUtil.h"

namespace FEC::EquipMode::Modes
{
	std::optional<Core::StrictPick> AmmoMode::TryBuildStrictPick(
		RE::InventoryEntryData* a_entry,
		RE::TESBoundObject* a_object,
		Core::Hand a_hand,
		bool a_equipOnly,
		RE::ExtraDataList* a_preferredXList)
	{
		if (!a_entry || !a_object || a_object->GetFormType() != RE::FormType::Ammo) {
			return std::nullopt;
		}
		if (!a_entry->extraLists || a_entry->extraLists->empty()) {
			return std::nullopt;
		}

		Core::StrictPick out{};
		out.object = a_object;
		out.slot = Core::GetSlotForObject(a_object, a_hand);

		if (a_preferredXList) {
			out.xList = a_preferredXList;
			out.unequip = (!a_equipOnly && Core::HasWornExtra(*a_preferredXList));
			return out;
		}

		// Ammo is not hand-specific; treat worn ammo as equipped for either hand.
		if (!a_equipOnly) {
			out.xList = Core::PickUniqueWornAny(a_entry);
			if (!out.xList) {
				out.xList = Core::PickFirstWornAny(a_entry);
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

		return std::nullopt;
	}
}
