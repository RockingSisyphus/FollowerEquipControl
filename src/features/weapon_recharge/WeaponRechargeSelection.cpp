#include "WeaponRechargeSelection.h"

#include "ContainerMenuUtil.h"
#include "InstanceResolver.h"
#include "InstanceSelection.h"

#include "WeaponRechargeChargeUtil.h"
#include "WeaponRechargeGfxUtil.h"

#include <RE/E/ExtraCharge.h>

#include <algorithm>

namespace FEC::WeaponRecharge
{
	bool TryGetSelectedItemChargeInfo(RE::ContainerMenu* a_menu, SelectedChargeInfo& a_out)
	{
		a_out = {};
		if (!a_menu) {
			return false;
		}

		auto* itemList = a_menu->GetRuntimeData().itemList;
		if (!itemList) {
			return false;
		}
		auto* selected = itemList->GetSelectedItem();
		if (!selected) {
			return false;
		}
		auto* entry = selected->data.objDesc;
		if (!entry) {
			return false;
		}
		auto* object = entry->GetObject();
		if (!object) {
			return false;
		}

		// Resolve against the current menu-side owner for safety.
		RE::NiPointer<RE::Actor> owner;
		{
			std::int32_t activeSegment = 0;
			(void)WeaponRecharge::GfxUtil::TryGetActiveSegment(a_menu->GetRuntimeData().root, activeSegment);

			// SkyUI activeSegment 0 is Take; segment 1 is Give.
			if (activeSegment == 0) {
				owner = ContainerMenuUtil::GetAffectedTarget(a_menu);
			} else {
				owner = RE::NiPointer<RE::Actor>(RE::PlayerCharacter::GetSingleton());
			}
		}
		if (!owner) {
			return false;
		}

		const auto captured = EquipMode::Selection::CaptureSelectedInstance(
			a_menu,
			object,
			EquipMode::Selection::CaptureContext::kDefault,
			owner.get());
		if (!captured.has_value()) {
			return false;
		}

		// Re-resolve against the owner inventory instead of trusting the UI-cloned xList.
		auto capturedCopy = *captured;
		capturedCopy.xListBelongsToTargetActor = false;
		const std::optional<EquipMode::Selection::SelectedInstanceContext> capturedForResolve{ capturedCopy };

		auto resolved = EquipMode::Selection::ResolveSelectedExtraStrict(
			owner.get(),
			object,
			capturedForResolve,
			std::nullopt,
			"WeaponRecharge");
		if (!resolved.has_value() || !resolved.value()) {
			return false;
		}

		double currentAbs = 0.0;
		double maxAbs = 0.0;
		if (!WeaponRecharge::ChargeUtil::TryReadChargeAbsPreferEquippedCache(owner.get(), object, resolved.value(), currentAbs, maxAbs)) {
			return false;
		}
		if (maxAbs <= 0.0) {
			return false;
		}

		a_out.entry = entry;
		a_out.xList = resolved.value();
		a_out.object = object;
		a_out.owner = owner->GetHandle();
		a_out.currentAbs = currentAbs;
		a_out.maxAbs = maxAbs;
		a_out.currentPercent = std::clamp((currentAbs / maxAbs) * 100.0, 0.0, 100.0);
		return true;
	}

	bool TrySetSelectedItemChargeAbs(RE::InventoryEntryData* a_entry, RE::ExtraDataList* a_xList, double a_newAbs)
	{
		if (!a_entry) {
			return false;
		}

		bool any = false;
		auto applyTo = [&](RE::ExtraDataList* list) {
			if (!list) {
				return;
			}
			auto* xCharge = list->GetByType<RE::ExtraCharge>();
			if (!xCharge) {
				xCharge = new RE::ExtraCharge();
				list->Add(xCharge);
			}
			xCharge->charge = static_cast<float>(a_newAbs);
			any = true;
		};

		if (a_xList) {
			applyTo(a_xList);
			return any;
		}

		if (!a_entry->extraLists || a_entry->extraLists->empty()) {
			return false;
		}

		for (auto* list : *a_entry->extraLists) {
			applyTo(list);
		}
		return any;
	}
}
