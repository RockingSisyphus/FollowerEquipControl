// Maps ExtraDataList worn extras to EquipMode EquipState values.

#pragma once

#include "InstanceSelection.h"

namespace RE
{
	class ExtraDataList;
}

namespace FEC::EquipMode::Selection
{
	[[nodiscard]] EquipState GetEquipStateFromExtra(const RE::ExtraDataList& a_list);
}
