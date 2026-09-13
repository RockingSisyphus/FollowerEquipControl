#include "CombatEquipScoreApplier.h"

#include "CombatEquipPreference.h"
#include "CombatEquipScoreEquipSlotCache.h"
#include "CombatEquipScoreHookSafety.h"
#include "CombatEquipScoreTelemetry.h"

#include "SignatureResolve.h"

#include "PluginSettings.h"

#include "RE/E/ExtraEnchantment.h"

namespace FEC::CombatEquipScoreApplier
{
	namespace
	{
		[[nodiscard]] bool HasAnyInventoryInstance(RE::Actor* a_actor, RE::TESBoundObject* a_object) noexcept
		{
			if (!a_actor || !a_object) {
				return false;
			}

			auto inv = a_actor->GetInventory([&](RE::TESBoundObject& a_obj) {
				return std::addressof(a_obj) == a_object;
			});

			const auto it = inv.find(a_object);
			return it != inv.end() && it->second.first > 0;
		}

		// If all known staff instances are empty, avoid biasing toward that staff.
		// Fail-open when charge cannot be determined.
		[[nodiscard]] bool HasAnyChargedStaffInstance(RE::Actor* a_actor, RE::FormID a_staffBaseID) noexcept
		{
			if (!a_actor || a_staffBaseID == 0) {
				return true;
			}

			auto inv = a_actor->GetInventory([&](RE::TESBoundObject& a_obj) {
				return a_obj.GetFormID() == a_staffBaseID;
			});
			for (const auto& entry : inv) {
				auto* item = entry.first;
				const auto& invData = entry.second;
				if (!item || item->GetFormID() != a_staffBaseID) {
					continue;
				}

				// Without per-instance extra lists, charge cannot be determined; fail-open.
				if (!invData.second || !invData.second->extraLists || invData.second->extraLists->empty()) {
					return true;
				}

				bool sawCharge = false;
				for (auto* xList : *invData.second->extraLists) {
					if (!xList) {
						continue;
					}
					if (auto* xEnch = xList->GetByType<RE::ExtraEnchantment>()) {
						sawCharge = true;
						if (xEnch->charge > 0) {
							return true;
						}
					}
				}

				// Known charge data was present and no instance had charge; otherwise fail-open.
				return !sawCharge;
			}

			// Staff not found in inventory: fail-closed.
			return false;
		}

		// Skip ranged score bias when the actor has no matching ammo; otherwise the engine
		// can keep selecting unusable ranged weapons.
		// Null actor fails open; an empty inventory result is a real "no ammo" answer.
		[[nodiscard]] bool HasAnyAmmoForRangedWeapon(RE::Actor* a_actor, bool a_isBolt) noexcept
		{
			if (!a_actor) {
				return true;
			}

			// Infinite Ammo disables consumption, so ammo availability is irrelevant.
			if (PluginSettings::Get().combatEquipRestore.enableInfiniteAmmo) {
				return true;
			}

			auto inv = a_actor->GetInventory([a_isBolt](RE::TESBoundObject& a_obj) {
				if (a_obj.GetFormType() != RE::FormType::Ammo) {
					return false;
				}
				auto* ammo = a_obj.As<RE::TESAmmo>();
				return ammo && ammo->IsBolt() == a_isBolt;
			});

			for (const auto& entry : inv) {
				if (entry.second.first > 0) {
					return true;
				}
			}

			return false;
		}
	}

