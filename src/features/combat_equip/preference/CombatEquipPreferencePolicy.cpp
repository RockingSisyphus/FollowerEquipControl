#include "CombatEquipPreferencePolicy.h"

#include "PluginSettings.h"

namespace FEC::CombatEquip::Preference::Policy
{
	namespace
	{
		constexpr std::uint16_t Bit(Category a_cat) noexcept
		{
			return static_cast<std::uint16_t>(1u << static_cast<std::uint8_t>(a_cat));
		}

		constexpr std::uint16_t BuildMask(std::initializer_list<Category> a_cats) noexcept
		{
			std::uint16_t out = 0;
			for (auto c : a_cats) {
				out = static_cast<std::uint16_t>(out | Bit(c));
			}
			return out;
		}

		constexpr std::uint8_t kTotal = static_cast<std::uint8_t>(Category::kTotal);
		static_assert(kTotal <= 16, "CrossClear masks assume <=16 categories");

		constexpr std::uint16_t kCrossClearOnSet[static_cast<std::size_t>(Category::kTotal)] = {
			/* kOneHandRight */ BuildMask({ Category::kTwoHand }),
			/* kOneHandLeft  */ BuildMask({ Category::kShieldLeft, Category::kTwoHand }),
			/* kShieldLeft   */ BuildMask({ Category::kOneHandLeft, Category::kTwoHand }),
			/* kTwoHand      */ BuildMask({ Category::kOneHandRight, Category::kOneHandLeft, Category::kShieldLeft }),
			/* kBow          */ BuildMask({ Category::kCrossbow }),
			/* kArrow        */ BuildMask({}),
			/* kCrossbow     */ BuildMask({ Category::kBow }),
			/* kBolt         */ BuildMask({}),
			/* kStaffRight   */ BuildMask({}),
			/* kStaffLeft    */ BuildMask({}),
			/* kScrollRight  */ BuildMask({ Category::kScrollBoth }),
			/* kScrollLeft   */ BuildMask({ Category::kScrollBoth }),
			/* kScrollBoth   */ BuildMask({ Category::kScrollRight, Category::kScrollLeft }),
			/* kHeadgear     */ BuildMask({}),
		};
	}

	std::optional<Category> TryClassifyCEPCategory(RE::TESBoundObject* a_object, bool a_leftHand)
	{
		if (!a_object) {
			return std::nullopt;
		}

		if (a_object->GetFormType() == RE::FormType::Scroll) {
			auto* scroll = a_object->As<RE::ScrollItem>();
			if (scroll && scroll->IsTwoHanded()) {
				return Category::kScrollBoth;
			}
			return a_leftHand ? Category::kScrollLeft : Category::kScrollRight;
		}

		if (auto* weap = a_object->As<RE::TESObjectWEAP>()) {
			if (weap->IsStaff()) {
				return a_leftHand ? Category::kStaffLeft : Category::kStaffRight;
			}
			if (weap->IsBow()) {
				return Category::kBow;
			}
			if (weap->IsCrossbow()) {
				return Category::kCrossbow;
			}
			if (weap->IsTwoHandedSword() || weap->IsTwoHandedAxe()) {
				return Category::kTwoHand;
			}
			return a_leftHand ? Category::kOneHandLeft : Category::kOneHandRight;
		}

		if (auto* ammo = a_object->As<RE::TESAmmo>()) {
			return ammo->IsBolt() ? Category::kBolt : Category::kArrow;
		}

		if (auto* armo = a_object->As<RE::TESObjectARMO>()) {
			if (armo->IsShield()) {
				return Category::kShieldLeft;
			}
			// Headgear covers Head, Hair, or Circlet biped slots.
			const auto slotMask = static_cast<std::uint32_t>(armo->GetSlotMask());
			constexpr auto kHead = static_cast<std::uint32_t>(RE::BGSBipedObjectForm::BipedObjectSlot::kHead);
			constexpr auto kHair = static_cast<std::uint32_t>(RE::BGSBipedObjectForm::BipedObjectSlot::kHair);
			constexpr auto kCirclet = static_cast<std::uint32_t>(RE::BGSBipedObjectForm::BipedObjectSlot::kCirclet);
			if ((slotMask & (kHead | kHair | kCirclet)) != 0) {
				return Category::kHeadgear;
			}
		}

		return std::nullopt;
	}

	std::uint16_t CrossClearMaskOnCaptureEquip(Category a_cat) noexcept
	{
		const auto idx = static_cast<std::uint8_t>(a_cat);
		if (idx >= static_cast<std::uint8_t>(Category::kTotal)) {
			return 0;
		}
		return kCrossClearOnSet[idx];
	}

	bool ShouldClearOnCaptureUnequip(Category a_cat, bool a_leftHand) noexcept
	{
		// Headgear is cleared only by the secondary-key path.
		// Preference capture filters it before this point.

		const auto& ce = PluginSettings::Get().combatEquipPreference;

		if (!a_leftHand) {
			if (!ce.enableClearPreferencesOnUnequip) {
				return false;
			}
			return true;
		}

		if (!ce.enableClearLeftHandPreferenceOnUnequip) {
			return false;
		}
		switch (a_cat) {
		case Category::kOneHandLeft:
		case Category::kShieldLeft:
		case Category::kStaffLeft:
		case Category::kScrollLeft:
			return true;
		default:
			return false;
		}
	}
}
