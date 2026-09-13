#include "CombatEquipOverrideMelee.h"

#include "CombatEquipOverrideUtil.h"

#include "CombatEquipPreference.h"
#include "EquipGate.h"
#include "SignatureResolve.h"
#include "PluginSettings.h"
#include "WeaponBound.h"

namespace FEC::CombatEquipOverride::Melee
{
	namespace
	{
		using Cat = CombatEquipPreference::Category;

		[[nodiscard]] bool IsTwoHandMeleeWeapon(const RE::TESBoundObject* a_object)
		{
			auto* weap = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr;
			if (!weap) {
				return false;
			}

			switch (weap->GetWeaponType()) {
			case RE::WEAPON_TYPE::kTwoHandSword:
			case RE::WEAPON_TYPE::kTwoHandAxe:
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] bool IsOneHandMeleeWeapon(const RE::TESBoundObject* a_object)
		{
			auto* weap = a_object ? a_object->As<RE::TESObjectWEAP>() : nullptr;
			if (!weap) {
				return false;
			}

			switch (weap->GetWeaponType()) {
			case RE::WEAPON_TYPE::kOneHandSword:
			case RE::WEAPON_TYPE::kOneHandDagger:
			case RE::WEAPON_TYPE::kOneHandAxe:
			case RE::WEAPON_TYPE::kOneHandMace:
				return true;
			default:
				return false;
			}
		}

		[[nodiscard]] bool IsShield(const RE::TESBoundObject* a_object)
		{
			auto* armo = a_object ? a_object->As<RE::TESObjectARMO>() : nullptr;
			return armo && armo->IsShield();
		}

		[[nodiscard]] bool IsRightUnarmedOrOneHandMelee(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}

			auto* rightForm = a_actor->GetEquippedObject(false);
			if (!rightForm) {
				return true;
			}

			auto* rightObj = rightForm->As<RE::TESBoundObject>();
			return rightObj && IsOneHandMeleeWeapon(rightObj);
		}

		[[nodiscard]] bool IsLeftUnarmedOrOneHandMeleeOrShield(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}

			auto* leftForm = a_actor->GetEquippedObject(true);
			if (!leftForm) {
				return true;
			}

			auto* leftObj = leftForm->As<RE::TESBoundObject>();
			if (!leftObj) {
				return false;
			}

			return IsOneHandMeleeWeapon(leftObj) || IsShield(leftObj);
		}

		// Same resolved instance would make dual-wield enforcement swap hands repeatedly.
		[[nodiscard]] bool OneHandPrefsResolveSameInstance(RE::Actor* a_actor)
		{
			if (!a_actor) return false;
			const auto id = a_actor->GetFormID();
			if (id == 0) return false;

			const auto r = CombatEquipPreference::GetEntry(id, Cat::kOneHandRight);
			const auto l = CombatEquipPreference::GetEntry(id, Cat::kOneHandLeft);
			if (!r || !l || r->baseObjectID == 0 || l->baseObjectID == 0) return false;
			if (r->baseObjectID != l->baseObjectID) return false;

			auto* form = RE::TESForm::LookupByID(r->baseObjectID);
			auto* bound = form ? form->As<RE::TESBoundObject>() : nullptr;
			if (!bound) return false;

			const auto resR = SignatureResolve::Resolve(a_actor, bound, r->signature,
				InstanceSignature::EquipState::kWornRight, SignatureResolve::Policy::kIdentityOnly);
			const auto resL = SignatureResolve::Resolve(a_actor, bound, l->signature,
				InstanceSignature::EquipState::kWornLeft, SignatureResolve::Policy::kIdentityOnly);

			if (resR.HasXList() && resL.HasXList()) {
				return resR.xList == resL.xList;
			}
			// Both base-only: indistinguishable single item.
			return resR.kind == SignatureResolve::ResolveKind::kMatchedBaseOnly &&
			       resL.kind == SignatureResolve::ResolveKind::kMatchedBaseOnly;
		}

