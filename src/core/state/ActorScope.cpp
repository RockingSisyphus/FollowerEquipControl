#include "ActorScope.h"

#include "ActorInclusion.h"
#include "ActorScopeExcludeState.h"
#include "KnownFollowerState.h"
#include "PluginSettings.h"

#include <RE/B/BGSEquipSlot.h>
#include <RE/E/ExtraAshPileRef.h>

namespace FEC::ActorScope
{
	namespace
	{
		// Skyrim.esm DefaultRace is the canonical humanoid armor parent.
		constexpr RE::FormID kDefaultRaceFormID = 0x00000019;

		// Skyrim.esm hand-slot FormIDs are fixed across load orders.
		constexpr RE::FormID kEquipSlotRightHand  = 0x00013F42;
		constexpr RE::FormID kEquipSlotLeftHand   = 0x00013F43;
		constexpr RE::FormID kEquipSlotEitherHand = 0x00013F44;
		constexpr RE::FormID kEquipSlotBothHands  = 0x00013F45;

		[[nodiscard]] const RE::TESRace* GetDefaultRacePtr() noexcept
		{
			static const RE::TESRace* cached = []() -> const RE::TESRace* {
				auto* dh = RE::TESDataHandler::GetSingleton();
				if (!dh) {
					return nullptr;
				}
				return dh->LookupForm<RE::TESRace>(kDefaultRaceFormID, "Skyrim.esm"sv);
			}();
			return cached;
		}

		// QNAM alone is not enough: some creature races expose hand slots but cannot use hand-held weapons.
		[[nodiscard]] bool RaceHasHandEquipSlot(const RE::TESRace* a_race) noexcept
		{
			if (!a_race) {
				return false;
			}
			for (auto* slot : a_race->equipSlots) {
				if (!slot) {
					continue;
				}
				const auto id = slot->GetFormID();
				if (id == kEquipSlotRightHand ||
					id == kEquipSlotLeftHand ||
					id == kEquipSlotEitherHand ||
					id == kEquipSlotBothHands) {
					return true;
				}
			}
			return false;
		}

		// VNAM must include a real weapon, shield, or torch bit; kHandToHandMelee and kSpell are ignored.
		[[nodiscard]] bool RaceHasRealWeaponBit(const RE::TESRace* a_race) noexcept
		{
			if (!a_race) {
				return false;
			}
			using EquipFlag = RE::TESRace::EquipmentFlag;
			return a_race->validEquipTypes.any(
				EquipFlag::kOneHandSword,
				EquipFlag::kOneHandDagger,
				EquipFlag::kOneHandAxe,
				EquipFlag::kOneHandMace,
				EquipFlag::kTwoHandSword,
				EquipFlag::kTwoHandAxe,
				EquipFlag::kBow,
				EquipFlag::kStaff,
				EquipFlag::kShield,
				EquipFlag::kTorch,
				EquipFlag::kCrossbow);
		}

		[[nodiscard]] bool RaceCanHoldHandItem(const RE::TESRace* a_race) noexcept
		{
			// Hand items require a hand slot, a real weapon bit, and either pickup support or a body object.
			if (!RaceHasHandEquipSlot(a_race) || !RaceHasRealWeaponBit(a_race)) {
				return false;
			}
			using Flag = RE::RACE_DATA::Flag;
			const bool canPickup  = a_race->data.flags.any(Flag::kCanPickupItems);
			const bool hasBodyObj = a_race->data.bodyObject.underlying() != 0;
			return canPickup || hasBodyObj;
		}

		[[nodiscard]] bool ChainsToDefaultRace(const RE::TESRace* a_race) noexcept
		{
			const auto* target = GetDefaultRacePtr();
			if (!a_race) {
				return false;
			}

			const auto* race = a_race;
			for (int i = 0; i < 8 && race; ++i) {
				if (race->GetFormID() == kDefaultRaceFormID) {
					return true;
				}
				if (target && race == target) {
					return true;
				}
				auto* parent = race->armorParentRace;
				if (!parent || parent == race) {
					break;
				}
				race = parent;
			}
			return false;
		}

		[[nodiscard]] bool SkinShareDefaultRace(const RE::TESRace* a_race) noexcept
		{
			if (!a_race) {
				return false;
			}
			const auto* skin = a_race->skin;
			if (!skin) {
				return false;
			}
			const auto* target = GetDefaultRacePtr();
			for (auto* aa : skin->armorAddons) {
				if (!aa) {
					continue;
				}
				if (aa->race) {
					if (aa->race->GetFormID() == kDefaultRaceFormID) {
						return true;
					}
					if (target && aa->race == target) {
						return true;
					}
				}
				for (auto* extra : aa->additionalRaces) {
					if (!extra) {
						continue;
					}
					if (extra->GetFormID() == kDefaultRaceFormID) {
						return true;
					}
					if (target && extra == target) {
						return true;
					}
				}
			}
			return false;
		}

