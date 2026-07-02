#include "EquipModeAction.h"

#include "ActorScope.h"
#include "ContainerMenuUtil.h"
#include "EquipGate.h"
#include "StrictPick.h"
#include "StrictPickUtil.h"

#include "CombatEquipIconInjector.h"
#include "HandItemIconInjector.h"
#include "OutfitSyncIconInjector.h"

#include "PreferenceCapture.h"
#include "Notifications.h"
#include "UISounds.h"

#include "AmmoMode.h"
#include "ArmorMode.h"
#include "ScrollMode.h"
#include "TorchMode.h"
#include "WeaponMode.h"

#include "SyntheticXList.h"

#include <RE/B/BGSEquipSlot.h>
#include <RE/E/ExtraWorn.h>
#include <RE/E/ExtraWornLeft.h>
#include <RE/I/InventoryChanges.h>
#include <RE/I/InventoryEntryData.h>
#include <RE/T/TESForm.h>

namespace FEC::EquipMode::Core
{
	namespace
	{
		[[nodiscard]] bool InferLeftHandForCapture(RE::TESBoundObject* a_object, const RE::BGSEquipSlot* a_slot) noexcept
		{
			if (auto* armo = a_object ? a_object->As<RE::TESObjectARMO>() : nullptr; armo && armo->IsShield()) {
				return true;
			}
			return a_slot && a_slot == Core::GetLeftHandSlot();
		}

		[[nodiscard]] bool ShouldLogEquipModeTrace() noexcept
		{
			return spdlog::should_log(spdlog::level::debug);
		}

		[[nodiscard]] RE::FormID SafeFormID(const RE::TESForm* a_form) noexcept
		{
			return a_form ? a_form->GetFormID() : 0;
		}

		[[nodiscard]] const char* SafeName(const RE::TESForm* a_form) noexcept
		{
			if (!a_form) {
				return "";
			}

			const auto* name = a_form->GetName();
			return name ? name : "";
		}

		[[nodiscard]] const char* SafeName(const RE::Actor* a_actor) noexcept
		{
			if (!a_actor) {
				return "";
			}

			if (const auto* name = a_actor->GetName(); name && *name) {
				return name;
			}

			if (const auto* base = a_actor->GetActorBase()) {
				if (const auto* name = base->GetName(); name && *name) {
					return name;
				}
			}

			return "";
		}



		template <class Fn>
		void LogEquipModeTraceDebug(Fn&& a_fn)
		{
			if (ShouldLogEquipModeTrace()) {
				a_fn();
			}
		}

		template <class Fn>
		void LogEquipModeTraceTrace(Fn&& a_fn)
		{
			if (spdlog::should_log(spdlog::level::trace)) {
				a_fn();
			}
		}

		void LogActorHands(RE::Actor* a_actor, std::string_view a_tag)
		{
			if (!ShouldLogEquipModeTrace() || !a_actor) {
				return;
			}

			auto* left = a_actor->GetEquippedObject(true);
			auto* right = a_actor->GetEquippedObject(false);
			logger::debug(
				"EquipModeTrace: {} actor={:08X} name='{}' equippedL={:08X} '{}' equippedR={:08X} '{}'",
				a_tag,
				SafeFormID(a_actor),
				SafeName(a_actor),
				SafeFormID(left),
				SafeName(left),
				SafeFormID(right),
				SafeName(right));
		}

		void LogExtraLists(RE::InventoryEntryData* a_entry)
		{
			if (!spdlog::should_log(spdlog::level::trace)) {
				return;
			}
			if (!a_entry || !a_entry->extraLists) {
				LogEquipModeTraceTrace([&] { logger::trace("EquipModeTrace: extraLists entry/extraLists null"); });
				return;
			}
			std::uint32_t nonNull = 0;
			for (auto* x : *a_entry->extraLists) {
				if (x) {
					++nonNull;
				}
			}
			LogEquipModeTraceTrace([&] { logger::trace("EquipModeTrace: extraLists nonNullCount={} countDelta={}", nonNull, a_entry->countDelta); });
			std::uint32_t i = 0;
			for (auto* x : *a_entry->extraLists) {
				if (!x) {
					++i;
					continue;
				}
				LogEquipModeTraceTrace([&] { logger::trace(
					"EquipModeTrace: xList[{}]={:p} wornAny={} wornL={} wornR={}",
					i,
					static_cast<void*>(x),
					Core::HasWornExtra(*x),
					Core::HasWornLeftExtra(*x),
					Core::HasWornRightExtra(*x)); });
				++i;
			}
		}

		void LogActorObjectExtraLists(RE::Actor* a_actor, RE::TESBoundObject* a_object, std::string_view a_tag)
		{
			if (!spdlog::should_log(spdlog::level::trace)) {
				return;
			}
			if (!a_actor || !a_object) {
				return;
			}
			auto* changes = a_actor->GetInventoryChanges();
			if (!changes || !changes->entryList) {
				LogEquipModeTraceTrace([&] { logger::trace(
					"EquipModeTrace: {} invDump object not found actor={:08X} object={:08X} '{}'",
					a_tag,
					SafeFormID(a_actor),
					SafeFormID(a_object),
					SafeName(a_object)); });
				return;
			}
			RE::InventoryEntryData* entry = nullptr;
			for (auto* e : *changes->entryList) {
				if (e && e->GetObject() == a_object) {
					entry = e;
					break;
				}
			}
			if (!entry) {
				LogEquipModeTraceTrace([&] { logger::trace(
					"EquipModeTrace: {} invDump object not found actor={:08X} object={:08X} '{}'",
					a_tag,
					SafeFormID(a_actor),
					SafeFormID(a_object),
					SafeName(a_object)); });
				return;
			}
			LogEquipModeTraceTrace([&] { logger::trace(
				"EquipModeTrace: {} invDump actor={:08X} object={:08X} '{}' entry={:p}",
				a_tag,
				SafeFormID(a_actor),
				SafeFormID(a_object),
				SafeName(a_object),
				static_cast<void*>(entry)); });
			LogExtraLists(entry);
		}

