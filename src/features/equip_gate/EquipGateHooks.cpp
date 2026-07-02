#include "EquipGateHooks.h"

#include "EquipGate.h"

#include "BestWeaponAutoEquipSuppressor.h"
#include "CombatEquipOverride.h"
#include "ContainerMenuUtil.h"
#include "EquipGateCore.h"
#include "EquipGateTelemetry.h"
#include "HandItemRestore.h"
#include "InventoryUtil.h"
#include "NonCombatEquipBlocker.h"
#include "ActorScope.h"
#include "KnownFollowerState.h"
#include "OutfitSnapshotRestore.h"
#include "PluginSettings.h"
#include "Relocations.h"
#include "WeaponBound.h"

#include <Windows.h>

#include <detours/detours.h>

#ifdef GetObject
#	undef GetObject
#endif

namespace FEC::EquipGate::Hooks
{
	namespace
	{
		[[nodiscard]] const RE::BGSEquipSlot* GetDefaultEquipSlot(RE::DEFAULT_OBJECT a_id)
		{
			auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
			if (!dom) {
				return nullptr;
			}
			return dom->GetObject<RE::BGSEquipSlot>(a_id);
		}

		[[nodiscard]] const RE::BGSEquipSlot* GetDefaultLeftHandEquipSlot()
		{
			return GetDefaultEquipSlot(RE::DEFAULT_OBJECT::kLeftHandEquip);
		}

		[[nodiscard]] const RE::BGSEquipSlot* GetDefaultRightHandEquipSlot()
		{
			return GetDefaultEquipSlot(RE::DEFAULT_OBJECT::kRightHandEquip);
		}

		[[nodiscard]] const RE::BGSEquipSlot* ComputeEquipSlotForPreferred(
			RE::TESBoundObject* a_preferred,
			const RE::BGSEquipSlot* a_vanillaSlot)
		{
			if (!a_preferred) {
				return a_vanillaSlot;
			}

			const auto* leftHand = GetDefaultLeftHandEquipSlot();
			const auto* rightHand = GetDefaultRightHandEquipSlot();
			const bool requestLeft = (a_vanillaSlot && leftHand && a_vanillaSlot == leftHand);

			if (auto* armor = a_preferred->As<RE::TESObjectARMO>(); armor && armor->IsShield()) {
				return leftHand ? leftHand : a_vanillaSlot;
			}

			if (auto* weapon = a_preferred->As<RE::TESObjectWEAP>()) {
				switch (weapon->GetWeaponType()) {
				case RE::WEAPON_TYPE::kOneHandSword:
				case RE::WEAPON_TYPE::kOneHandDagger:
				case RE::WEAPON_TYPE::kOneHandAxe:
				case RE::WEAPON_TYPE::kOneHandMace:
					return requestLeft ? (leftHand ? leftHand : a_vanillaSlot) : (rightHand ? rightHand : a_vanillaSlot);

				case RE::WEAPON_TYPE::kStaff:
					return requestLeft ? (leftHand ? leftHand : a_vanillaSlot) : (rightHand ? rightHand : a_vanillaSlot);

				case RE::WEAPON_TYPE::kTwoHandSword:
				case RE::WEAPON_TYPE::kTwoHandAxe:
				case RE::WEAPON_TYPE::kBow:
				case RE::WEAPON_TYPE::kCrossbow:
					return nullptr;
				}
			}

			return a_vanillaSlot;
		}

		[[nodiscard]] bool IsShield(RE::TESBoundObject* a_object)
		{
			auto* armor = a_object ? a_object->As<RE::TESObjectARMO>() : nullptr;
			return armor && armor->IsShield();
		}

		[[nodiscard]] bool IsTrackableHandItemOrAmmo(RE::TESBoundObject* a_object)
		{
			if (!a_object) {
				return false;
			}

			const auto formType = a_object->GetFormType();
			if (formType == RE::FormType::Ammo ||
				formType == RE::FormType::Scroll) {
				return true;
			}

			if (a_object->IsWeapon()) {
				return !WeaponBound::IsWeaponAndBound(a_object);
			}

			return IsShield(a_object);
		}

		[[nodiscard]] bool ShouldRequestHandItemReconcile(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			std::uint32_t a_bypassDepth)
		{
			if (a_bypassDepth != 0) {
				return false;
			}
			if (ContainerMenuUtil::IsContainerMenuOpen()) {
				return false;
			}
			if (!a_actor || a_actor->IsPlayerRef() || a_actor->IsDead()) {
				return false;
			}
			if (!ActorScope::IsAffectedFollower(a_actor)) {
				return false;
			}
			auto* st = a_actor->AsActorState();
			if ((st && st->IsWeaponDrawn()) || a_actor->IsInCombat()) {
				return false;
			}
			return IsTrackableHandItemOrAmmo(a_object);
		}