		// Fallback for humanoid mod races that neither chain to DefaultRace nor reference it in skin data.
		[[nodiscard]] bool RaceUsesHumanoidSkeleton(const RE::TESRace* a_race) noexcept
		{
			const char* path = a_race->skeletonModels[0].GetModel();
			if (!path || *path == '\0') {
				return false;
			}
			constexpr std::string_view kHumanoidSkelPrefix = "actors\\character\\";
			return ::_strnicmp(path, kHumanoidSkelPrefix.data(), kHumanoidSkelPrefix.size()) == 0;
		}

		[[nodiscard]] bool RaceCanRenderBodyArmor(const RE::TESRace* a_race) noexcept
		{
			if (!a_race) {
				return false;
			}
			// A biped body slot is required for body armor to render.
			if (a_race->data.bodyObject == RE::BIPED_OBJECT::kNone) {
				return false;
			}
			if (ChainsToDefaultRace(a_race) || SkinShareDefaultRace(a_race)) {
				return true;
			}
			return RaceUsesHumanoidSkeleton(a_race);
		}
	}

	EquipCapability ClassifyEquipCapability(const RE::Actor* a_actor) noexcept
	{
		if (!a_actor) {
			return EquipCapability::kNone;
		}
		const auto* race = a_actor->GetRace();
		if (!race) {
			return EquipCapability::kNone;
		}

		const bool hand = RaceCanHoldHandItem(race);
		const bool body = RaceCanRenderBodyArmor(race);

		EquipCapability cap;
		if (hand && body) {
			cap = EquipCapability::kFull;
		} else if (hand) {
			cap = EquipCapability::kHandOnly;
		} else {
			cap = EquipCapability::kNone;
		}

		return cap;
	}

	bool IsPlayerCommandedActor(RE::Actor* a_actor) noexcept
	{
		if (!a_actor || a_actor->IsPlayerRef()) {
			return false;
		}
		if (!a_actor->IsCommandedActor()) {
			return false;
		}
		// AIProcess may already be freed while an actor is tearing down.
		if (a_actor->IsDeleted() || !a_actor->Is3DLoaded()) {
			return false;
		}
		if (!a_actor->GetActorRuntimeData().currentProcess) {
			return false;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return false;
		}
		return a_actor->GetCommandingActor().get() == player;
	}



	namespace
	{
		// Shared capability policy for categories with a gate toggle and a non-humanoid sub-toggle.
		void ApplyCapabilityPolicy(EquipPolicy& a_policy, bool a_nonHumanoidToggle) noexcept
		{
			switch (a_policy.capability) {
				case EquipCapability::kFull:
					a_policy.inScope = true;
					a_policy.armorAllowed = true;
					break;
				case EquipCapability::kHandOnly:
					// Hand-only actors are admitted; armor still follows the non-humanoid toggle.
					a_policy.inScope = true;
					a_policy.armorAllowed = a_nonHumanoidToggle;
					break;
				case EquipCapability::kNone:
					// Pure creatures require the non-humanoid toggle for admission and armor.
					a_policy.inScope = a_nonHumanoidToggle;
					a_policy.armorAllowed = a_nonHumanoidToggle;
					break;
			}
		}

		// Corpses are not tracked as followers; armor use is decided separately.
		void ApplyCorpsePolicy(EquipPolicy& a_policy) noexcept
		{
			a_policy.inScope = false;
			a_policy.armorAllowed = (a_policy.capability == EquipCapability::kFull);
		}

		// Former followers require the main former-follower toggle.
		[[nodiscard]] bool IsKnownFollowerAdmitted(RE::Actor* a_actor, bool a_affectFormerFollowers) noexcept
		{
			if (!KnownFollowerState::IsPersistedKnown(a_actor)) {
				return false;
			}
			if (a_affectFormerFollowers) {
				return true;
			}
			return KnownFollowerState::IsPlayerTeammate(a_actor);
		}
	}

