#include "EquipGateTelemetry.h"

#include "ActorScope.h"

#include <format>
#include <mutex>
#include <string>
#include <unordered_map>

namespace FEC::EquipGate::Telemetry
{
	namespace
	{
		constexpr std::uint32_t kCombatInvScoreObsEveryN = 12;
		std::mutex g_combatInvScoreObsLock;
		std::unordered_map<RE::FormID, std::uint32_t> g_combatInvScoreObsCount;

		[[nodiscard]] bool ShouldLogCombatObs(RE::Actor* a_actor)
		{
			if (!spdlog::should_log(spdlog::level::debug)) {
				return false;
			}
			if (!a_actor) {
				return false;
			}
			if (!ActorScope::IsAffectedFollower(a_actor)) {
				return false;
			}
			auto* st = a_actor->AsActorState();
			const bool drawn = (st && st->IsWeaponDrawn());
			const bool inCombat = a_actor->IsInCombat();
			return drawn || inCombat;
		}

		[[nodiscard]] bool ShouldLogCombatInvScoreObs(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return false;
			}
			if (!spdlog::should_log(spdlog::level::debug)) {
				return false;
			}
			if (!ShouldLogCombatObs(a_actor)) {
				return false;
			}
			const auto actorID = a_actor->GetFormID();
			if (actorID == 0) {
				return false;
			}
			{
				std::lock_guard lock(g_combatInvScoreObsLock);
				auto& c = g_combatInvScoreObsCount[actorID];
				++c;
				if ((c % kCombatInvScoreObsEveryN) != 0) {
					return false;
				}
			}
			return true;
		}
	}

	void LogCombatObsCombatInventoryScores(const char* a_outcome, RE::Actor* a_actor)
	{
		if (!ShouldLogCombatInvScoreObs(a_actor)) {
			return;
		}

		auto& rt = a_actor->GetActorRuntimeData();
		auto* cc = rt.combatController;
		auto* inv = cc ? cc->inventory : nullptr;
		if (!inv) {
			return;
		}

		auto* st = a_actor->AsActorState();
		const bool drawn = (st && st->IsWeaponDrawn());
		const bool inCombat = a_actor->IsInCombat();
		const bool isAttacking = a_actor->IsAttacking();
		const auto attackState = st ? static_cast<std::uint32_t>(st->GetAttackState()) : 0;

		std::string buf;
		buf.reserve(2048);
		std::format_to(std::back_inserter(buf),
			"[AI-OBS] CombatInv outcome={} actor={:08X} ({}) inCombat={} drawn={} isAttacking={} attackState={} dirty={} equippedCount={}",
			a_outcome ? a_outcome : "?",
			a_actor->GetFormID(),
			a_actor->GetName(),
			inCombat,
			drawn,
			isAttacking,
			attackState,
			inv->dirty,
			inv->equippedItems.size());

		for (std::uint32_t i = 0; i < inv->equippedItems.size(); i++) {
			auto* item = inv->equippedItems[i].item.get();
			if (!item) {
				continue;
			}
			auto* form = item->item;
			std::format_to(std::back_inserter(buf),
				"\n  equipped idx={} score={} type={} cat={} slot={} item={:08X} ({})",
				i,
				item->itemScore,
				static_cast<std::uint32_t>(item->GetType()),
				static_cast<std::uint32_t>(item->GetCategory()),
				item->itemSlot.slot,
				form ? form->GetFormID() : 0,
				form ? form->GetName() : "NONE");
		}

		for (std::uint32_t bucket = 0; bucket < 7; bucket++) {
			const auto& arr = inv->inventoryItems[bucket];
			const auto limit = std::min<std::uint32_t>(3, arr.size());
			for (std::uint32_t i = 0; i < limit; i++) {
				auto* item = arr[i].get();
				if (!item) {
					continue;
				}
				auto* form = item->item;
				std::format_to(std::back_inserter(buf),
					"\n  bucket={} idx={} score={} type={} cat={} slot={} item={:08X} ({})",
					bucket,
					i,
					item->itemScore,
					static_cast<std::uint32_t>(item->GetType()),
					static_cast<std::uint32_t>(item->GetCategory()),
					item->itemSlot.slot,
					form ? form->GetFormID() : 0,
					form ? form->GetName() : "NONE");
			}
		}

		logger::debug("{}", buf);
	}

	void LogCombatObsEquipObject(
		const char* a_outcome,
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		bool a_aiDriven,
		std::uint32_t a_bypassDepth,
		std::uint32_t a_count,
		bool a_queueEquip,
		bool a_forceEquip,
		bool a_applyNow)
	{
		if (!ShouldLogCombatObs(a_actor)) {
			return;
		}
		auto* st = a_actor->AsActorState();
		const bool drawn = (st && st->IsWeaponDrawn());
		const bool inCombat = a_actor->IsInCombat();
		const bool isAttacking = a_actor ? a_actor->IsAttacking() : false;
		const auto attackState = st ? static_cast<std::uint32_t>(st->GetAttackState()) : 0;
		auto* right = a_actor->GetEquippedObject(false);
		auto* left = a_actor->GetEquippedObject(true);
		logger::debug(
			"[AI-OBS] EquipObject outcome={} actor={:08X} ({}) inCombat={} drawn={} isAttacking={} attackState={} aiDriven={} bypassDepth={} obj={:08X} ({}) formType={} count={} queue={} force={} applyNow={} slot={:p} xList={:p} right={:08X} ({}) left={:08X} ({})",
			a_outcome ? a_outcome : "?",
			a_actor ? a_actor->GetFormID() : 0,
			a_actor ? a_actor->GetName() : "NONE",
			inCombat,
			drawn,
			isAttacking,
			attackState,
			a_aiDriven,
			a_bypassDepth,
			a_object ? a_object->GetFormID() : 0,
			a_object ? a_object->GetName() : "NONE",
			a_object ? static_cast<std::uint32_t>(a_object->GetFormType()) : 0,
			a_count,
			a_queueEquip,
			a_forceEquip,
			a_applyNow,
			static_cast<const void*>(a_slot),
			static_cast<const void*>(a_extraData),
			right ? right->GetFormID() : 0,
			right ? right->GetName() : "NONE",
			left ? left->GetFormID() : 0,
			left ? left->GetName() : "NONE");
	}

	void LogCombatObsUnequipObject(
		const char* a_outcome,
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		RE::ExtraDataList* a_extraData,
		const RE::BGSEquipSlot* a_slot,
		bool a_aiDriven,
		std::uint32_t a_bypassDepth,
		std::uint32_t a_count,
		bool a_queueEquip,
		bool a_forceEquip,
		bool a_applyNow,
		const RE::BGSEquipSlot* a_slotToReplace)
	{
		if (!ShouldLogCombatObs(a_actor)) {
			return;
		}
		auto* st = a_actor->AsActorState();
		const bool drawn = (st && st->IsWeaponDrawn());
		const bool inCombat = a_actor->IsInCombat();
		const bool isAttacking = a_actor ? a_actor->IsAttacking() : false;
		const auto attackState = st ? static_cast<std::uint32_t>(st->GetAttackState()) : 0;
		auto* right = a_actor->GetEquippedObject(false);
		auto* left = a_actor->GetEquippedObject(true);
		logger::debug(
			"[AI-OBS] UnequipObject outcome={} actor={:08X} ({}) inCombat={} drawn={} isAttacking={} attackState={} aiDriven={} bypassDepth={} obj={:08X} ({}) formType={} count={} queue={} force={} applyNow={} slot={:p} xList={:p} slotToReplace={:p} right={:08X} ({}) left={:08X} ({})",
			a_outcome ? a_outcome : "?",
			a_actor ? a_actor->GetFormID() : 0,
			a_actor ? a_actor->GetName() : "NONE",
			inCombat,
			drawn,
			isAttacking,
			attackState,
			a_aiDriven,
			a_bypassDepth,
			a_object ? a_object->GetFormID() : 0,
			a_object ? a_object->GetName() : "NONE",
			a_object ? static_cast<std::uint32_t>(a_object->GetFormType()) : 0,
			a_count,
			a_queueEquip,
			a_forceEquip,
			a_applyNow,
			static_cast<const void*>(a_slot),
			static_cast<const void*>(a_extraData),
			static_cast<const void*>(a_slotToReplace),
			right ? right->GetFormID() : 0,
			right ? right->GetName() : "NONE",
			left ? left->GetFormID() : 0,
			left ? left->GetName() : "NONE");
	}

	void LogCombatObsEquipSpell(
		const char* a_outcome,
		RE::Actor* a_actor,
		RE::SpellItem* a_spell,
		const RE::BGSEquipSlot* a_slot,
		bool a_aiDriven,
		std::uint32_t a_bypassDepth)
	{
		if (!ShouldLogCombatObs(a_actor)) {
			return;
		}
		auto* st = a_actor->AsActorState();
		const bool drawn = (st && st->IsWeaponDrawn());
		const bool inCombat = a_actor->IsInCombat();
		const bool isAttacking = a_actor ? a_actor->IsAttacking() : false;
		const auto attackState = st ? static_cast<std::uint32_t>(st->GetAttackState()) : 0;
		auto* right = a_actor->GetEquippedObject(false);
		auto* left = a_actor->GetEquippedObject(true);
		logger::debug(
			"[AI-OBS] EquipSpell outcome={} actor={:08X} ({}) inCombat={} drawn={} isAttacking={} attackState={} aiDriven={} bypassDepth={} spell={:08X} ({}) slot={:p} right={:08X} ({}) left={:08X} ({})",
			a_outcome ? a_outcome : "?",
			a_actor ? a_actor->GetFormID() : 0,
			a_actor ? a_actor->GetName() : "NONE",
			inCombat,
			drawn,
			isAttacking,
			attackState,
			a_aiDriven,
			a_bypassDepth,
			a_spell ? a_spell->GetFormID() : 0,
			a_spell ? a_spell->GetName() : "NONE",
			static_cast<const void*>(a_slot),
			right ? right->GetFormID() : 0,
			right ? right->GetName() : "NONE",
			left ? left->GetFormID() : 0,
			left ? left->GetName() : "NONE");
	}

	void Clear()
	{
		std::lock_guard lock(g_combatInvScoreObsLock);
		g_combatInvScoreObsCount.clear();
	}

	void EraseActor(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return;
		}

		std::lock_guard lock(g_combatInvScoreObsLock);
		g_combatInvScoreObsCount.erase(a_actorID);
	}
}