		void RequestHandItemReconcileAfterAttempt(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			std::uint32_t a_bypassDepth,
			const char* a_reason)
		{
			if (!ShouldRequestHandItemReconcile(a_actor, a_object, a_bypassDepth)) {
				return;
			}
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace(
					"EquipGate: requesting HandItemRestore reconcile after {} actor={:08X} obj={:08X}",
					a_reason ? a_reason : "attempt",
					a_actor ? a_actor->GetFormID() : 0,
					a_object ? a_object->GetFormID() : 0);
			}
			HandItemRestore::RequestReconcile(a_actor);
		}

		class ActorEquipManagerEquipObjectHook
		{
		public:
			static void Install();
			static void Uninstall();
		private:
			static void thunk(
				RE::ActorEquipManager* a_this,
				RE::Actor* a_actor,
				RE::TESBoundObject* a_object,
				RE::ExtraDataList* a_extraData,
				std::uint32_t a_count,
				const RE::BGSEquipSlot* a_slot,
				bool a_queueEquip,
				bool a_forceEquip,
				bool a_playSounds,
				bool a_applyNow);

			inline static bool _missingAddrLogged{ false };
			inline static bool _installed{ false };
			inline static decltype(&thunk) _func{ nullptr };
		};

		class ActorEquipManagerUnequipObjectHook
		{
		public:
			static void Install();
			static void Uninstall();
		private:
			static bool thunk(
				RE::ActorEquipManager* a_this,
				RE::Actor* a_actor,
				RE::TESBoundObject* a_object,
				RE::ExtraDataList* a_extraData,
				std::uint32_t a_count,
				const RE::BGSEquipSlot* a_slot,
				bool a_queueEquip,
				bool a_forceEquip,
				bool a_playSounds,
				bool a_applyNow,
				const RE::BGSEquipSlot* a_slotToReplace);

			inline static bool _missingAddrLogged{ false };
			inline static bool _installed{ false };
			inline static decltype(&thunk) _func{ nullptr };
		};

		class ActorEquipManagerEquipSpellHook
		{
		public:
			static void Install();
			static void Uninstall();
		private:
			static void thunk(RE::ActorEquipManager* a_this, RE::Actor* a_actor, RE::SpellItem* a_spell, const RE::BGSEquipSlot* a_slot);

