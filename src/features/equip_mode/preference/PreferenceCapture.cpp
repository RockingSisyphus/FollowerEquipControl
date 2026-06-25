#include "PreferenceCapture.h"

#include "ActorScope.h"
#include "CombatEquipPreference.h"
#include "CombatEquipPreferencePolicy.h"
#include "ContainerMenuUtil.h"
#include "OutfitSnapshotRestore.h"

namespace FEC::EquipMode::Preference
{
	void CaptureEquip(RE::Actor* a_actor, RE::TESBoundObject* a_object, bool a_leftHand, RE::ExtraDataList* a_xList, bool a_hasSelection)
	{
		if (!a_actor || !a_object) {
			return;
		}
		if (!ActorScope::IsAffectedFollower(a_actor)) {
			return;
		}

		// Only capture from the active trade menu for this actor.
		auto menu = ContainerMenuUtil::GetOpenContainerMenu();
		if (!menu) {
			return;
		}
		auto target = ContainerMenuUtil::GetAffectedTarget(menu.get());
		if (!target || target.get() != a_actor) {
			return;
		}

		// Headgear preference is secondary-key only; primary equip must not save it.
		const auto cepCat = FEC::CombatEquip::Preference::Policy::TryClassifyCEPCategory(a_object, a_leftHand);
		const bool isHeadgear = cepCat.has_value() && *cepCat == CombatEquipPreference::Category::kHeadgear;

		if (!isHeadgear) {
			CombatEquipPreference::CaptureUserEquip(a_actor, a_object, a_leftHand, a_xList, a_hasSelection);
		}
		OutfitSnapshotRestore::CaptureUserEquip(a_actor, a_object, a_xList, a_hasSelection);
	}

	void CaptureUnequip(RE::Actor* a_actor, RE::TESBoundObject* a_object, bool a_leftHand)
	{
		if (!a_actor || !a_object) {
			return;
		}
		if (!ActorScope::IsAffectedFollower(a_actor)) {
			return;
		}

		// Only capture from the active trade menu for this actor.
		auto menu = ContainerMenuUtil::GetOpenContainerMenu();
		if (!menu) {
			return;
		}
		auto target = ContainerMenuUtil::GetAffectedTarget(menu.get());
		if (!target || target.get() != a_actor) {
			return;
		}

		// Headgear clear is secondary-key only.
		const auto cepCat = FEC::CombatEquip::Preference::Policy::TryClassifyCEPCategory(a_object, a_leftHand);
		const bool isHeadgear = cepCat.has_value() && *cepCat == CombatEquipPreference::Category::kHeadgear;

		if (!isHeadgear) {
			CombatEquipPreference::CaptureUserUnequip(a_actor, a_object, a_leftHand);
		}
		OutfitSnapshotRestore::CaptureUserUnequip(a_actor, a_object);
	}
}