	EquipPolicy ResolveEquipPolicy(RE::Actor* a_actor) noexcept
	{
		EquipPolicy policy{};
		if (!a_actor || a_actor->IsPlayerRef()) {
			return policy;
		}
		// User-disabled actors are out of scope for all mod features.
		if (ActorScopeExcludeState::IsExcluded(a_actor->GetFormID())) {
			return policy;
		}
		policy.capability = ClassifyEquipCapability(a_actor);

		// Dead actors are classified before live-actor checks so they cannot fall through to other categories.
		if (a_actor->IsDead()) {
			policy.category = ActorCategory::kCorpse;
			// Ash piles collapse to kNone so downstream checks treat them as inert.
			if (a_actor->extraList.HasType<RE::ExtraAshPileRef>()) {
				policy.capability = EquipCapability::kNone;
			}
			ApplyCorpsePolicy(policy);
			return policy;
		}

		const auto& sys = PluginSettings::Get().actorScope;

		// Priority: commanded, inclusion, then persisted follower; teammates win over summons and inclusion matches.

		// Commanded actors only stay in this branch when they are not also teammates.
		if (IsPlayerCommandedActor(a_actor) && !KnownFollowerState::IsPlayerTeammate(a_actor)) {
			policy.category = ActorCategory::kCommanded;
			if (IsKnownFollowerAdmitted(a_actor, sys.affectFormerFollowers)) {
				// Uses the same non-humanoid toggle as persisted followers.
				ApplyCapabilityPolicy(policy, sys.includeNonHumanoidSummons);
			}
			return policy;
		}

		// Inclusion matches also yield to teammates.
		if (sys.includeInclusionActors && ActorInclusion::MatchesAny(a_actor) && !KnownFollowerState::IsPlayerTeammate(a_actor)) {
			policy.category = ActorCategory::kInclusion;
			if (IsKnownFollowerAdmitted(a_actor, sys.affectFormerFollowers)) {
				ApplyCapabilityPolicy(policy, sys.includeNonHumanoidSummons);
			}
			return policy;
		}

		// Persisted teammates cover active and former followers once the toggle allows them.
		if (IsKnownFollowerAdmitted(a_actor, sys.affectFormerFollowers)) {
			policy.category = ActorCategory::kKnownFollower;
			// Persisted summons use the same non-humanoid toggle as their live form.
			ApplyCapabilityPolicy(policy, sys.includeNonHumanoidSummons);
			return policy;
		}

		return policy;
	}



	bool IsAffectedFollower(RE::Actor* a_actor) noexcept
	{
		return ResolveEquipPolicy(a_actor).inScope;
	}

	bool BodyEquipAllowed(RE::Actor* a_actor) noexcept
	{
		return ResolveEquipPolicy(a_actor).armorAllowed;
	}

	bool ArmorEquipAllowed(RE::Actor* a_actor, const RE::TESObjectARMO* a_armor) noexcept
	{
		if (!a_armor || !a_actor) {
			return true;
		}
		const auto policy = ResolveEquipPolicy(a_actor);
		// Out-of-scope actors keep the game's native rules.
		if (policy.category == ActorCategory::kOutOfScope) {
			return true;
		}
		return policy.armorAllowed;
	}

	bool HandEquipAllowed(RE::Actor* a_actor) noexcept
	{
		if (!a_actor) {
			return true;
		}
		const auto policy = ResolveEquipPolicy(a_actor);
		// Out-of-scope actors keep engine-native behavior.
		if (policy.category == ActorCategory::kOutOfScope) {
			return true;
		}
		// Only kNone actors lack the ability to hold a weapon; kHandOnly and kFull keep hand equip.
		return policy.capability != EquipCapability::kNone;
	}



	namespace
	{
		// Admission filter mirrors the capability gates used by equip policy.
		[[nodiscard]] bool PassesAdmissionFilter(EquipCapability a_cap, bool a_nonHumanoidToggle) noexcept
		{
			switch (a_cap) {
				case EquipCapability::kFull:     return true;
				case EquipCapability::kHandOnly: return true;
				case EquipCapability::kNone:     return a_nonHumanoidToggle;
			}
			return false;
		}

		// A mount is eligible if it is ridden by an in-scope actor, or if it is the player's last ridden mount.
		[[nodiscard]] bool IsPlayerOwnedMount(RE::Actor* a_actor) noexcept
		{
			if (!a_actor || !a_actor->IsAMount()) {
				return false;
			}

			{
				RE::NiPointer<RE::Actor> rider;
				if (a_actor->GetRider(rider) && rider) {
					const auto cat = ResolveEquipPolicy(rider.get()).category;
					if (cat != ActorCategory::kOutOfScope && cat != ActorCategory::kCorpse) {
						return true;
					}
				}
			}

			auto* player = RE::PlayerCharacter::GetSingleton();
			if (player && player->QLastRiddenMount() == a_actor->GetHandle()) {
				return true;
			}

			return false;
		}
	}

