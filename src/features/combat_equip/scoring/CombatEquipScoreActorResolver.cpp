#include "CombatEquipScoreActorResolver.h"

#include "CombatEquipScoreHookSafety.h"

namespace FEC::CombatEquipScoreActorResolver
{
	RE::Actor* TryResolveActor(RE::CombatController* a_controller) noexcept
	{
		using FEC::CombatEquipScoreHookSafety::IsPlausibleGamePointer;
		using FEC::CombatEquipScoreHookSafety::IsPlausiblePolymorphic;

		if (!a_controller) {
			return nullptr;
		}
		if (!IsPlausibleGamePointer(a_controller)) {
			return nullptr;
		}

		auto resolvePtr = [](const RE::NiPointer<RE::Actor>& a_ptr) noexcept -> RE::Actor* {
			auto* actor = a_ptr.get();
			return IsPlausiblePolymorphic(actor) ? actor : nullptr;
		};

		auto resolveHandle = [](const RE::ActorHandle& a_handle) noexcept -> RE::Actor* {
			if (!a_handle) {
				return nullptr;
			}
			auto sp = a_handle.get();
			auto* actor = sp.get();
			return IsPlausiblePolymorphic(actor) ? actor : nullptr;
		};

		if (auto* actor = resolvePtr(a_controller->cachedAttacker)) {
			return actor;
		}
		if (auto* actor = resolveHandle(a_controller->attackerHandle)) {
			return actor;
		}
		// Fallback: some contexts may be keyed off target.
		if (auto* actor = resolvePtr(a_controller->cachedTarget)) {
			return actor;
		}
		if (auto* actor = resolveHandle(a_controller->targetHandle)) {
			return actor;
		}

		return nullptr;
	}
}
