#include "InstanceSelection.h"

namespace FEC::EquipMode::Selection
{
	std::optional<SelectedInstanceContext> CaptureSelectedInstance(
		RE::ContainerMenu* a_menu,
		RE::TESBoundObject* a_object,
		CaptureContext a_context,
		RE::Actor* a_actor)
	{
		if (!a_menu || !a_object) {
			return std::nullopt;
		}

		auto* itemList = a_menu->GetRuntimeData().itemList;
		if (!itemList) {
			return std::nullopt;
		}
		auto* selected = itemList->GetSelectedItem();
		if (!selected) {
			return std::nullopt;
		}
		auto* uiEntry = selected->data.objDesc;
		if (!uiEntry || uiEntry->GetObject() != a_object) {
			return std::nullopt;
		}

		// Do not dereference xList pointers from uiEntry->extraLists. UI clone lists can
		// retain dangling xList addresses after baseFallback cycles. Address comparison is
		// safe only after validating the pointer against actorEntry->extraLists.
		// selected->data.GetCount() and GetEquipState() also dereference those xLists.
		RE::InventoryEntryData* actorEntry = nullptr;
		if (a_actor) {
			if (auto* changes = a_actor->GetInventoryChanges(); changes && changes->entryList) {
				for (auto* e : *changes->entryList) {
					if (e && e->GetObject() == a_object) {
						actorEntry = e;
						break;
					}
				}
			}
		}

		SelectedInstanceContext out{};
		out.xListBelongsToTargetActor = (a_context != CaptureContext::kPlayerToNpcTransfer);

		// Treat missing actor entry as a plain stack; resolver will do actor-owned fallback.
		if (!actorEntry || !actorEntry->extraLists || actorEntry->extraLists->empty()) {
			out.xList = nullptr;
			out.capturedFromNonUniqueRow = false;
			return out;
		}

		std::uint32_t nonNull = 0;
		for (auto* list : *actorEntry->extraLists) {
			if (!list) {
				continue;
			}
			++nonNull;
		}

		// Capture a specific instance only when the selected UI row has one non-null xList.
		// Multi-xList rows are ambiguous and fall back to worn-state heuristics.
		{
			RE::ExtraDataList* uiXList = nullptr;
			std::uint32_t uiNonNull = 0;
			if (uiEntry->extraLists) {
				for (auto* x : *uiEntry->extraLists) {
					if (x) { ++uiNonNull; if (uiNonNull == 1) { uiXList = x; } }
				}
			}
			if (uiNonNull == 1 && uiXList) {
				for (auto* list : *actorEntry->extraLists) {
					if (list == uiXList) {
						out.xList = list;
						out.capturedFromNonUniqueRow = false;
						return out;
					}
				}
				// Stale pointer from a prior baseFallback cycle.
			}
			// Base-copy and stacked rows fall through.
		}
		out.xList = nullptr;
		out.capturedFromNonUniqueRow = (nonNull > 0);
		return out;
	}
}