	bool ShouldAdmitForContainerMenu(RE::Actor* a_actor, RE::ContainerMenu::ContainerMode a_mode) noexcept
	{
		if (!a_actor || a_actor->IsPlayerRef() || a_actor->IsDead()) {
			return false;
		}
		if (ActorScopeExcludeState::IsExcluded(a_actor->GetFormID())) {
			return false;
		}
		const auto& sys = PluginSettings::Get().actorScope;
		const auto cap = ClassifyEquipCapability(a_actor);

		// Commanded actors are admitted first unless they are also teammates.
		if (IsPlayerCommandedActor(a_actor) && !KnownFollowerState::IsPlayerTeammate(a_actor)) {
			return sys.includePlayerSummons &&
				PassesAdmissionFilter(cap, sys.includeNonHumanoidSummons);
		}

		// Inclusion matches preserve per-mode selection and still yield to teammates.
		if (sys.includeInclusionActors && ActorInclusion::MatchesForMode(a_actor, a_mode) &&
		    !KnownFollowerState::IsPlayerTeammate(a_actor)) {
			return PassesAdmissionFilter(cap, sys.includeNonHumanoidInclusionActors);
		}

		// Live teammates are admitted even before they are persisted.
		if (KnownFollowerState::IsPlayerTeammate(a_actor)) {
			return PassesAdmissionFilter(cap, sys.includeNonHumanoidSummons);
		}

		// Former followers are admitted when the toggle allows them.
		if (sys.affectFormerFollowers && KnownFollowerState::IsPersistedKnown(a_actor)) {
			return PassesAdmissionFilter(cap, sys.includeNonHumanoidSummons);
		}

		return false;
	}

	bool ShouldAdmitForQuickTrade(RE::Actor* a_actor) noexcept
	{
		if (!a_actor || a_actor->IsPlayerRef() || a_actor->IsDead()) {
			return false;
		}
		const auto& sys = PluginSettings::Get().actorScope;
		const auto& qt  = PluginSettings::Get().quickTrade;

		if (!qt.enableQuickTrade) {
			return false;
		}

		// Mount access bypasses category scope. When all-world mounts are disabled, only in-scope riders and the player's last ridden mount qualify.
		if (qt.enableQuickTradeForMounts) {
			const bool passes = qt.quickTradeAllWorldMounts
			                        ? a_actor->IsAMount()
			                        : IsPlayerOwnedMount(a_actor);
			if (passes) {
				return true;
			}
		}

		// Excluded actors are always rejected after the mount bypass.
		if (ActorScopeExcludeState::IsExcluded(a_actor->GetFormID())) {
			return false;
		}

		const auto cap = ClassifyEquipCapability(a_actor);

		// Priority order mirrors ResolveEquipPolicy: commanded, inclusion, active teammate, former follower.

		// Commanded actors only qualify here when they are not also teammates.
		if (IsPlayerCommandedActor(a_actor) && !KnownFollowerState::IsPlayerTeammate(a_actor)) {
			return qt.enableQuickTradeForPlayerSummons &&
			       sys.includePlayerSummons &&
			       (PassesAdmissionFilter(cap, sys.includeNonHumanoidSummons) ||
			        (qt.enableQuickTradeForKNoneActors && cap == EquipCapability::kNone));
		}

		// Inclusion actors use MatchesAny here because Quick Trade always opens NPC trade.
		if (sys.includeInclusionActors && ActorInclusion::MatchesAny(a_actor) &&
		    !KnownFollowerState::IsPlayerTeammate(a_actor)) {
			return qt.enableQuickTradeForInclusionActors &&
			       (PassesAdmissionFilter(cap, sys.includeNonHumanoidInclusionActors) ||
			        (qt.enableQuickTradeForKNoneActors && cap == EquipCapability::kNone));
		}

		// Active followers are admitted even before their first persistence pass.
		if (KnownFollowerState::IsPlayerTeammate(a_actor)) {
			return qt.enableQuickTradeForFollowers &&
			       (PassesAdmissionFilter(cap, sys.includeNonHumanoidSummons) ||
			        (qt.enableQuickTradeForKNoneActors && cap == EquipCapability::kNone));
		}

		// Former followers require affectFormerFollowers so Quick Trade matches the main scope.
		if (sys.affectFormerFollowers && KnownFollowerState::IsPersistedKnown(a_actor)) {
			return qt.enableQuickTradeForFormerFollowers &&
			       (PassesAdmissionFilter(cap, sys.includeNonHumanoidSummons) ||
			        (qt.enableQuickTradeForKNoneActors && cap == EquipCapability::kNone));
		}

		return false;
	}
}