		[[nodiscard]] bool TryEquipFromCategory(RE::Actor* a_actor, Cat a_cat, const RE::BGSEquipSlot* a_slotToUse)
		{
			if (!a_actor) {
				return false;
			}
			const auto actorID = a_actor->GetFormID();
			if (actorID == 0) {
				return false;
			}

			const auto entry = CombatEquipPreference::GetEntry(actorID, a_cat);
			if (!entry.has_value() || entry->baseObjectID == 0) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("MeleeEnforce: actor {:08X} cat {} no pref", actorID, static_cast<int>(a_cat));
				}
				return false;
			}

			std::optional<InstanceSignature::EquipState> preferWornState;
			if (a_slotToUse == Util::GetRightHandSlot()) {
				preferWornState = InstanceSignature::EquipState::kWornRight;
			} else if (a_slotToUse == Util::GetLeftHandSlot()) {
				preferWornState = InstanceSignature::EquipState::kWornLeft;
			}

			auto* form = RE::TESForm::LookupByID(entry->baseObjectID);
			auto* bound = form ? form->As<RE::TESBoundObject>() : nullptr;
			if (!bound) {
				return false;
			}

			const auto resolved = SignatureResolve::Resolve(
				a_actor,
				bound,
				entry->signature,
				preferWornState,
				SignatureResolve::Policy::kIdentityOnly);
			if (!resolved.Matched()) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("MeleeEnforce: actor {:08X} cat {} pref {:08X} not resolved in inventory",
						actorID, static_cast<int>(a_cat), entry->baseObjectID);
				}
				return false;
			}
			auto* xList = resolved.HasXList() ? resolved.xList : nullptr;

			if (a_slotToUse == Util::GetRightHandSlot()) {
				auto* eq = a_actor->GetEquippedObject(false);
				if (eq && eq->GetFormID() == bound->GetFormID()) {
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace("MeleeEnforce: actor {:08X} cat {} pref {:08X} already in right hand",
							actorID, static_cast<int>(a_cat), entry->baseObjectID);
					}
					return true;
				}
			} else if (a_slotToUse == Util::GetLeftHandSlot()) {
				auto* eq = a_actor->GetEquippedObject(true);
				if (eq && eq->GetFormID() == bound->GetFormID()) {
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace("MeleeEnforce: actor {:08X} cat {} pref {:08X} already in left hand",
							actorID, static_cast<int>(a_cat), entry->baseObjectID);
					}
					return true;
				}
			} else {
				auto* eq = a_actor->GetEquippedObject(false);
				if (eq && eq->GetFormID() == bound->GetFormID()) {
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace("MeleeEnforce: actor {:08X} cat {} pref {:08X} already equipped",
							actorID, static_cast<int>(a_cat), entry->baseObjectID);
					}
					return true;
				}
			}

			// Bound weapons have special lifecycle semantics; do not force them.
			if (WeaponBound::IsWeaponAndBound(bound)) {
				return false;
			}

			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return false;
			}

			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("MeleeEnforce: actor {:08X} cat {} equipping pref {:08X}",
					actorID, static_cast<int>(a_cat), entry->baseObjectID);
			}

			equipMan->EquipObject(
				a_actor,
				bound,
				xList,
				1,
				a_slotToUse,
				false,
				false,
				false,
				true);

			return true;
		}

		[[nodiscard]] std::optional<Decision> HandleTwoHand(RE::Actor* a_actor)
		{
			EquipGate::ScopedBypass gateBypass;

			if (TryEquipFromCategory(a_actor, Cat::kTwoHand, nullptr) ||
				TryEquipFromCategory(a_actor, Cat::kOneHandRight, Util::GetRightHandSlot()) ||
				TryEquipFromCategory(a_actor, Cat::kOneHandLeft, Util::GetLeftHandSlot()) ||
				TryEquipFromCategory(a_actor, Cat::kShieldLeft, Util::GetLeftHandSlot())) {
				Decision d;
				d.action = DecisionAction::kBlockEquipAttempt;
				d.reason = "melee_override_twohand";
				return d;
			}

			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("MeleeEnforce: actor {:08X} HandleTwoHand all prefs failed -> fallback unarmed",
					a_actor->GetFormID());
			}
			if (PluginSettings::Get().combatEquipEnforcement.enableMeleeEnforcementEmptyHand) {
				Util::ForceUnequipRight(a_actor);
				Util::ForceUnequipLeft(a_actor);
			}

			Decision d;
			d.action = DecisionAction::kBlockEquipAttempt;
			d.reason = "melee_override_twohand_fallback_unarmed";
			return d;
		}

		[[nodiscard]] std::optional<Decision> HandleOneHandRight(RE::Actor* a_actor)
		{
			EquipGate::ScopedBypass gateBypass;

			const bool equippedRight =
				TryEquipFromCategory(a_actor, Cat::kTwoHand, nullptr) ||
				TryEquipFromCategory(a_actor, Cat::kOneHandRight, Util::GetRightHandSlot());
			if (!equippedRight) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("MeleeEnforce: actor {:08X} HandleOneHandRight -> right hand fallback unarmed",
						a_actor->GetFormID());
				}
				if (PluginSettings::Get().combatEquipEnforcement.enableMeleeEnforcementEmptyHand) {
					Util::ForceUnequipRight(a_actor);
				}
			}

			const bool sameInstance = OneHandPrefsResolveSameInstance(a_actor);

			bool equippedLeft = false;
			const bool leftEligible = IsLeftUnarmedOrOneHandMeleeOrShield(a_actor);
			if (leftEligible) {
				equippedLeft =
					(!sameInstance && TryEquipFromCategory(a_actor, Cat::kOneHandLeft, Util::GetLeftHandSlot())) ||
					TryEquipFromCategory(a_actor, Cat::kShieldLeft, Util::GetLeftHandSlot());
				if (!equippedLeft) {
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace("MeleeEnforce: actor {:08X} HandleOneHandRight -> left hand fallback unarmed",
							a_actor->GetFormID());
					}
					if (PluginSettings::Get().combatEquipEnforcement.enableMeleeEnforcementEmptyHand) {
						Util::ForceUnequipLeft(a_actor);
					}
				}
			}

			Decision d;
			d.action = DecisionAction::kBlockEquipAttempt;
			if (equippedRight) {
				if (leftEligible) {
					d.reason = equippedLeft ? "melee_override_1hr__r_ok__l_ok" : "melee_override_1hr__r_ok__l_unarmed";
				} else {
					d.reason = "melee_override_1hr__r_ok__l_skip";
				}
			} else {
				if (leftEligible) {
					d.reason = equippedLeft ? "melee_override_1hr__r_unarmed__l_ok" : "melee_override_1hr__r_unarmed__l_unarmed";
				} else {
					d.reason = "melee_override_1hr__r_unarmed__l_skip";
				}
			}
			return d;
		}

		[[nodiscard]] std::optional<Decision> HandleOneHandLeft(RE::Actor* a_actor)
		{
			EquipGate::ScopedBypass gateBypass;

			const bool equippedLeft =
				TryEquipFromCategory(a_actor, Cat::kTwoHand, nullptr) ||
				TryEquipFromCategory(a_actor, Cat::kOneHandLeft, Util::GetLeftHandSlot()) ||
				TryEquipFromCategory(a_actor, Cat::kShieldLeft, Util::GetLeftHandSlot());
			if (!equippedLeft) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("MeleeEnforce: actor {:08X} HandleOneHandLeft -> left hand fallback unarmed",
						a_actor->GetFormID());
				}
				if (PluginSettings::Get().combatEquipEnforcement.enableMeleeEnforcementEmptyHand) {
					Util::ForceUnequipLeft(a_actor);
				}
			}

			const bool sameInstance = OneHandPrefsResolveSameInstance(a_actor);

			bool equippedRight = false;
			const bool rightEligible = IsRightUnarmedOrOneHandMelee(a_actor);
			if (rightEligible) {
				equippedRight = !sameInstance && TryEquipFromCategory(a_actor, Cat::kOneHandRight, Util::GetRightHandSlot());
				if (!equippedRight) {
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace("MeleeEnforce: actor {:08X} HandleOneHandLeft -> right hand fallback unarmed",
							a_actor->GetFormID());
					}
					if (PluginSettings::Get().combatEquipEnforcement.enableMeleeEnforcementEmptyHand) {
						Util::ForceUnequipRight(a_actor);
					}
				}
			}

			Decision d;
			d.action = DecisionAction::kBlockEquipAttempt;
			if (equippedLeft) {
				if (rightEligible) {
					d.reason = equippedRight ? "melee_override_1hl__l_ok__r_ok" : "melee_override_1hl__l_ok__r_unarmed";
				} else {
					d.reason = "melee_override_1hl__l_ok__r_skip";
				}
			} else {
				if (rightEligible) {
					d.reason = equippedRight ? "melee_override_1hl__l_unarmed__r_ok" : "melee_override_1hl__l_unarmed__r_unarmed";
				} else {
					d.reason = "melee_override_1hl__l_unarmed__r_skip";
				}
			}
			return d;
		}

		[[nodiscard]] std::optional<Decision> HandleShield(RE::Actor* a_actor)
		{
			EquipGate::ScopedBypass gateBypass;

			const bool equippedLeft =
				TryEquipFromCategory(a_actor, Cat::kTwoHand, nullptr) ||
				TryEquipFromCategory(a_actor, Cat::kOneHandLeft, Util::GetLeftHandSlot()) ||
				TryEquipFromCategory(a_actor, Cat::kShieldLeft, Util::GetLeftHandSlot());
			if (!equippedLeft) {
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("MeleeEnforce: actor {:08X} HandleShield -> left hand fallback unarmed",
						a_actor->GetFormID());
				}
				if (PluginSettings::Get().combatEquipEnforcement.enableMeleeEnforcementEmptyHand) {
					Util::ForceUnequipLeft(a_actor);
				}
			}

			bool equippedRight = false;
			const bool rightEligible = IsRightUnarmedOrOneHandMelee(a_actor);
			if (rightEligible) {
				equippedRight = TryEquipFromCategory(a_actor, Cat::kOneHandRight, Util::GetRightHandSlot());
				if (!equippedRight) {
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace("MeleeEnforce: actor {:08X} HandleShield -> right hand fallback unarmed",
							a_actor->GetFormID());
					}
					if (PluginSettings::Get().combatEquipEnforcement.enableMeleeEnforcementEmptyHand) {
						Util::ForceUnequipRight(a_actor);
					}
				}
			}

			Decision d;
			d.action = DecisionAction::kBlockEquipAttempt;
			if (equippedLeft) {
				if (rightEligible) {
					d.reason = equippedRight ? "melee_override_shield__l_ok__r_ok" : "melee_override_shield__l_ok__r_unarmed";
				} else {
					d.reason = "melee_override_shield__l_ok__r_skip";
				}
			} else {
				if (rightEligible) {
					d.reason = equippedRight ? "melee_override_shield__l_unarmed__r_ok" : "melee_override_shield__l_unarmed__r_unarmed";
				} else {
					d.reason = "melee_override_shield__l_unarmed__r_skip";
				}
			}
			return d;
		}
	}

	std::optional<Decision> DecideMeleeEquipOverride(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count,
		bool a_aiDriven,
		bool a_drawn)
	{
		(void)a_extraData;
		(void)a_count;

		const auto& ce = PluginSettings::Get().combatEquipEnforcement;
		if (!ce.enableMeleeEnforcement) {
			return std::nullopt;
		}

		// Only AI-driven equip attempts while drawn or in combat.
		if (!a_aiDriven || !a_actor || !a_object) {
			return std::nullopt;
		}
		if (!a_drawn && !a_actor->IsInCombat()) {
			return std::nullopt;
		}

		const bool requestLeft = Util::IsLeftHandSlot(a_slot);
		const bool requestRight = Util::IsRightHandSlot(a_slot) || !requestLeft;

		if (IsTwoHandMeleeWeapon(a_object)) {
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("MeleeEnforce: actor {:08X} AI wants twohand {:08X} -> HandleTwoHand",
					a_actor->GetFormID(), a_object->GetFormID());
			}
			return HandleTwoHand(a_actor);
		}

		if (IsOneHandMeleeWeapon(a_object)) {
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("MeleeEnforce: actor {:08X} AI wants onehand {:08X} hand={} -> Handle{}",
					a_actor->GetFormID(), a_object->GetFormID(),
					requestLeft ? "left" : "right",
					requestLeft ? "OneHandLeft" : "OneHandRight");
			}
			return requestLeft ? HandleOneHandLeft(a_actor) : HandleOneHandRight(a_actor);
		}

		if (IsShield(a_object)) {
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("MeleeEnforce: actor {:08X} AI wants shield {:08X} -> HandleShield",
					a_actor->GetFormID(), a_object->GetFormID());
			}
			(void)requestRight;
			return HandleShield(a_actor);
		}

		return std::nullopt;
	}

	std::optional<UnequipDecision> DecideMeleeUnequipOverride(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		std::uint32_t a_count,
		bool a_aiDriven)
	{
		(void)a_extraData;
		(void)a_count;

		const auto& ce = PluginSettings::Get().combatEquipEnforcement;
		if (!ce.enableMeleeEnforcement) {
			return std::nullopt;
		}
		if (!a_aiDriven || !a_actor || !a_object) {
			return std::nullopt;
		}

		// Keep unequip blocking limited to drawn or combat state.
		auto* st = a_actor->AsActorState();
		const bool drawn = (st && st->IsWeaponDrawn());
		if (!drawn && !a_actor->IsInCombat()) {
			return std::nullopt;
		}

		// Do not interfere with ranged behavior.
		if (Util::IsRangedWeaponEquipped(a_actor)) {
			return std::nullopt;
		}

		const bool isOneHand = IsOneHandMeleeWeapon(a_object);
		const bool isShield = IsShield(a_object);
		if (!isOneHand && !isShield) {
			return std::nullopt;
		}

		// Bound weapons have transient lifecycle behavior; do not block their unequip.
		if (WeaponBound::IsWeaponAndBound(a_object)) {
			return std::nullopt;
		}

		const auto actorID = a_actor->GetFormID();
		if (actorID == 0) {
			return std::nullopt;
		}

		auto matchesPreferredCategory = [&](Cat a_cat) -> bool {
			const auto entry = CombatEquipPreference::GetEntry(actorID, a_cat);
			if (!entry.has_value() || entry->baseObjectID == 0) {
				return false;
			}
			return entry->baseObjectID == a_object->GetFormID();
		};

		if (isShield) {
			// Shields are left-hand equipment; still handle missing or ambiguous slot data.
			bool isLeft = Util::IsLeftHandSlot(a_slot);
			if (!isLeft && !a_slot) {
				auto* equippedLeft = a_actor->GetEquippedObject(true);
				auto* equippedRight = a_actor->GetEquippedObject(false);
				if (equippedLeft == a_object && equippedRight != a_object) {
					isLeft = true;
				}
			}
			if (!isLeft) {
				return std::nullopt;
			}

			// Avoid blocking stale shield unequip calls.
			auto* equippedLeftObj = a_actor->GetEquippedObject(true);
			if (equippedLeftObj != a_object) {
				return std::nullopt;
			}

			if (!matchesPreferredCategory(Cat::kShieldLeft)) {
				return std::nullopt;
			}

			UnequipDecision d;
			d.action = UnequipDecisionAction::kBlockUnequipAttempt;
			d.reason = "melee_unequip_block_preferred_shield_left";
			return d;
		}

		// Prefer slot data for one-hand unequip direction; fall back to equipped objects.
		const bool slotSaysLeft = Util::IsLeftHandSlot(a_slot);
		const bool slotSaysRight = Util::IsRightHandSlot(a_slot);
		auto* equippedLeft = a_actor->GetEquippedObject(true);
		auto* equippedRight = a_actor->GetEquippedObject(false);
		const bool uniquelyLeft = (equippedLeft == a_object && equippedRight != a_object);
		const bool uniquelyRight = (equippedRight == a_object && equippedLeft != a_object);
		const bool ambiguousBoth = (equippedLeft == a_object && equippedRight == a_object);

		bool shouldBlock = false;
		const char* reason = "";
		if (slotSaysLeft || (!slotSaysRight && uniquelyLeft)) {
			// Avoid blocking stale left-hand unequip calls.
			if (equippedLeft != a_object) {
				return std::nullopt;
			}
			shouldBlock = matchesPreferredCategory(Cat::kOneHandLeft);
			reason = "melee_unequip_block_preferred_onehand_left";
		} else if (slotSaysRight || (!slotSaysLeft && uniquelyRight)) {
			// Avoid blocking stale right-hand unequip calls.
			if (equippedRight != a_object) {
				return std::nullopt;
			}
			shouldBlock = matchesPreferredCategory(Cat::kOneHandRight);
			reason = "melee_unequip_block_preferred_onehand_right";
		} else if (ambiguousBoth) {
			// Ambiguous hand: block if either one-hand preference matches.
			shouldBlock = matchesPreferredCategory(Cat::kOneHandLeft) || matchesPreferredCategory(Cat::kOneHandRight);
			reason = "melee_unequip_block_preferred_onehand_ambiguous";
		} else {
			return std::nullopt;
		}

		if (!shouldBlock) {
			return std::nullopt;
		}

		UnequipDecision d;
		d.action = UnequipDecisionAction::kBlockUnequipAttempt;
		d.reason = reason;
		return d;
	}
}