		enum class EquipOp
		{
			kEquip,
			kUnequip,
		};

		void DoEquipOp(
			RE::ActorEquipManager* a_equipMan,
			EquipOp a_op,
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			RE::ExtraDataList* a_xList,
			const RE::BGSEquipSlot* a_slot,
			bool a_hasSelection)
		{
			if (!a_equipMan || !a_actor || !a_object) {
				return;
			}

			if (a_op == EquipOp::kEquip) {
				Preference::CaptureEquip(a_actor, a_object, InferLeftHandForCapture(a_object, a_slot), a_xList, a_hasSelection);
			} else {
				Preference::CaptureUnequip(a_actor, a_object, InferLeftHandForCapture(a_object, a_slot));
			}

			EquipGate::ScopedBypass bypass;

			if (a_op == EquipOp::kEquip) {
				// New xList addresses created by EquipObject(applyNow=true) are assumed to stay live until ContainerMenu finishes.
				if (spdlog::should_log(spdlog::level::trace)) {
					LogActorObjectExtraLists(a_actor, a_object, "DoEquipOp-pre-applyNow");
				}
				a_equipMan->EquipObject(a_actor, a_object, a_xList, 1, a_slot, false, false, true, true);
				if (spdlog::should_log(spdlog::level::trace)) {
					LogActorObjectExtraLists(a_actor, a_object, "DoEquipOp-post-applyNow");
				}
				FEC::UISounds::PlayForObject(a_object, FEC::UISounds::Action::kPickup);
			} else {
				if (spdlog::should_log(spdlog::level::trace)) {
					LogActorObjectExtraLists(a_actor, a_object, "DoEquipOp-pre-unequip");
				}
				a_equipMan->UnequipObject(a_actor, a_object, a_xList, 1, a_slot, false, false, true, true);
				if (spdlog::should_log(spdlog::level::trace)) {
					LogActorObjectExtraLists(a_actor, a_object, "DoEquipOp-post-unequip");
				}
				FEC::UISounds::PlayForObject(a_object, FEC::UISounds::Action::kPutdown);
			}
		}

		void EquipStrict(RE::Actor* a_actor, RE::TESBoundObject* a_object, RE::ExtraDataList* a_xList, const RE::BGSEquipSlot* a_slot, bool a_hasSelection)
		{
			if (!a_actor || !a_object || !a_xList) {
				return;
			}
			if (spdlog::should_log(spdlog::level::trace)) {
				LogEquipModeTraceTrace([&] { logger::trace(
					"EquipModeTrace: Equip(strict) actor={:08X} '{}' object={:08X} '{}' xList={:p} slot={:08X} '{}'",
					SafeFormID(a_actor),
					SafeName(a_actor),
					SafeFormID(a_object),
					SafeName(a_object),
					static_cast<void*>(a_xList),
					SafeFormID(a_slot),
					SafeName(a_slot)); });
			}
			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return;
			}
			DoEquipOp(equipMan, EquipOp::kEquip, a_actor, a_object, a_xList, a_slot, a_hasSelection);
		}

