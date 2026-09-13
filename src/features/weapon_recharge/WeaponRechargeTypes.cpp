#include "WeaponRechargeTypes.h"

namespace FEC::WeaponRecharge
{
	double GetSoulRechargeValue(RE::SOUL_LEVEL a_soul)
	{
		// Vanilla-ish absolute charge points by soul size.
		switch (a_soul) {
		case RE::SOUL_LEVEL::kPetty:
			return 250.0;
		case RE::SOUL_LEVEL::kLesser:
			return 500.0;
		case RE::SOUL_LEVEL::kCommon:
			return 1000.0;
		case RE::SOUL_LEVEL::kGreater:
			return 2000.0;
		case RE::SOUL_LEVEL::kGrand:
			return 3000.0;
		default:
			return 0.0;
		}
	}
}
