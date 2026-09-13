#include "WeaponRechargeTransaction.h"

#include "WeaponRechargeChargeUtil.h"
#include "WeaponRechargeSelection.h"
#include "WeaponRechargeTypes.h"

#include <algorithm>

#include <RE/A/Actor.h>
#include <RE/I/InventoryChanges.h>

#include <RE/E/ExtraWorn.h>
#include <RE/E/ExtraWornLeft.h>
#include <RE/E/ExtraSoul.h>

namespace FEC::WeaponRecharge
{
	namespace
	{
		// Vanilla keyword: ReusableSoulGem [KYWD:000ED2F1]
		constexpr RE::FormID kReusableSoulGemKeyword = 0x000ED2F1;

		[[nodiscard]] bool IsReusableByKeyword(const RE::TESSoulGem* a_gem)
		{
			if (!a_gem) {
				return false;
			}
			return a_gem->HasKeywordID(kReusableSoulGemKeyword);
		}

		[[nodiscard]] bool TryClearSoulFromFirstMatchingInstance(RE::Actor* a_owner, RE::TESSoulGem* a_gem, RE::SOUL_LEVEL a_soul)
		{
			if (!a_owner || !a_gem || a_soul <= RE::SOUL_LEVEL::kNone) {
				return false;
			}

			auto* inv = a_owner->GetInventoryChanges(false);
			if (!inv || !inv->entryList) {
				return false;
			}

			for (auto* entry : *inv->entryList) {
				if (!entry) {
					continue;
				}
				auto* obj = entry->GetObject();
				if (!obj || obj != a_gem) {
					continue;
				}
				if (!entry->extraLists || entry->extraLists->empty()) {
					continue;
				}

				for (auto* list : *entry->extraLists) {
					if (!list) {
						continue;
					}
					auto* xSoul = list->GetByType<RE::ExtraSoul>();
					if (!xSoul) {
						continue;
					}
					if (xSoul->GetContainedSoul() != a_soul) {
						continue;
					}

					xSoul->soul = RE::SOUL_LEVEL::kNone;
					inv->changed = true;
					return true;
				}
			}

			return false;
		}

		[[nodiscard]] bool IsWornList(const RE::ExtraDataList* a_list)
		{
			if (!a_list) {
				return false;
			}
			return a_list->HasType<RE::ExtraWorn>() || a_list->HasType<RE::ExtraWornLeft>();
		}

		[[nodiscard]] bool TryWriteWornChargeViaInventoryChanges(
			RE::Actor* a_ownerActor,
			RE::TESBoundObject* a_object,
			double a_newAbs,
			RE::ExtraDataList** a_outWornRight,
			RE::ExtraDataList** a_outWornLeft)
		{
			if (a_outWornRight) {
				*a_outWornRight = nullptr;
			}
			if (a_outWornLeft) {
				*a_outWornLeft = nullptr;
			}

			if (!a_ownerActor || !a_object) {
				return false;
			}

			auto* inv = a_ownerActor->GetInventoryChanges(false);
			if (!inv || !inv->entryList) {
				return false;
			}

			RE::InventoryEntryData* foundEntry = nullptr;
			for (auto* entry : *inv->entryList) {
				if (!entry) {
					continue;
				}
				auto* obj = entry->GetObject();
				auto* boundObj = obj ? obj->As<RE::TESBoundObject>() : nullptr;
				if (boundObj == a_object) {
					foundEntry = entry;
					break;
				}
			}

			if (!foundEntry || !foundEntry->extraLists || foundEntry->extraLists->empty()) {
				return false;
			}

			bool wroteAny = false;
			for (auto* list : *foundEntry->extraLists) {
				if (!IsWornList(list)) {
					continue;
				}

				if (a_outWornRight && list->HasType<RE::ExtraWorn>()) {
					*a_outWornRight = list;
				}
				if (a_outWornLeft && list->HasType<RE::ExtraWornLeft>()) {
					*a_outWornLeft = list;
				}

				auto* xCharge = list->GetByType<RE::ExtraCharge>();
				if (!xCharge) {
					xCharge = new RE::ExtraCharge();
					list->Add(xCharge);
				}
				xCharge->charge = static_cast<float>(a_newAbs);
				wroteAny = true;
			}

			if (wroteAny) {
				inv->changed = true;
			}
			return wroteAny;
		}