	float MaybeApplyPreferredBonus(
		RE::Actor* a_actor,
		RE::TESForm* a_form,
		const RE::BGSEquipSlot* a_slot,
		float a_score)
	{
		using FEC::CombatEquipScoreHookSafety::IsPlausiblePolymorphic;

		if (!a_actor || !a_form) {
			return a_score;
		}
		if (!IsPlausiblePolymorphic(a_form)) {
			return a_score;
		}

		const auto actorID = a_actor->GetFormID();
		const auto baseID = a_form->GetFormID();
		if (actorID == 0 || baseID == 0) {
			return a_score;
		}

		// Scoring can be invoked for ephemeral or virtual items; only bias items in the actor inventory.
		auto* boundObject = a_form->As<RE::TESBoundObject>();
		if (!boundObject || !HasAnyInventoryInstance(a_actor, boundObject)) {
			return a_score;
		}

		using Cat = CombatEquipPreference::Category;
		auto getMatch = [&](Cat a_cat) noexcept -> std::optional<CombatEquipPreference::Entry> {
			const auto e = CombatEquipPreference::GetEntry(actorID, a_cat);
			if (!e.has_value() || e->baseObjectID != baseID) {
				return std::nullopt;
			}
			return e;
		};

		auto bonusFor = [&](Cat a_cat) noexcept -> float {
			switch (a_cat) {
			case Cat::kOneHandLeft:
			case Cat::kShieldLeft:
			case Cat::kStaffLeft:
			case Cat::kScrollLeft:
				return kPreferredBonusLeft;
			default:
				return kPreferredBonus;
			}
		};

		const auto& slots = CombatEquipScoreEquipSlotCache::GetHandSlots();
		const bool requestLeft = (a_slot && slots.left && a_slot == slots.left);
		const bool requestRight = (a_slot && slots.right && a_slot == slots.right);

		std::optional<InstanceSignature::EquipState> preferWornState;
		if (requestLeft) {
			preferWornState = InstanceSignature::EquipState::kWornLeft;
		} else if (requestRight) {
			preferWornState = InstanceSignature::EquipState::kWornRight;
		}

		auto preferredIdentityAvailable = [&](const CombatEquipPreference::Entry& a_entry) noexcept -> bool {
			if (!a_entry.signature.HasStableIdentity()) {
				return true;
			}
			// Scoring has no instance context; only bias if the preferred identity still resolves in inventory.
			return SignatureResolve::Resolve(
				a_actor,
				boundObject,
				a_entry.signature,
				preferWornState,
				SignatureResolve::Policy::kIdentityOnly)
				.HasXList();
		};

		if (a_form->GetFormType() == RE::FormType::Scroll) {
			const bool isTwoHandedScroll = [&]() noexcept {
				auto* scroll = a_form->As<RE::ScrollItem>();
				return scroll && scroll->IsTwoHanded();
			}();

			// kScrollBoth is a true two-handed scroll preference; ignore hand intent for two-handed scrolls.
			if (isTwoHandedScroll) {
				if (const auto e = getMatch(Cat::kScrollBoth); e.has_value() && preferredIdentityAvailable(*e)) {
					const float after = a_score + bonusFor(Cat::kScrollBoth);
					CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
					return after;
				}
				return a_score;
			}

			if (requestLeft) {
				if (const auto e = getMatch(Cat::kScrollLeft); e.has_value() && preferredIdentityAvailable(*e)) {
					const float after = a_score + bonusFor(Cat::kScrollLeft);
					CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
					return after;
				}
				return a_score;
			}
			if (requestRight) {
				if (const auto e = getMatch(Cat::kScrollRight); e.has_value() && preferredIdentityAvailable(*e)) {
					const float after = a_score + bonusFor(Cat::kScrollRight);
					CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
					return after;
				}
				return a_score;
			}

			// Unknown one-handed scroll intent: consider both hands.
			float bestBonus = 0.0f;
			if (const auto e = getMatch(Cat::kScrollLeft); e.has_value() && preferredIdentityAvailable(*e)) {
				bestBonus = bonusFor(Cat::kScrollLeft);
			}
			if (const auto e = getMatch(Cat::kScrollRight); e.has_value() && preferredIdentityAvailable(*e)) {
				const float b = bonusFor(Cat::kScrollRight);
				if (b > bestBonus) {
					bestBonus = b;
				}
			}
			if (bestBonus > 0.0f) {
				const float after = a_score + bestBonus;
				CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
				return after;
			}
			return a_score;
		}

		if (auto* weap = a_form->As<RE::TESObjectWEAP>()) {
			if (weap->IsStaff()) {
				if (requestLeft) {
					if (const auto e = getMatch(Cat::kStaffLeft); e.has_value() && preferredIdentityAvailable(*e)) {
						if (!HasAnyChargedStaffInstance(a_actor, baseID)) {
							return a_score;
						}
						const float after = a_score + bonusFor(Cat::kStaffLeft);
						CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
						return after;
					}
					return a_score;
				}
				if (requestRight) {
					if (const auto e = getMatch(Cat::kStaffRight); e.has_value() && preferredIdentityAvailable(*e)) {
						if (!HasAnyChargedStaffInstance(a_actor, baseID)) {
							return a_score;
						}
						const float after = a_score + bonusFor(Cat::kStaffRight);
						CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
						return after;
					}
					return a_score;
				}

				// Unknown staff intent: consider both hands.
				float bestBonus = 0.0f;
				if (const auto e = getMatch(Cat::kStaffLeft); e.has_value() && preferredIdentityAvailable(*e)) {
					bestBonus = bonusFor(Cat::kStaffLeft);
				}
				if (const auto e = getMatch(Cat::kStaffRight); e.has_value() && preferredIdentityAvailable(*e)) {
					const float b = bonusFor(Cat::kStaffRight);
					if (b > bestBonus) {
						bestBonus = b;
					}
				}
				if (bestBonus > 0.0f) {
					if (!HasAnyChargedStaffInstance(a_actor, baseID)) {
						return a_score;
					}
					const float after = a_score + bestBonus;
					CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
					return after;
				}
				return a_score;
			}

			if (weap->IsBow()) {
				if (const auto e = getMatch(Cat::kBow); e.has_value()) {
					if (!HasAnyAmmoForRangedWeapon(a_actor, false)) {
						return a_score;
					}
					if (!preferredIdentityAvailable(*e)) {
						return a_score;
					}
					const float after = a_score + kPreferredBonus;
					CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
					return after;
				}
				return a_score;
			}
			if (weap->IsCrossbow()) {
				if (const auto e = getMatch(Cat::kCrossbow); e.has_value()) {
					if (!HasAnyAmmoForRangedWeapon(a_actor, true)) {
						return a_score;
					}
					if (!preferredIdentityAvailable(*e)) {
						return a_score;
					}
					const float after = a_score + kPreferredBonus;
					CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
					return after;
				}
				return a_score;
			}

			if (weap->IsTwoHandedSword() || weap->IsTwoHandedAxe()) {
				if (const auto e = getMatch(Cat::kTwoHand); e.has_value() && preferredIdentityAvailable(*e)) {
					const float after = a_score + kPreferredBonus;
					CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
					return after;
				}
				return a_score;
			}

			if (requestLeft) {
				if (const auto e = getMatch(Cat::kOneHandLeft); e.has_value() && preferredIdentityAvailable(*e)) {
					const float after = a_score + bonusFor(Cat::kOneHandLeft);
					CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
					return after;
				}
				return a_score;
			}
			if (requestRight) {
				if (const auto e = getMatch(Cat::kOneHandRight); e.has_value() && preferredIdentityAvailable(*e)) {
					const float after = a_score + bonusFor(Cat::kOneHandRight);
					CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
					return after;
				}
				return a_score;
			}

			// Unknown melee intent: consider both hands.
			float bestBonus = 0.0f;
			if (const auto e = getMatch(Cat::kOneHandLeft); e.has_value() && preferredIdentityAvailable(*e)) {
				bestBonus = bonusFor(Cat::kOneHandLeft);
			}
			if (const auto e = getMatch(Cat::kOneHandRight); e.has_value() && preferredIdentityAvailable(*e)) {
				const float b = bonusFor(Cat::kOneHandRight);
				if (b > bestBonus) {
					bestBonus = b;
				}
			}
			if (bestBonus > 0.0f) {
				const float after = a_score + bestBonus;
				CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
				return after;
			}

			return a_score;
		}

		// Ammo does not go through CombatInventoryItem::CalculateScore: there is no
		// CombatInventoryItemAmmo type or TYPE::kAmmo. Ammo preferences are enforced by
		// CombatEquipOverrideAmmo.

		if (auto* armo = a_form->As<RE::TESObjectARMO>()) {
			if (armo->IsShield()) {
				if (const auto e = getMatch(Cat::kShieldLeft); e.has_value() && preferredIdentityAvailable(*e)) {
					const float after = a_score + bonusFor(Cat::kShieldLeft);
					CombatEquipScoreTelemetry::TraceBiasApplied(a_actor, a_form, a_slot, a_score, after);
					return after;
				}
				return a_score;
			}
		}

		return a_score;
	}
}
