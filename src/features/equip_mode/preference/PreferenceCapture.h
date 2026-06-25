#pragma once

#include "PCH.h"

namespace RE
{
	class Actor;
	class ExtraDataList;
	class TESBoundObject;
}

namespace FEC::EquipMode::Preference
{
	void CaptureEquip(RE::Actor* a_actor, RE::TESBoundObject* a_object, bool a_leftHand, RE::ExtraDataList* a_xList, bool a_hasSelection);
	void CaptureUnequip(RE::Actor* a_actor, RE::TESBoundObject* a_object, bool a_leftHand);
}