		void RefreshEquippedChargeCache(
			RE::Actor* a_ownerActor,
			RE::TESBoundObject* a_object,
			RE::ExtraDataList* a_wornRight,
			RE::ExtraDataList* a_wornLeft,
			bool a_equippedRight,
			bool a_equippedLeft,
			double /*a_newAbs*/)
		{
			if (!a_ownerActor || !a_object) {
				return;
			}

			if (a_equippedRight && a_wornRight) {
				a_ownerActor->UpdateWeaponAbility(a_object, a_wornRight, false);
			}
			if (a_equippedLeft && a_wornLeft) {
				a_ownerActor->UpdateWeaponAbility(a_object, a_wornLeft, true);
			}
		}
	}

	ApplyResult TryApplyRecharge(
		const SelectedChargeInfo& a_selected,
		RE::Actor* a_player,
		RE::Actor* a_follower,
		RE::TESSoulGem* a_gem,
		SoulGemSource a_gemSource,
		RE::SOUL_LEVEL a_gemSoul)
	{
		ApplyResult result{};
		if (!a_player || !a_follower || !a_gem) {
			return result;
		}
		if (!a_selected.entry || !a_selected.object) {
			return result;
		}

		double currentAbs = 0.0;
		double maxAbs = 0.0;
		if (a_selected.xList) {
			RE::Actor* ownerActor = nullptr;
			if (IsWornList(a_selected.xList) && a_selected.owner) {
				auto ownerPtr = a_selected.owner.get();
				ownerActor = ownerPtr ? ownerPtr.get() : nullptr;
			}
			(void)WeaponRecharge::ChargeUtil::TryReadChargeAbsPreferEquippedCache(ownerActor, a_selected.object, a_selected.xList, currentAbs, maxAbs);
		}
		if (maxAbs <= 0.0) {
			currentAbs = a_selected.currentAbs;
			maxAbs = a_selected.maxAbs;
		}
		if (maxAbs <= 0.0) {
			return result;
		}
		currentAbs = std::clamp(currentAbs, 0.0, maxAbs);

		const auto soulLevel = (a_gemSoul > RE::SOUL_LEVEL::kNone) ? a_gemSoul : a_gem->GetContainedSoul();
		const double soulValue = GetSoulRechargeValue(soulLevel);
		if (soulValue <= 0.0) {
			return result;
		}

		const double newAbs = std::clamp(currentAbs + soulValue, 0.0, maxAbs);
		if (newAbs <= currentAbs + 0.0001) {
			// Vanilla behavior: don't consume gems if no change.
			return result;
		}

		bool wrote = false;
		const bool selectedIsWorn = IsWornList(a_selected.xList);

		RE::Actor* ownerActor = nullptr;
		if (a_selected.owner) {
			auto ownerPtr = a_selected.owner.get();
			ownerActor = ownerPtr ? ownerPtr.get() : nullptr;
		}

		bool isEquippedR = false;
		bool isEquippedL = false;
		if (ownerActor) {
			if (const auto* rightObj = ownerActor->GetEquippedObject(false)) {
				isEquippedR = (rightObj == a_selected.object);
			}
			if (const auto* leftObj = ownerActor->GetEquippedObject(true)) {
				isEquippedL = (leftObj == a_selected.object);
			}
		}

		// Prefer worn-list writes plus cache refresh for equipped weapons.
		if (ownerActor && (selectedIsWorn || isEquippedR || isEquippedL)) {
			RE::ExtraDataList* wornRight = nullptr;
			RE::ExtraDataList* wornLeft = nullptr;
			wrote = TryWriteWornChargeViaInventoryChanges(ownerActor, a_selected.object, newAbs, &wornRight, &wornLeft);
			if (wrote) {
				RefreshEquippedChargeCache(ownerActor, a_selected.object, wornRight, wornLeft, isEquippedR, isEquippedL, newAbs);
			}
		}
		if (!wrote) {
			wrote = TrySetSelectedItemChargeAbs(a_selected.entry, a_selected.xList, newAbs);
		}
		if (!wrote) {
			return result;
		}

		result.applied = true;

		// Consume gem only after successful apply.
		RE::Actor* gemOwner = (a_gemSource == SoulGemSource::kFollower) ? a_follower : a_player;
		if (IsReusableByKeyword(a_gem)) {
			// Reusable soul gems are emptied, not consumed.
			// If the matching ExtraSoul is not found, do not delete the item.
			result.consumedGem = TryClearSoulFromFirstMatchingInstance(gemOwner, a_gem, soulLevel);
		} else {
			gemOwner->RemoveItem(a_gem, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			result.consumedGem = true;
		}

		return result;
	}
}