		void EquipBase(RE::Actor* a_actor, RE::TESBoundObject* a_object, const RE::BGSEquipSlot* a_slot, bool a_hasSelection)
		{
			if (!a_actor || !a_object) {
				return;
			}
			if (spdlog::should_log(spdlog::level::trace)) {
				LogEquipModeTraceTrace([&] { logger::trace(
					"EquipModeTrace: Equip(base) actor={:08X} '{}' object={:08X} '{}' slot={:08X} '{}'",
					SafeFormID(a_actor),
					SafeName(a_actor),
					SafeFormID(a_object),
					SafeName(a_object),
					SafeFormID(a_slot),
					SafeName(a_slot)); });
			}
			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return;
			}
			DoEquipOp(equipMan, EquipOp::kEquip, a_actor, a_object, nullptr, a_slot, a_hasSelection);
		}

		void UnequipStrict(RE::Actor* a_actor, RE::TESBoundObject* a_object, RE::ExtraDataList* a_xList, const RE::BGSEquipSlot* a_slot)
		{
			if (!a_actor || !a_object || !a_xList) {
				return;
			}
			if (spdlog::should_log(spdlog::level::trace)) {
				LogEquipModeTraceTrace([&] { logger::trace(
					"EquipModeTrace: Unequip(strict) actor={:08X} '{}' object={:08X} '{}' xList={:p} slot={:08X} '{}'",
					SafeFormID(a_actor),
					SafeName(a_actor),
					SafeFormID(a_object),
					SafeName(a_object),
					static_cast<void*>(a_xList),
					SafeFormID(a_slot),
					SafeName(a_slot)); });
			}
			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return;
			}
			DoEquipOp(equipMan, EquipOp::kUnequip, a_actor, a_object, a_xList, a_slot, false);
		}

		void UnequipBase(RE::Actor* a_actor, RE::TESBoundObject* a_object, const RE::BGSEquipSlot* a_slot)
		{
			if (!a_actor || !a_object) {
				return;
			}
			if (spdlog::should_log(spdlog::level::trace)) {
				LogEquipModeTraceTrace([&] { logger::trace(
					"EquipModeTrace: Unequip(base) actor={:08X} '{}' object={:08X} '{}' slot={:08X} '{}'",
					SafeFormID(a_actor),
					SafeName(a_actor),
					SafeFormID(a_object),
					SafeName(a_object),
					SafeFormID(a_slot),
					SafeName(a_slot)); });
			}
			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return;
			}
			DoEquipOp(equipMan, EquipOp::kUnequip, a_actor, a_object, nullptr, a_slot, false);
		}

		[[nodiscard]] const RE::TESForm* GetEquippedObjectForSlot(RE::Actor* a_actor, const RE::BGSEquipSlot* a_slot)
		{
			if (!a_actor || !a_slot) {
				return nullptr;
			}
			if (a_slot == Core::GetLeftHandSlot()) {
				return a_actor->GetEquippedObject(true);
			}
			if (a_slot == Core::GetRightHandSlot()) {
				return a_actor->GetEquippedObject(false);
			}
			return nullptr;
		}

		[[nodiscard]] std::optional<Core::StrictPick> TryBuildBaseFallbackPick(
			RE::Actor* a_target,
			RE::InventoryEntryData* a_entry,
			RE::TESBoundObject* a_object,
			Hand a_hand,
			bool a_equipOnly,
			std::int32_t a_rowCount,
			bool a_rowIsWorn)
		{
			if (!a_target || !a_entry || !a_object) {
				return std::nullopt;
			}

			Core::StrictPick out{};
			out.baseFallback = true;
			out.object = a_object;
			out.slot = Core::GetSlotForObject(a_object, a_hand);

			const std::int32_t rowCount = (a_rowCount > 0) ? a_rowCount : 1;

			if (a_object->IsWeapon() || a_object->GetFormType() == RE::FormType::Scroll || a_object->GetFormType() == RE::FormType::Light) {
				const bool twoHanded = Core::IsTwoHandedObject(a_object);
				if (twoHanded) {
					out.slot = Core::GetRightHandSlot();
				}
				const bool isHandSlotItem = (out.slot == Core::GetLeftHandSlot()) || (out.slot == Core::GetRightHandSlot());
				if (!isHandSlotItem) {
					return std::nullopt;
				}

				const auto* equippedInDesired = GetEquippedObjectForSlot(a_target, out.slot);
				const bool inDesired = equippedInDesired && equippedInDesired->GetFormID() == SafeFormID(a_object);
				const Core::Hand otherHand = (out.slot == Core::GetLeftHandSlot()) ? Core::Hand::kRight : Core::Hand::kLeft;
				const auto* otherSlot = Core::GetSlotForHand(otherHand);
				const auto* equippedInOther = GetEquippedObjectForSlot(a_target, otherSlot);
				const bool inOther = equippedInOther && equippedInOther->GetFormID() == SafeFormID(a_object);

				if (a_equipOnly) {
					if (inOther && !inDesired) {
						out.swapMove = true;
						out.fromSlot = otherSlot;
					}
					out.unequip = false;
					return out;
				}

				if (inDesired) {
					if (a_rowIsWorn) {
						out.unequip = true;
						return out;
					}
					// Another instance of this form is in the desired slot; let the engine replace it.
				}

				if (inOther) {
					// With multiple copies, prefer a second copy over moving the existing one.
					if (rowCount <= 1 || twoHanded) {
						out.swapMove = true;
						out.fromSlot = otherSlot;
					}
				}

				out.unequip = false;
				return out;
			}

			if (a_object->IsArmor()) {
				const bool worn = a_target->GetWornArmor(SafeFormID(a_object)) != nullptr;
				out.unequip = (!a_equipOnly && worn && a_rowIsWorn);
				return out;
			}

			if (a_object->GetFormType() == RE::FormType::Ammo) {
				// GetCurrentAmmo() is reliable for live actors, but dead actors need the row worn-state flag.
				bool equipped = false;
				if (a_target->IsDead()) {
					equipped = a_rowIsWorn;
				} else {
					auto* current = a_target->GetCurrentAmmo();
					equipped = current && current->GetFormID() == SafeFormID(a_object);
				}
				out.unequip = (!a_equipOnly && equipped);
				return out;
			}

			return std::nullopt;
		}

		[[nodiscard]] RE::InventoryEntryData* GetStrictEntryForSelection(
			RE::Actor* a_target,
			RE::InventoryEntryData* a_uiEntry,
			RE::TESBoundObject* a_object)
		{
			if (!a_uiEntry || !a_object) {
				return nullptr;
			}

			// Use only actor-owned InventoryEntryData. UI clones can retain engine-freed xList pointers
			// after base-fallback equip/unequip cycles, and HasType<>/IsWorn/GetExtraData can AV.
			if (!a_target) {
				return nullptr;
			}

			auto* changes = a_target->GetInventoryChanges();
			if (!changes || !changes->entryList) {
				return nullptr;
			}

			for (auto* realEntry : *changes->entryList) {
				if (!realEntry || realEntry->GetObject() != a_object) {
					continue;
				}

				if ((!realEntry->extraLists || realEntry->extraLists->empty())) {
					(void)SyntheticXList::EnsureXList(a_target, realEntry);
				}

				if (spdlog::should_log(spdlog::level::trace)) {
					LogEquipModeTraceTrace([&] { logger::trace(
						"EquipModeTrace: strictPick using actor entry object={:08X} '{}' entry={:p}",
						SafeFormID(a_object),
						SafeName(a_object),
						static_cast<void*>(realEntry)); });
				}
				return realEntry;
			}

			return nullptr;
		}

		[[nodiscard]] std::optional<Core::StrictPick> TryBuildStrictPickFromSelection(
			RE::ContainerMenu* a_menu,
			RE::Actor* a_target,
			Hand a_hand,
			bool a_equipOnly)
		{
			auto fail = [&](std::string_view why) -> std::optional<Core::StrictPick> {
				LogEquipModeTraceTrace([&] { logger::trace("EquipModeTrace: strictPick fail reason={}", why); });
				return std::nullopt;
			};

			if (!a_menu || !a_target) {
				return fail("menuOrTargetNull");
			}
			auto* itemList = a_menu->GetRuntimeData().itemList;
			if (!itemList) {
				return fail("itemListNull");
			}
			auto* selected = itemList->GetSelectedItem();
			if (!selected) {
				return fail("selectedNull");
			}
			auto* entry = selected->data.objDesc;
			if (!entry) {
				return fail("objDescNull");
			}
			auto* object = entry->GetObject();
			if (!object) {
				return fail("objectNull");
			}

			// Do not call UI-clone InventoryEntryData virtuals here. GetCount, GetName, and
			// GetEquipState walk extraLists and can dereference stale xList pointers.
			const std::int32_t rowCount = (entry->countDelta > 0) ? entry->countDelta : 1;
			const bool rowIsWorn = [&]() -> bool {
				if (!a_target || !object) {
					return false;
				}
				if (object->IsWeapon() || object->GetFormType() == RE::FormType::Scroll || object->GetFormType() == RE::FormType::Light) {
					auto* l = a_target->GetEquippedObject(true);
					if (l && l->GetFormID() == SafeFormID(object)) {
						return true;
					}
					auto* r = a_target->GetEquippedObject(false);
					return r && r->GetFormID() == SafeFormID(object);
				}
				if (object->IsArmor()) {
					return a_target->GetWornArmor(SafeFormID(object)) != nullptr;
				}
				if (object->GetFormType() == RE::FormType::Ammo) {
					if (auto* c = a_target->GetCurrentAmmo()) {
						return c->GetFormID() == SafeFormID(object);
					}
					return false;
				}
				return false;
			}();
			if (spdlog::should_log(spdlog::level::trace)) {
				LogEquipModeTraceTrace([&] { logger::trace(
					"EquipModeTrace: strictPick row obj={:08X} '{}' rowCount={} rowIsWorn={} entryCountDelta={}",
					SafeFormID(object),
					SafeName(object),
					rowCount,
					rowIsWorn,
					entry->countDelta); });
			}

			auto* strictEntry = GetStrictEntryForSelection(a_target, entry, object);
			if (!strictEntry) {
				// Template-only items may have no actor-owned InventoryChanges entry because the
				// engine records only mutations there. If using the UI clone as a base fallback,
				// keep its extraLists untouched: they may be stale after prior base equip cycles.
				strictEntry = entry;
				LogEquipModeTraceTrace([&] { logger::trace("EquipModeTrace: strictPick no actor entry, using UI clone entry"); });
			}

			// Log via actor-owned lookup only; UI clone extraLists can contain stale xList pointers.
			if (spdlog::should_log(spdlog::level::trace)) {
				LogActorObjectExtraLists(a_target, object, "strictPick-actorEntry");
			}

			// Run capability guards before both strict and base-fallback paths; unworn items with
			// no xList can return through fallback before per-type mode checks run.
			if (object->IsWeapon() || object->GetFormType() == RE::FormType::Ammo) {
				if (!ActorScope::HandEquipAllowed(a_target)) {
					return fail("handEquipBlockedForCreatureActor");
				}
			} else if (object->IsArmor()) {
				if (auto* armo = object->As<RE::TESObjectARMO>();
					!ActorScope::ArmorEquipAllowed(a_target, armo)) {
					Notifications::ToastKeyFmt("feedback.equip_mode.armor_blocked_hand_only", {}, SafeName(a_target));
					UISounds::PlaySoundByFormID(UISounds::SoundFormID::kActivateFail);
					return fail("armorEquipBlockedForActor");
				}
			}

			if (!strictEntry->extraLists || strictEntry->extraLists->empty()) {
				auto fb = TryBuildBaseFallbackPick(a_target, entry, object, a_hand, a_equipOnly, rowCount, rowIsWorn);
				if (fb.has_value()) {
					if (spdlog::should_log(spdlog::level::trace)) {
					const auto* why = entry->extraLists ? "extraListsEmpty" : "extraListsNull";
					LogEquipModeTraceTrace([&] { logger::trace(
						"EquipModeTrace: strictPick fallback(base) reason={} rowCount={} entryCountDelta={} object={:08X} '{}' handIntent={} equipOnly={} unequip={} swapMove={} slot={:08X} fromSlot={:08X}",
						why,
						rowCount,
						entry->countDelta,
						SafeFormID(fb->object),
						SafeName(fb->object),
						Core::HandToStr(a_hand),
						a_equipOnly,
						fb->unequip,
						fb->swapMove,
						SafeFormID(fb->slot),
						SafeFormID(fb->fromSlot)); });
					}
					return fb;
				}
			}

			// Use UI clone xList pointer values only for identity matching; never dereference them.
			// A stale pointer only fails to match actor-owned xLists, leaving preferredXList null.
			RE::ExtraDataList* preferredXList = nullptr;
			bool isBaseRow = false;
			{
				RE::ExtraDataList* uiXList = nullptr;
				std::uint32_t uiNonNull = 0;
				if (entry->extraLists) {
					for (auto* x : *entry->extraLists) {
						if (x) { ++uiNonNull; if (uiNonNull == 1) { uiXList = x; } }
					}
				}
				if (uiNonNull == 1 && uiXList && strictEntry->extraLists) {
					for (auto* x : *strictEntry->extraLists) {
						if (x == uiXList) { preferredXList = x; break; }
					}
					// Stale pointer from a prior base-fallback cycle; leave preferredXList null.
				} else if (uiNonNull > 1 && entry->extraLists && strictEntry->extraLists) {
					// Actor entries contain all xLists for this form, including other UI rows.
					// Restrict matches to the selected row, then prefer desired-hand worn, not-worn, then any.
					RE::ExtraDataList* wornMatch = nullptr;
					RE::ExtraDataList* notWornMatch = nullptr;
					RE::ExtraDataList* anyMatch = nullptr;
					for (auto* actorX : *strictEntry->extraLists) {
						if (!actorX) {
							continue;
						}
						bool inRow = false;
						for (auto* uiX : *entry->extraLists) {
							if (uiX == actorX) {
								inRow = true;
								break;
							}
						}
						if (!inRow) {
							continue;
						}
						if (!anyMatch) {
							anyMatch = actorX;
						}
						if (!wornMatch) {
							if (object->IsArmor()) {
								if (Core::HasWornExtra(*actorX)) {
									wornMatch = actorX;
								}
							} else {
								if (a_hand == Hand::kRight && Core::HasWornRightExtra(*actorX)) {
									wornMatch = actorX;
								}
								if (a_hand == Hand::kLeft && Core::HasWornLeftExtra(*actorX)) {
									wornMatch = actorX;
								}
							}
						}
						if (!notWornMatch && !Core::HasWornExtra(*actorX)) {
							notWornMatch = actorX;
						}
					}
					preferredXList = wornMatch ? wornMatch : notWornMatch ? notWornMatch : anyMatch;
				} else if (uiNonNull == 0) {
					// No xList in this UI row: pure base-copy row.
					isBaseRow = true;
				}
			}
			if (isBaseRow) {
				auto fb = TryBuildBaseFallbackPick(a_target, entry, object, a_hand, a_equipOnly, rowCount, rowIsWorn);
				if (fb.has_value()) {
					return fb;
				}
				return fail("baseFallbackFailed");
			}

			// Strict mode operates only on instances present in the selected row.
			std::optional<Core::StrictPick> out;
			if (object->IsWeapon()) {
				out = Modes::WeaponMode::TryBuildStrictPick(strictEntry, object, a_hand, a_equipOnly, a_target, preferredXList);
			} else if (object->IsArmor()) {
				out = Modes::ArmorMode::TryBuildStrictPick(strictEntry, object, a_hand, a_equipOnly, preferredXList);
			} else if (object->GetFormType() == RE::FormType::Ammo) {
				out = Modes::AmmoMode::TryBuildStrictPick(strictEntry, object, a_hand, a_equipOnly, preferredXList);
			} else if (object->GetFormType() == RE::FormType::Light) {
				out = Modes::TorchMode::TryBuildStrictPick(strictEntry, object, a_hand, a_equipOnly, preferredXList);
			} else if (object->GetFormType() == RE::FormType::Scroll) {
				out = Modes::ScrollMode::TryBuildStrictPick(strictEntry, object, a_hand, a_equipOnly, a_target, preferredXList);
			} else {
				return fail("unsupportedType");
			}

			if (!out || !out->object || (!out->xList && !out->baseFallback)) {
				return fail("noStrictInstance");
			}

			// Upgrade baseFallback to a synthetic xList only when the synthetic instance is not worn.
			// A worn synthetic xList can retarget the already-equipped copy and move it between hands.
			if (out->baseFallback && !out->xList && a_target) {
				RE::ExtraDataList* synth = nullptr;
				if (auto* changes = a_target->GetInventoryChanges(); changes && changes->entryList) {
					for (auto* realEntry : *changes->entryList) {
						if (realEntry && realEntry->GetObject() == object) {
							synth = SyntheticXList::EnsureXList(a_target, realEntry);
							break;
						}
					}
				}
				if (synth && Core::HasWornExtra(*synth)) {
					LogEquipModeTraceDebug([&] { logger::debug(
						"EquipModeTrace: strictPick synthetic xList={:p} is worn; skipping upgrade, keeping baseFallback",
						static_cast<void*>(synth)); });
					synth = nullptr;
				}
				if (synth) {
					out->xList = synth;
					out->baseFallback = false;
					LogEquipModeTraceDebug([&] { logger::debug(
						"EquipModeTrace: strictPick upgraded baseFallback to strict via synthetic xList={:p}",
						static_cast<void*>(synth)); });
				} else {
					LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: strictPick synthetic injection failed, keeping baseFallback"); });
				}
			}

			LogEquipModeTraceTrace([&] { logger::trace(
				"EquipModeTrace: strictPick built object={:08X} '{}' handIntent={} equipOnly={} unequip={} swapMove={} slot={:08X} fromSlot={:08X} xList={:p}",
				SafeFormID(out->object),
				SafeName(out->object),
				Core::HandToStr(a_hand),
				a_equipOnly,
				out->unequip,
				out->swapMove,
				SafeFormID(out->slot),
				SafeFormID(out->fromSlot),
				static_cast<void*>(out->xList)); });
			return out;
		}

		[[nodiscard]] bool ActorOwnsExactInstance(RE::Actor* a_actor, RE::TESBoundObject* a_object, RE::ExtraDataList* a_xList)
		{
			if (!a_actor || !a_object || !a_xList) {
				return false;
			}
			auto* changes = a_actor->GetInventoryChanges();
			if (!changes || !changes->entryList) {
				return false;
			}
			for (auto* entry : *changes->entryList) {
				if (!entry || entry->GetObject() != a_object) {
					continue;
				}
				if (!entry->extraLists) {
					continue;
				}
				for (auto* x : *entry->extraLists) {
					if (x == a_xList) {
						return true;
					}
				}
			}
			return false;
		}

		enum class ApplyLog
		{
			kNone,
			kStrictFromMenu,
		};

		void ApplyPickAction(RE::Actor* a_target, const Core::StrictPick& a_pick, bool a_hasSelection, ApplyLog a_log)
		{
			if (!a_target || !a_pick.object) {
				return;
			}

			auto logStrict = [&](const char* a_msg) {
				if (a_log == ApplyLog::kStrictFromMenu) {
					LogEquipModeTraceDebug([&] { logger::debug("{}", a_msg); });
				}
			};

			if (a_pick.swapMove) {
				if (a_pick.baseFallback) {
					logStrict("EquipModeTrace: StrictFromMenu action=SwapMoveBase (unequip+equip base)");
					UnequipBase(a_target, a_pick.object, a_pick.fromSlot ? a_pick.fromSlot : a_pick.slot);
					EquipBase(a_target, a_pick.object, a_pick.slot, a_hasSelection);
				} else {
					logStrict("EquipModeTrace: StrictFromMenu action=SwapMoveStrict (unequip+equip same xList)");
					UnequipStrict(a_target, a_pick.object, a_pick.xList, a_pick.fromSlot ? a_pick.fromSlot : a_pick.slot);
					// Unequip can free xLists that only held worn flags; re-check ownership before reusing it.
					if (ActorOwnsExactInstance(a_target, a_pick.object, a_pick.xList)) {
						EquipStrict(a_target, a_pick.object, a_pick.xList, a_pick.slot, a_hasSelection);
					} else {
						logStrict("EquipModeTrace: StrictFromMenu SwapMoveStrict xList invalidated after unequip, falling back to base equip");
						EquipBase(a_target, a_pick.object, a_pick.slot, a_hasSelection);
					}
				}
				return;
			}

			if (a_pick.unequip) {
				if (a_pick.baseFallback) {
					logStrict("EquipModeTrace: StrictFromMenu action=UnequipBase");
					UnequipBase(a_target, a_pick.object, a_pick.slot);
				} else {
					logStrict("EquipModeTrace: StrictFromMenu action=UnequipStrict");
					UnequipStrict(a_target, a_pick.object, a_pick.xList, a_pick.slot);
				}
				return;
			}

			if (a_pick.baseFallback) {
				logStrict("EquipModeTrace: StrictFromMenu action=EquipBase");
				EquipBase(a_target, a_pick.object, a_pick.slot, a_hasSelection);
			} else {
				logStrict("EquipModeTrace: StrictFromMenu action=EquipStrict");
				EquipStrict(a_target, a_pick.object, a_pick.xList, a_pick.slot, a_hasSelection);
			}
		}

	}

	bool PerformEquipModeActionStrictFromMenu(RE::ContainerMenu* a_menu, RE::Actor* a_target, Hand a_hand, bool a_equipOnly)
	{
		if (!a_target || a_target->IsPlayerRef() || a_target->IsDead()) {
			return false;
		}
		LogEquipModeTraceDebug([&] { logger::debug(
			"EquipModeTrace: StrictFromMenu begin actor={:08X} '{}' hand={} equipOnly={}",
			SafeFormID(a_target),
			SafeName(a_target),
			HandToStr(a_hand),
			a_equipOnly); });
		LogActorHands(a_target, "StrictFromMenu-pre");

		const auto pick = TryBuildStrictPickFromSelection(a_menu, a_target, a_hand, a_equipOnly);
		if (!pick.has_value()) {
			LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: StrictFromMenu result=fail (no strict pick)"); });
			ContainerMenuUtil::QueueRefreshForActor(SafeFormID(a_target));
			return false;
		}
		LogEquipModeTraceDebug([&] { logger::debug(
			"EquipModeTrace: StrictFromMenu pick object={:08X} '{}' slot={:08X} '{}' unequip={} swapMove={} xList={:p}",
			SafeFormID(pick->object),
			SafeName(pick->object),
			SafeFormID(pick->slot),
			SafeName(pick->slot),
			pick->unequip,
			pick->swapMove,
			static_cast<void*>(pick->xList)); });
		if (pick->baseFallback) {
			LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: StrictFromMenu pickMode=baseFallback (no xList)"); });
		}

		if (!pick->baseFallback) {
			// Fail closed if the selected xList is not actor-owned.
			if (!ActorOwnsExactInstance(a_target, pick->object, pick->xList)) {
				LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: StrictFromMenu result=fail (actor does not own exact xList instance)"); });
				ContainerMenuUtil::QueueRefreshForActor(SafeFormID(a_target));
				return false;
			}
		}

		ApplyPickAction(a_target, *pick, true, ApplyLog::kStrictFromMenu);

		CombatEquipIconInjector::NotifyEquipChanged();
		OutfitSyncIconInjector::NotifyEquipChanged();
		HandItemIconInjector::NotifyEquipChanged();

		ContainerMenuUtil::Refresh3DAndMenu(a_target);
		LogActorHands(a_target, "StrictFromMenu-post");
		LogActorObjectExtraLists(a_target, pick->object, "StrictFromMenu-post");
		LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: StrictFromMenu result=ok"); });
		return true;
	}

	bool PerformEquipOnlyStrictByXList(RE::Actor* a_target, RE::TESBoundObject* a_object, RE::ExtraDataList* a_xList, Hand a_hand)
	{
		if (!a_target || !a_object || !a_xList) {
			return false;
		}
		if (a_target->IsPlayerRef() || a_target->IsDead()) {
			return false;
		}
		if (a_object->IsArmor()) {
			if (auto* armo = a_object->As<RE::TESObjectARMO>();
				!ActorScope::ArmorEquipAllowed(a_target, armo)) {
				LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: EquipOnlyStrictByXList result=fail (armor blocked for actor policy or race)"); });
				Notifications::ToastKeyFmt("feedback.equip_mode.armor_blocked_hand_only", {}, SafeName(a_target));
				UISounds::PlaySoundByFormID(UISounds::SoundFormID::kActivateFail);
				return false;
			}
		}
		// kNone actors cannot hold weapons or ammo.
		if ((a_object->IsWeapon() || a_object->GetFormType() == RE::FormType::Ammo) &&
			!ActorScope::HandEquipAllowed(a_target)) {
			LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: EquipOnlyStrictByXList result=fail (hand equip blocked for actor capability)"); });
			return false;
		}
		LogEquipModeTraceDebug([&] { logger::debug(
			"EquipModeTrace: EquipOnlyStrictByXList begin actor={:08X} '{}' object={:08X} '{}' hand={} xList={:p}",
			SafeFormID(a_target),
			SafeName(a_target),
			SafeFormID(a_object),
			SafeName(a_object),
			HandToStr(a_hand),
			static_cast<void*>(a_xList)); });
		LogActorHands(a_target, "EquipOnlyStrictByXList-pre");
		if (!ActorOwnsExactInstance(a_target, a_object, a_xList)) {
			LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: EquipOnlyStrictByXList result=fail (actor does not own exact xList instance)"); });
			ContainerMenuUtil::QueueRefreshForActor(SafeFormID(a_target));
			return false;
		}
		const auto* slot = Core::GetSlotForObject(a_object, a_hand);
		Core::StrictPick pick{};
		pick.object = a_object;
		pick.xList = a_xList;
		pick.slot = slot;
		pick.unequip = false;
		pick.swapMove = false;
		pick.baseFallback = false;
		ApplyPickAction(a_target, pick, true, ApplyLog::kNone);
		CombatEquipIconInjector::NotifyEquipChanged();
		OutfitSyncIconInjector::NotifyEquipChanged();
		HandItemIconInjector::NotifyEquipChanged();
		ContainerMenuUtil::Refresh3DAndMenu(a_target);
		LogActorHands(a_target, "EquipOnlyStrictByXList-post");
		LogActorObjectExtraLists(a_target, a_object, "EquipOnlyStrictByXList-post");
		LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: EquipOnlyStrictByXList result=ok"); });
		return true;
	}

	bool PerformEquipOnlyBase(RE::Actor* a_target, RE::TESBoundObject* a_object, Hand a_hand)
	{
		if (!a_target || !a_object) {
			return false;
		}
		if (a_target->IsPlayerRef() || a_target->IsDead()) {
			return false;
		}
		if (a_object->IsArmor()) {
			if (auto* armo = a_object->As<RE::TESObjectARMO>();
				!ActorScope::ArmorEquipAllowed(a_target, armo)) {
				LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: EquipOnlyBase result=fail (armor blocked for actor policy or race)"); });
				return false;
			}
		}
		if ((a_object->IsWeapon() || a_object->GetFormType() == RE::FormType::Ammo) &&
			!ActorScope::HandEquipAllowed(a_target)) {
			LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: EquipOnlyBase result=fail (hand equip blocked for actor capability)"); });
			return false;
		}

		LogEquipModeTraceDebug([&] { logger::debug(
			"EquipModeTrace: EquipOnlyBase begin actor={:08X} '{}' object={:08X} '{}' hand={} (no xList)",
			SafeFormID(a_target),
			SafeName(a_target),
			SafeFormID(a_object),
			SafeName(a_object),
			HandToStr(a_hand)); });
		LogActorHands(a_target, "EquipOnlyBase-pre");

		const auto* slot = Core::GetSlotForObject(a_object, a_hand);
		Core::StrictPick pick{};
		pick.object = a_object;
		pick.xList = nullptr;
		pick.slot = slot;
		pick.unequip = false;
		pick.swapMove = false;
		pick.baseFallback = true;
		ApplyPickAction(a_target, pick, false, ApplyLog::kNone);
		ContainerMenuUtil::Refresh3DAndMenu(a_target);

		LogActorHands(a_target, "EquipOnlyBase-post");
		LogActorObjectExtraLists(a_target, a_object, "EquipOnlyBase-post");
		LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: EquipOnlyBase result=ok"); });
		return true;
	}

	bool PerformCorpseEquipToggleFromMenu(RE::ContainerMenu* a_menu, RE::Actor* a_target, Hand a_hand)
	{
		if (!a_menu || !a_target || a_target->IsPlayerRef() || !a_target->IsDead()) {
			return false;
		}

		LogEquipModeTraceDebug([&] { logger::debug(
			"EquipModeTrace: CorpseEquipToggle begin actor={:08X} '{}'",
			SafeFormID(a_target),
			SafeName(a_target)); });

		const auto pick = TryBuildStrictPickFromSelection(a_menu, a_target, a_hand, false);
		if (!pick.has_value()) {
			LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: CorpseEquipToggle result=fail (no strict pick)"); });
			ContainerMenuUtil::QueueRefreshForActor(SafeFormID(a_target));
			return false;
		}

		// Corpse equip mode allows only armor, weapons, and ammo.
		if (!pick->object || (!pick->object->IsArmor() && !pick->object->IsWeapon() && pick->object->GetFormType() != RE::FormType::Ammo)) {
			LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: CorpseEquipToggle result=fail (not armor, weapon, or ammo)"); });
			return false;
		}

		// kNone corpses cannot hold weapons or ammo; armor is gated by TryBuildStrictPick.
		if ((pick->object->IsWeapon() || pick->object->GetFormType() == RE::FormType::Ammo) &&
			!ActorScope::HandEquipAllowed(a_target)) {
			LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: CorpseEquipToggle result=fail (hand equip blocked for actor capability)"); });
			return false;
		}

		if (!pick->baseFallback) {
			if (!ActorOwnsExactInstance(a_target, pick->object, pick->xList)) {
				LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: CorpseEquipToggle result=fail (actor does not own exact xList instance)"); });
				ContainerMenuUtil::QueueRefreshForActor(SafeFormID(a_target));
				return false;
			}
		}

		ApplyPickAction(a_target, *pick, true, ApplyLog::kStrictFromMenu);

		if (a_target->Is3DLoaded()) {
			a_target->Update3DModel();
		}
		ContainerMenuUtil::QueueRefreshForOpenContainerMenu();

		LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: CorpseEquipToggle result=ok"); });
		return true;
	}

	bool PerformCorpseEquipOnly(RE::Actor* a_target, RE::TESBoundObject* a_object, RE::ExtraDataList* a_xList, Hand a_hand)
	{
		if (!a_target || !a_object || a_target->IsPlayerRef() || !a_target->IsDead()) {
			return false;
		}
		if (!a_object->IsArmor() && !a_object->IsWeapon() && a_object->GetFormType() != RE::FormType::Ammo) {
			return false;
		}
		if (a_object->IsArmor()) {
			if (auto* armo = a_object->As<RE::TESObjectARMO>();
				!ActorScope::ArmorEquipAllowed(a_target, armo)) {
				LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: CorpseEquipOnly result=fail (armor blocked for actor policy or race)"); });
				return false;
			}
		}
		// kNone corpses cannot hold weapons or ammo.
		if ((a_object->IsWeapon() || a_object->GetFormType() == RE::FormType::Ammo) &&
			!ActorScope::HandEquipAllowed(a_target)) {
			LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: CorpseEquipOnly result=fail (hand equip blocked for actor capability)"); });
			return false;
		}

		LogEquipModeTraceDebug([&] { logger::debug(
			"EquipModeTrace: CorpseEquipOnly begin actor={:08X} '{}' object={:08X} '{}' hand={} xList={:p}",
			SafeFormID(a_target), SafeName(a_target),
			SafeFormID(a_object), SafeName(a_object),
			Core::HandToStr(a_hand),
			static_cast<void*>(a_xList)); });

		const auto* slot = Core::GetSlotForObject(a_object, a_hand);
		Core::StrictPick pick{};
		pick.object = a_object;
		pick.xList = a_xList;
		pick.slot = slot;
		pick.unequip = false;
		pick.swapMove = false;
		pick.baseFallback = (a_xList == nullptr);

		ApplyPickAction(a_target, pick, false, ApplyLog::kNone);

		if (a_target->Is3DLoaded()) {
			a_target->Update3DModel();
		}
		ContainerMenuUtil::QueueRefreshForOpenContainerMenu();
		LogEquipModeTraceDebug([&] { logger::debug("EquipModeTrace: CorpseEquipOnly result=ok"); });
		return true;
	}
}
