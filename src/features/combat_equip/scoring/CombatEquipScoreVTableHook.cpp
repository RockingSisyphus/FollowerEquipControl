#include "CombatEquipScoreVTableHook.h"

#include "ContainerMenuUtil.h"
#include "ActorScope.h"
#include "CombatEquipScore.h"
#include "CombatEquipScoreActorResolver.h"
#include "CombatEquipScoreApplier.h"
#include "CombatEquipScoreHookSafety.h"
#include "CombatEquipScorePolicy.h"
#include "CombatEquipScoreTelemetry.h"
#include "MemoryUtil.h"
#include "PluginSettings.h"
#include "Relocations.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace FEC::CombatEquipScoreVTableHook
{
	namespace
	{
		using FEC::CombatEquipScoreHookSafety::IsPlausibleGamePointer;
		using FEC::CombatEquipScoreHookSafety::IsPlausiblePolymorphic;

		std::atomic<bool> g_installed{ false };
		std::mutex g_installLock;

		using CalculateScoreFn = float (*)(RE::CombatInventoryItem*, RE::CombatController*);
		inline std::unordered_map<std::uintptr_t, std::uintptr_t> g_originalByVtable;

		struct VFuncHook
		{
			REL::Relocation<std::uintptr_t> vtbl{};
			std::uintptr_t original{ 0 };
			bool installed{ false };

			void Install(REL::VariantID a_vtblReloc)
			{
				if (installed) {
					return;
				}

				vtbl = REL::Relocation<std::uintptr_t>(a_vtblReloc);
				if (!vtbl.address()) {
					return;
				}

				original = vtbl.write_vfunc(Relocations::kCombatInventoryItem_CalculateScoreVfuncIndex, thunk);
				installed = (original != 0);
			}

			void Uninstall()
			{
				if (!installed || !vtbl.address() || original == 0) {
					return;
				}

				// Restore only if the slot still points to our thunk.
				constexpr auto kIdx = Relocations::kCombatInventoryItem_CalculateScoreVfuncIndex;
				auto* const vptr = reinterpret_cast<std::uintptr_t*>(vtbl.address());
				if (!MemoryUtil::IsReadableMemory(vptr, (kIdx + 1) * sizeof(std::uintptr_t))) {
					return;
				}
				const auto current = vptr[kIdx];
				const auto ours = reinterpret_cast<std::uintptr_t>(&thunk);
				if (current != ours) {
					return;
				}

				(void)vtbl.write_vfunc(kIdx, original);
				installed = false;
			}

			static float thunk(RE::CombatInventoryItem* a_this, RE::CombatController* a_controller)
			{
				auto& ctr = CombatEquipScoreTelemetry::Counters();
				ctr.entered.fetch_add(1);

				if (!IsPlausibleGamePointer(a_this)) {
					ctr.errBadThis.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("skip:bad_this", a_controller, nullptr, nullptr, nullptr, 0.0f, 0);
					return 0.0f;
				}

				auto* vptr = a_this ? *reinterpret_cast<std::uintptr_t* const*>(a_this) : nullptr;
				if (!vptr) {
					ctr.errBadVptr.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("skip:null_vptr", a_controller, nullptr, nullptr, nullptr, 0.0f, 0);
					return 0.0f;
				}
				if (!IsPlausibleGamePointer(vptr)) {
					ctr.errBadVptr.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("skip:bad_vptr", a_controller, nullptr, nullptr, nullptr, 0.0f, reinterpret_cast<std::uintptr_t>(vptr));
					return 0.0f;
				}

				const auto it = g_originalByVtable.find(reinterpret_cast<std::uintptr_t>(vptr));
				if (it == g_originalByVtable.end() || it->second == 0) {
					ctr.errNoOriginal.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("skip:no_original", a_controller, nullptr, nullptr, nullptr, 0.0f, reinterpret_cast<std::uintptr_t>(vptr));
					return 0.0f;
				}

				auto orig = reinterpret_cast<CalculateScoreFn>(it->second);
				float score = orig(a_this, a_controller);

				RE::Actor* actor = CombatEquipScoreActorResolver::TryResolveActor(a_controller);
				auto* form = a_this ? a_this->item : nullptr;
				const auto* slot = a_this ? a_this->itemSlot.equipSlot : nullptr;
				if (slot && !IsPlausibleGamePointer(slot)) {
					slot = nullptr;
				}

				// Fail open with explicit skip reasons for telemetry.
				if (!CombatEquipScoreController::IsEnabled()) {
					ctr.passDisabled.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("pass:disabled", a_controller, actor, form, slot, score, reinterpret_cast<std::uintptr_t>(vptr));
					return score;
				}
				if (!actor) {
					ctr.passNoActor.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("pass:no_actor", a_controller, nullptr, form, slot, score, reinterpret_cast<std::uintptr_t>(vptr));
					return score;
				}
				if (!IsPlausiblePolymorphic(actor)) {
					ctr.passBadActor.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("pass:bad_actor", a_controller, nullptr, form, slot, score, reinterpret_cast<std::uintptr_t>(vptr));
					return score;
				}
				if (actor->IsPlayerRef()) {
					ctr.passPlayer.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("pass:player", a_controller, actor, form, slot, score, reinterpret_cast<std::uintptr_t>(vptr));
					return score;
				}
				if (ContainerMenuUtil::IsContainerMenuOpen()) {
					ctr.passMenuOpen.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("pass:menu_open", a_controller, actor, form, slot, score, reinterpret_cast<std::uintptr_t>(vptr));
					return score;
				}
				if (!ActorScope::IsAffectedFollower(actor)) {
					ctr.passNotPersistedFollower.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("pass:not_persisted_follower", a_controller, actor, form, slot, score, reinterpret_cast<std::uintptr_t>(vptr));
					return score;
				}
				auto* st = actor->AsActorState();
				const bool drawn = (st && st->IsWeaponDrawn());
				const bool inCombat = actor->IsInCombat();
				if (!(drawn || inCombat)) {
					ctr.passNotCombatOrDrawn.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("pass:not_combat_or_drawn", a_controller, actor, form, slot, score, reinterpret_cast<std::uintptr_t>(vptr));
					return score;
				}

				if (!IsPlausiblePolymorphic(form)) {
					ctr.passBadItem.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("pass:bad_item", a_controller, actor, form, slot, score, reinterpret_cast<std::uintptr_t>(vptr));
					return score;
				}

				// Keep the policy call for parity even though the thunk applies the same gates.
				(void)CombatEquipScorePolicy::ShouldBiasForActor(actor);

				const float after = CombatEquipScoreApplier::MaybeApplyPreferredBonus(actor, form, slot, score);
				if (after > score + 0.0001f) {
					ctr.applied.fetch_add(1);
					CombatEquipScoreTelemetry::TraceHookStatus("applied", a_controller, actor, form, slot, after, reinterpret_cast<std::uintptr_t>(vptr));
					return after;
				}

				ctr.passNoMatch.fetch_add(1);
				CombatEquipScoreTelemetry::TraceHookStatus("pass:no_match", a_controller, actor, form, slot, score, reinterpret_cast<std::uintptr_t>(vptr));
				return score;
			}
		};

		inline VFuncHook g_itemBase;
		inline VFuncHook g_itemStaff;
		inline VFuncHook g_itemMelee;
		inline VFuncHook g_itemRanged;
		inline VFuncHook g_itemShield;
		inline std::vector<VFuncHook> g_itemStaffMagic;

		void InstallVTableHook(VFuncHook& a_hook, REL::VariantID a_vtblReloc)
		{
			REL::Relocation<std::uintptr_t> vtbl{ a_vtblReloc };
			if (!vtbl.address()) {
				return;
			}

			constexpr std::size_t kScoreVfuncIndex = Relocations::kCombatInventoryItem_CalculateScoreVfuncIndex;
			auto* const vptr = reinterpret_cast<std::uintptr_t*>(vtbl.address());
			if (!MemoryUtil::IsReadableMemory(vptr, (kScoreVfuncIndex + 1) * sizeof(std::uintptr_t))) {
				logger::warn(
					"CombatEquipScore: refusing to hook; vtable unreadable (vtbl={:p})",
					reinterpret_cast<const void*>(vtbl.address()));
				return;
			}
			const auto current = vptr[kScoreVfuncIndex];
			if (!MemoryUtil::IsPlausiblePointer(reinterpret_cast<const void*>(current)) ||
				!MemoryUtil::IsExecutableMemory(reinterpret_cast<const void*>(current))) {
				logger::warn(
					"CombatEquipScore: refusing to hook; unexpected vfunc target (vtbl={:p}, idx={}, current={:p})",
					reinterpret_cast<const void*>(vtbl.address()),
					kScoreVfuncIndex,
					reinterpret_cast<const void*>(current));
				return;
			}

			g_originalByVtable[vtbl.address()] = current;
			a_hook.Install(a_vtblReloc);
			if (a_hook.installed) {
				logger::debug(
					"CombatEquipScore: signature hooked vtbl={:p} idx=0x{:X} original={:p}",
					reinterpret_cast<const void*>(vtbl.address()),
					kScoreVfuncIndex,
					reinterpret_cast<const void*>(current));
			}
		}
	}

	void Install()
	{
		if (g_installed.load()) {
			return;
		}

		std::lock_guard lock(g_installLock);
		if (g_installed.load()) {
			return;
		}

		// Hook CalculateScore across the known CombatInventoryItem derived vtables.
		InstallVTableHook(g_itemBase, RE::VTABLE_CombatInventoryItem[0]);
		InstallVTableHook(g_itemMelee, RE::VTABLE_CombatInventoryItemMelee[0]);
		InstallVTableHook(g_itemRanged, RE::VTABLE_CombatInventoryItemRanged[0]);
		InstallVTableHook(g_itemShield, RE::VTABLE_CombatInventoryItemShield[0]);
		InstallVTableHook(g_itemStaff, RE::VTABLE_CombatInventoryItemStaff[0]);

		// Staffs can use CombatInventoryItemMagicT<CombatInventoryItemStaff, CombatMagicCasterX>
		// vtables instead of the plain staff vtable, so hook each known template instance.
		{
			static constexpr REL::VariantID kStaffMagicVTables[] = {
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterScript_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterParalyze_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterReanimate_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterTargetEffect_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterArmor_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterBoundItem_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterInvisibility_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterLight_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterCloak_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterDisarm_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterStagger_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterSummon_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterRestore_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterWard_[0],
				RE::VTABLE_CombatInventoryItemMagicT_CombatInventoryItemStaff_CombatMagicCasterOffensive_[0],
			};

			g_itemStaffMagic.clear();
			g_itemStaffMagic.reserve(std::size(kStaffMagicVTables));
			for (auto id : kStaffMagicVTables) {
				g_itemStaffMagic.emplace_back();
				InstallVTableHook(g_itemStaffMagic.back(), id);
			}
		}

		g_installed.store(true);
		logger::info("CombatEquipScore: installed CombatInventoryItem::CalculateScore vfunc hooks");
	}

	void Uninstall()
	{
		if (!g_installed.load()) {
			return;
		}

		std::lock_guard lock(g_installLock);
		if (!g_installed.load()) {
			return;
		}

		for (auto& h : g_itemStaffMagic) {
			h.Uninstall();
		}
		g_itemStaffMagic.clear();

		g_itemStaff.Uninstall();
		g_itemShield.Uninstall();
		g_itemRanged.Uninstall();
		g_itemMelee.Uninstall();
		g_itemBase.Uninstall();

		g_originalByVtable.clear();
		g_installed.store(false);
		logger::info("CombatEquipScore: uninstalled CombatInventoryItem::CalculateScore vfunc hooks");
	}
}