			inline static bool _missingAddrLogged{ false };
			inline static bool _installed{ false };
			inline static decltype(&thunk) _func{ nullptr };
		};
	}

	void ActorEquipManagerEquipObjectHook::Install()
	{
		if (_installed) {
			return;
		}
		const auto addr = RE::Offset::ActorEquipManager::EquipObject.address();
		if (!addr) {
			if (!_missingAddrLogged) {
				logger::warn("EquipGate: ActorEquipManager::EquipObject address not found");
				_missingAddrLogged = true;
			}
			return;
		}
		_func = reinterpret_cast<decltype(_func)>(addr);
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		if (const auto attachErr = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); attachErr != NO_ERROR) {
			DetourTransactionAbort();
			logger::error("EquipGate: failed to attach EquipObject detour (err={})", attachErr);
			return;
		}
		if (DetourTransactionCommit() != NO_ERROR) {
			logger::error("EquipGate: failed to install EquipObject detour");
			return;
		}
		_installed = true;
		logger::info("EquipGate: installed EquipObject detour");
	}

	void ActorEquipManagerEquipObjectHook::Uninstall()
	{
		if (!_installed) {
			return;
		}
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		if (const auto detachErr = DetourDetach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); detachErr != NO_ERROR) {
			DetourTransactionAbort();
			logger::warn("EquipGate: failed to detach EquipObject detour (err={})", detachErr);
			return;
		}
		if (const auto commitErr = DetourTransactionCommit(); commitErr != NO_ERROR) {
			logger::warn("EquipGate: failed to commit EquipObject detour removal (err={})", commitErr);
			return;
		}
		_installed = false;
	}

	void ActorEquipManagerEquipObjectHook::thunk(
		RE::ActorEquipManager* a_this,
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		std::uint32_t a_count,
		const RE::BGSEquipSlot* a_slot,
		bool a_queueEquip,
		bool a_forceEquip,
		bool a_playSounds,
		bool a_applyNow)
	{
		const auto bypassDepth = Core::GetBypassDepth();
		const bool aiDriven = (bypassDepth == 0);
		auto* st = a_actor ? a_actor->AsActorState() : nullptr;
		const bool drawn = (st && st->IsWeaponDrawn());
		const bool trackableObject = a_object && (a_object->IsWeapon() || a_object->IsArmor() || a_object->GetFormType() == RE::FormType::Ammo || a_object->GetFormType() == RE::FormType::Scroll);
		const bool isWeapon = a_object && a_object->IsWeapon();
		const auto* equippedRight = a_actor ? a_actor->GetEquippedObject(false) : nullptr;
		const auto* equippedLeft = a_actor ? a_actor->GetEquippedObject(true) : nullptr;
		const bool alreadyEquipped = isWeapon && (a_object == equippedRight || a_object == equippedLeft);

		std::optional<FEC::CombatEquipOverride::Decision> implDecision;

		const bool traceActor = a_actor && ActorScope::IsAffectedFollower(a_actor);
		if (traceActor) {
			Telemetry::LogCombatObsCombatInventoryScores("equip_call", a_actor);
		}

		if (traceActor && spdlog::should_log(spdlog::level::trace)) {
			const auto invCount = a_object ? FEC::InventoryUtil::GetTotalCount(a_actor, a_object) : 0;
			logger::trace(
				"EquipGate: EquipObject call actor={:08X} ({}) obj={:08X} ({}) aiDriven={} bypassDepth={} trackable={} slot={:p} xList={:p} count={} invCount={} queue={} force={} applyNow={} ",
				a_actor->GetFormID(),
				a_actor->GetName(),
				a_object ? a_object->GetFormID() : 0,
				a_object ? a_object->GetName() : "NONE",
				aiDriven,
				bypassDepth,
				trackableObject,
				static_cast<const void*>(a_slot),
				static_cast<const void*>(a_extraData),
				a_count,
				invCount,
				a_queueEquip,
				a_forceEquip,
				a_applyNow);
		}

		if (bypassDepth == 0 && BestWeaponAutoEquipSuppressor::ShouldSuppressEquip(a_actor, a_object)) {
			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug(
					"BestWeaponAutoEquipSuppressor: suppressed vanilla weapon EquipObject on {:08X} ({}) for {:08X} ({})",
					a_actor ? a_actor->GetFormID() : 0,
					a_actor ? a_actor->GetName() : "NONE",
					a_object ? a_object->GetFormID() : 0,
					a_object ? a_object->GetName() : "NONE");
			}
			Telemetry::LogCombatObsEquipObject(
				"suppressed_auto",
				a_actor,
				a_object,
				a_extraData,
				a_slot,
				aiDriven,
				bypassDepth,
				a_count,
				a_queueEquip,
				a_forceEquip,
				a_applyNow);
			RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "suppressed_auto");
			return;
		}

		if (NonCombatEquipBlocker::ShouldSuppressEquip(a_actor, a_object, aiDriven, drawn)) {
			if (traceActor && spdlog::should_log(spdlog::level::debug)) {
				logger::debug(
					"NonCombatEquipBlocker: suppressed non-combat EquipObject on {:08X} ({}) for {:08X} ({})",
					a_actor ? a_actor->GetFormID() : 0,
					a_actor ? a_actor->GetName() : "NONE",
					a_object ? a_object->GetFormID() : 0,
					a_object ? a_object->GetName() : "NONE");
			}
			RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "suppressed_noncombat");
			return;
		}

		if (aiDriven) {
			implDecision = FEC::CombatEquipOverride::DecideEquipObject(
				a_actor,
				a_object,
				a_extraData,
				a_slot,
				a_count,
				aiDriven,
				drawn);

			if (implDecision.has_value()) {
				using Action = FEC::CombatEquipOverride::DecisionAction;
				switch (implDecision->action) {
				case Action::kBlockEquipAttempt:
					Telemetry::LogCombatObsEquipObject(
						"blocked_impl",
						a_actor,
						a_object,
						a_extraData,
						a_slot,
						aiDriven,
						bypassDepth,
						a_count,
						a_queueEquip,
						a_forceEquip,
						a_applyNow);
					RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "blocked_impl");
					return;

				case Action::kSwapToPreferred: {
					auto* pref = implDecision->preferred.item;
					if (!pref) {
						break;
					}
					const auto* slotToUse = implDecision->slotToUse ? implDecision->slotToUse : ComputeEquipSlotForPreferred(pref, a_slot);
					const auto countToUse = implDecision->countToUse > 0 ? implDecision->countToUse : 1;
					const bool queue = implDecision->queueEquip;
					const bool force = implDecision->forceEquip;
					const bool applyNow2 = implDecision->applyNow;

					ScopedBypass bypass;
					Telemetry::LogCombatObsEquipObject(
						"swap_impl",
						a_actor,
						pref,
						implDecision->preferred.extraData,
						slotToUse,
						false,
						Core::GetBypassDepth(),
						countToUse,
						queue,
						force,
						applyNow2);
					_func(a_this, a_actor, pref, implDecision->preferred.extraData, countToUse, slotToUse, queue, force, a_playSounds, applyNow2);

					RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "swap_impl");
					return;
				}

				case Action::kAllowVanilla:
					break;
				}
			}
		}

		const bool ignoreCombatSafety = false;

		if (Core::ShouldBlock(Operation::kEquip, a_actor, a_object, ignoreCombatSafety)) {
			if (alreadyEquipped) {
				ScopedBypass bypass;
				Telemetry::LogCombatObsEquipObject(
					"allowed_refresh_equipped",
					a_actor,
					a_object,
					a_extraData,
					a_slot,
					aiDriven,
					bypassDepth,
					a_count,
					a_queueEquip,
					a_forceEquip,
					a_applyNow);
				_func(a_this, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip, a_playSounds, a_applyNow);

				RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "allowed_refresh_equipped");
				return;
			}

			if (Core::ConsumePermit(a_actor, a_object, Operation::kEquip)) {
				ScopedBypass bypass;
				Telemetry::LogCombatObsEquipObject(
					"permit_one_shot",
					a_actor,
					a_object,
					a_extraData,
					a_slot,
					aiDriven,
					bypassDepth,
					a_count,
					a_queueEquip,
					a_forceEquip,
					a_applyNow);
				_func(a_this, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip, a_playSounds, a_applyNow);

				if (traceActor && spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"EquipGate: allowed blocked EquipObject via one-shot permit actor={:08X} obj={:08X}",
						a_actor ? a_actor->GetFormID() : 0,
						a_object ? a_object->GetFormID() : 0);
				}
				RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "permit_one_shot");
				return;
			}

			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug(
					"EquipGate: blocked EquipObject on {:08X} ({}) for {:08X} ({}) [queue={}, force={}, applyNow={}]",
					a_actor ? a_actor->GetFormID() : 0,
					a_actor ? a_actor->GetName() : "NONE",
					a_object ? a_object->GetFormID() : 0,
					a_object ? a_object->GetName() : "NONE",
					a_queueEquip,
					a_forceEquip,
					a_applyNow);
			}
			Telemetry::LogCombatObsEquipObject(
				"blocked_gate",
				a_actor,
				a_object,
				a_extraData,
				a_slot,
				aiDriven,
				bypassDepth,
				a_count,
				a_queueEquip,
				a_forceEquip,
				a_applyNow);
			RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "blocked_gate");
			return;
		}

		Telemetry::LogCombatObsEquipObject(
			"allowed_vanilla",
			a_actor,
			a_object,
			a_extraData,
			a_slot,
			aiDriven,
			bypassDepth,
			a_count,
			a_queueEquip,
			a_forceEquip,
			a_applyNow);
		_func(a_this, a_actor, a_object, a_extraData, a_count, a_slot, a_queueEquip, a_forceEquip, a_playSounds, a_applyNow);
		RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "allowed_vanilla");

		if (aiDriven && a_actor && a_object && a_object->IsArmor()) {
			FEC::OutfitSnapshotRestore::OnExternalArmorEquip(a_actor);
		}

	}

	void ActorEquipManagerUnequipObjectHook::Install()
	{
		if (_installed) {
			return;
		}
		const auto addr = RE::Offset::ActorEquipManager::UnequipObject.address();
		if (!addr) {
			if (!_missingAddrLogged) {
				logger::warn("EquipGate: ActorEquipManager::UnequipObject address not found");
				_missingAddrLogged = true;
			}
			return;
		}
		_func = reinterpret_cast<decltype(_func)>(addr);
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		if (const auto attachErr = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); attachErr != NO_ERROR) {
			DetourTransactionAbort();
			logger::error("EquipGate: failed to attach UnequipObject detour (err={})", attachErr);
			return;
		}
		if (DetourTransactionCommit() != NO_ERROR) {
			logger::error("EquipGate: failed to install UnequipObject detour");
			return;
		}
		_installed = true;
		logger::info("EquipGate: installed UnequipObject detour");
	}

	void ActorEquipManagerUnequipObjectHook::Uninstall()
	{
		if (!_installed) {
			return;
		}
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		if (const auto detachErr = DetourDetach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); detachErr != NO_ERROR) {
			DetourTransactionAbort();
			logger::warn("EquipGate: failed to detach UnequipObject detour (err={})", detachErr);
			return;
		}
		if (const auto commitErr = DetourTransactionCommit(); commitErr != NO_ERROR) {
			logger::warn("EquipGate: failed to commit UnequipObject detour removal (err={})", commitErr);
			return;
		}
		_installed = false;
	}

	bool ActorEquipManagerUnequipObjectHook::thunk(
		RE::ActorEquipManager* a_this,
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		std::uint32_t a_count,
		const RE::BGSEquipSlot* a_slot,
		bool a_queueEquip,
		bool a_forceEquip,
		bool a_playSounds,
		bool a_applyNow,
		const RE::BGSEquipSlot* a_slotToReplace)
	{
		const auto bypassDepth = Core::GetBypassDepth();
		const bool aiDriven = (bypassDepth == 0);
		const bool swapFinalize = aiDriven && (a_slotToReplace != nullptr);

		if (a_actor && ActorScope::IsAffectedFollower(a_actor)) {
			// Trace all unequip calls, including relax/sandbox AI outside combat.
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace(
					"EquipGate: UnequipObject call actor={:08X} ({}) obj={:08X} ({}) aiDriven={} bypassDepth={} queue={} force={} applyNow={}",
					a_actor->GetFormID(),
					a_actor->GetName(),
					a_object ? a_object->GetFormID() : 0,
					a_object ? a_object->GetName() : "NULL",
					aiDriven,
					bypassDepth,
					a_queueEquip,
					a_forceEquip,
					a_applyNow);
			}
			Telemetry::LogCombatObsCombatInventoryScores("unequip_call", a_actor);
		}

		if (swapFinalize) {
			ScopedBypass bypass;
			Telemetry::LogCombatObsUnequipObject(
				"allowed_swap_finalize",
				a_actor,
				a_object,
				a_extraData,
				a_slot,
				aiDriven,
				bypassDepth,
				a_count,
				a_queueEquip,
				a_forceEquip,
				a_applyNow,
				a_slotToReplace);
			const bool result = _func(
				a_this,
				a_actor,
				a_object,
				a_extraData,
				a_count,
				a_slot,
				a_queueEquip,
				a_forceEquip,
				a_playSounds,
				a_applyNow,
				a_slotToReplace);

			{
				ScopedBypass bypass2;
				FEC::CombatEquipOverride::OnUnequipObject(a_actor, a_object, aiDriven);
			}
			RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "allowed_swap_finalize");
			return result;
		}

		if (Core::ConsumePermit(a_actor, a_object, Operation::kUnequip)) {
			ScopedBypass bypass;
			Telemetry::LogCombatObsUnequipObject(
				"permit_one_shot",
				a_actor,
				a_object,
				a_extraData,
				a_slot,
				aiDriven,
				bypassDepth,
				a_count,
				a_queueEquip,
				a_forceEquip,
				a_applyNow,
				a_slotToReplace);
			const bool result = _func(
				a_this,
				a_actor,
				a_object,
				a_extraData,
				a_count,
				a_slot,
				a_queueEquip,
				a_forceEquip,
				a_playSounds,
				a_applyNow,
				a_slotToReplace);

			{
				ScopedBypass bypass2;
				FEC::CombatEquipOverride::OnUnequipObject(a_actor, a_object, aiDriven);
			}
			RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "permit_one_shot");
			return result;
		}

		if (aiDriven) {
			if (auto implDecision = FEC::CombatEquipOverride::DecideUnequipObject(
					a_actor,
					a_object,
					a_extraData,
					a_slot,
					a_count,
					aiDriven);
				implDecision.has_value()) {
				using Action = FEC::CombatEquipOverride::UnequipDecisionAction;
				switch (implDecision->action) {
				case Action::kBlockUnequipAttempt:
					Telemetry::LogCombatObsUnequipObject(
						"blocked_impl",
						a_actor,
						a_object,
						a_extraData,
						a_slot,
						aiDriven,
						bypassDepth,
						a_count,
						a_queueEquip,
						a_forceEquip,
						a_applyNow,
						a_slotToReplace);
					RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "blocked_impl");
					return false;
				case Action::kAllowVanilla:
					break;
				}
			}
		}

		if (Core::ShouldBlock(Operation::kUnequip, a_actor, a_object)) {
			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug(
					"EquipGate: blocked UnequipObject on {:08X} ({}) for {:08X} ({}) [queue={}, force={}, applyNow={}]",
					a_actor ? a_actor->GetFormID() : 0,
					a_actor ? a_actor->GetName() : "NONE",
					a_object ? a_object->GetFormID() : 0,
					a_object ? a_object->GetName() : "NONE",
					a_queueEquip,
					a_forceEquip,
					a_applyNow);
			}
			Telemetry::LogCombatObsUnequipObject(
				"blocked_gate",
				a_actor,
				a_object,
				a_extraData,
				a_slot,
				aiDriven,
				bypassDepth,
				a_count,
				a_queueEquip,
				a_forceEquip,
				a_applyNow,
				a_slotToReplace);
			RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "blocked_gate");
			return false;
		}

		Telemetry::LogCombatObsUnequipObject(
			"allowed_vanilla",
			a_actor,
			a_object,
			a_extraData,
			a_slot,
			aiDriven,
			bypassDepth,
			a_count,
			a_queueEquip,
			a_forceEquip,
			a_applyNow,
			a_slotToReplace);
		const bool result = _func(
			a_this,
			a_actor,
			a_object,
			a_extraData,
			a_count,
			a_slot,
			a_queueEquip,
			a_forceEquip,
			a_playSounds,
			a_applyNow,
			a_slotToReplace);

		{
			ScopedBypass bypass;
			FEC::CombatEquipOverride::OnUnequipObject(a_actor, a_object, aiDriven);
		}
		RequestHandItemReconcileAfterAttempt(a_actor, a_object, bypassDepth, "allowed_vanilla");
		return result;
	}

	void ActorEquipManagerEquipSpellHook::Install()
	{
		if (_installed) {
			return;
		}
		const auto addr = Relocations::kActorEquipManagerEquipSpell.address();
		if (!addr) {
			if (!_missingAddrLogged) {
				logger::warn("EquipGate: ActorEquipManager::EquipSpell address not found");
				_missingAddrLogged = true;
			}
			return;
		}
		_func = reinterpret_cast<decltype(_func)>(addr);
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		if (const auto attachErr = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); attachErr != NO_ERROR) {
			DetourTransactionAbort();
			logger::error("EquipGate: failed to attach EquipSpell detour (err={})", attachErr);
			return;
		}
		if (DetourTransactionCommit() != NO_ERROR) {
			logger::error("EquipGate: failed to install EquipSpell detour");
			return;
		}
		_installed = true;
		logger::info("EquipGate: installed EquipSpell detour");
	}

	void ActorEquipManagerEquipSpellHook::Uninstall()
	{
		if (!_installed) {
			return;
		}
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		if (const auto detachErr = DetourDetach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); detachErr != NO_ERROR) {
			DetourTransactionAbort();
			logger::warn("EquipGate: failed to detach EquipSpell detour (err={})", detachErr);
			return;
		}
		if (const auto commitErr = DetourTransactionCommit(); commitErr != NO_ERROR) {
			logger::warn("EquipGate: failed to commit EquipSpell detour removal (err={})", commitErr);
			return;
		}
		_installed = false;
	}

	void ActorEquipManagerEquipSpellHook::thunk(RE::ActorEquipManager* a_this, RE::Actor* a_actor, RE::SpellItem* a_spell, const RE::BGSEquipSlot* a_slot)
	{
		if (a_actor && ActorScope::IsAffectedFollower(a_actor)) {
			Telemetry::LogCombatObsCombatInventoryScores("equipspell_call", a_actor);
		}
		{
			ScopedBypass bypass;
			_func(a_this, a_actor, a_spell, a_slot);
		}
	}

	void Install()
	{
		ActorEquipManagerEquipSpellHook::Install();
		ActorEquipManagerEquipObjectHook::Install();
		ActorEquipManagerUnequipObjectHook::Install();
	}

	void Uninstall()
	{
		ActorEquipManagerUnequipObjectHook::Uninstall();
		ActorEquipManagerEquipObjectHook::Uninstall();
		ActorEquipManagerEquipSpellHook::Uninstall();
	}
}
