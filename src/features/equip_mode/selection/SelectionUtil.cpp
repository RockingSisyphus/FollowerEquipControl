#include "SelectionUtil.h"

#include <RE/E/ExtraWorn.h>
#include <RE/E/ExtraWornLeft.h>

namespace FEC::EquipMode::Selection
{
	EquipState GetEquipStateFromExtra(const RE::ExtraDataList& a_list)
	{
		if (a_list.HasType<RE::ExtraWornLeft>()) {
			return EquipState::kWornLeft;
		}
		if (a_list.HasType<RE::ExtraWorn>()) {
			return EquipState::kWornRight;
		}
		return EquipState::kNotWorn;
	}
}
