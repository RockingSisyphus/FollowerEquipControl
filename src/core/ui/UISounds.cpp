#include "UISounds.h"

#include "PluginSettings.h"

namespace FEC::UISounds
{
	namespace
	{
		[[nodiscard]] RE::DEFAULT_OBJECT GetDefaultFor(RE::TESBoundObject* a_object, Action a_action) noexcept
		{
			if (a_action == Action::kUse) {
				return RE::DEFAULT_OBJECT::kPickupSoundGeneric;
			}

			if (a_object) {
				if (a_object->GetFormType() == RE::FormType::Book) {
					return a_action == Action::kPickup ? RE::DEFAULT_OBJECT::kPickupSoundBook : RE::DEFAULT_OBJECT::kPutdownSoundBook;
				}
				if (a_object->IsArmor()) {
					return a_action == Action::kPickup ? RE::DEFAULT_OBJECT::kPickupSoundArmor : RE::DEFAULT_OBJECT::kPutdownSoundArmor;
				}
				if (a_object->IsWeapon()) {
					return a_action == Action::kPickup ? RE::DEFAULT_OBJECT::kPickupSoundWeapon : RE::DEFAULT_OBJECT::kPutdownSoundWeapon;
				}
			}

			return a_action == Action::kPickup ? RE::DEFAULT_OBJECT::kPickupSoundGeneric : RE::DEFAULT_OBJECT::kPutdownSoundGeneric;
		}
	}

	bool Enabled()
	{
		return FEC::PluginSettings::Get().uiFeedback.enableUISounds;
	}

	void PlayForObject(RE::TESBoundObject* a_object, Action a_action)
	{
		if (!Enabled()) {
			return;
		}

		auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
		auto* audio = RE::BSAudioManager::GetSingleton();
		if (!dom || !audio) {
			return;
		}

		// AlchemyItem use audio comes from consumptionSound, not pickup/putdown defaults.
		if (a_action == Action::kUse && a_object) {
			if (auto* alch = a_object->As<RE::AlchemyItem>()) {
				if (alch->data.consumptionSound) {
					(void)audio->Play(alch->data.consumptionSound->GetFormID());
					return;
				}
			}

			// IngredientItem lacks consumptionSound; YNAM pickup sound is the closest record-backed fallback.
			if (auto* ingr = a_object->As<RE::IngredientItem>()) {
				if (ingr->pickupSound) {
					(void)audio->Play(ingr->pickupSound->GetFormID());
					return;
				}
			}
		}

		auto* soundForm = dom->GetObject<RE::BGSSoundDescriptorForm>(GetDefaultFor(a_object, a_action));
		if (!soundForm) {
			return;
		}

		const auto soundID = soundForm->GetFormID();
		if (soundID == 0) {
			return;
		}

		(void)audio->Play(soundID);
	}

	void PlaySoundByFormID(RE::FormID a_soundFormID)
	{
		if (!Enabled()) {
			return;
		}

		if (a_soundFormID == 0) {
			return;
		}

		auto* audio = RE::BSAudioManager::GetSingleton();
		if (!audio) {
			return;
		}

		(void)audio->Play(a_soundFormID);
	}
}
