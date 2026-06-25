// Opens the SkyUI ItemCard soul gem picker used by the weapon recharge flow.

#pragma once

#include "PCH.h"

#include "WeaponRechargeTypes.h"

#include <functional>
#include <vector>

namespace FEC::WeaponRecharge
{
	class PickerUI
	{
	public:
		using PickCallback = std::function<void(RE::TESSoulGem* a_gem, SoulGemSource a_source, RE::SOUL_LEVEL a_soul)>;
		using CancelCallback = std::function<void()>;

		// Uses SkyUI ItemCard ICT_LIST; menu gating must already be checked.
		[[nodiscard]] static bool Open(
			RE::ContainerMenu* a_menu,
			const std::vector<SoulGemOption>& a_options,
			double a_selectedCurrentAbs,
			double a_selectedMaxAbs,
			PickCallback a_onPick,
			CancelCallback a_onCancel);

		// Creates native handlers used by the ActionScript picker callbacks.
		static void EnsureHandlersCreated();
	};
}
