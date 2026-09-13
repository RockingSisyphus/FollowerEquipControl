#include "InstanceResolver.h"

#include "SelectionUtil.h"

namespace FEC::EquipMode::Selection
{
	namespace
	{
		[[nodiscard]] RE::ExtraDataList* ResolveByPointerIdentity(RE::Actor* a_actor, RE::TESBoundObject* a_object, RE::ExtraDataList* a_hint)
		{
			if (!a_actor || !a_object || !a_hint) {
				return nullptr;
			}
			auto inv = a_actor->GetInventory([&](RE::TESBoundObject& a_obj) {
				return std::addressof(a_obj) == a_object;
			});
			const auto it = inv.find(a_object);
			if (it == inv.end()) {
				return nullptr;
			}
			auto* entry = it->second.second.get();
			if (!entry || !entry->extraLists) {
				return nullptr;
			}
			for (auto* list : *entry->extraLists) {
				if (list == a_hint) {
					return list;
				}
			}
			return nullptr;
		}

		[[nodiscard]] RE::ExtraDataList* FindAnyMatchingDesired(RE::Actor* a_actor, RE::TESBoundObject* a_object, std::optional<EquipState> a_desired)
		{
			if (!a_actor || !a_object) {
				return nullptr;
			}
			auto inv = a_actor->GetInventory([&](RE::TESBoundObject& a_obj) {
				return std::addressof(a_obj) == a_object;
			});
			const auto it = inv.find(a_object);
			if (it == inv.end()) {
				return nullptr;
			}
			auto* entry = it->second.second.get();
			if (!entry || !entry->extraLists || entry->extraLists->empty()) {
				return nullptr;
			}

			RE::ExtraDataList* first = nullptr;
			RE::ExtraDataList* firstWorn = nullptr;
			RE::ExtraDataList* firstNotWorn = nullptr;
			for (auto* list : *entry->extraLists) {
				if (!list) {
					continue;
				}
				first = first ? first : list;
				const auto st = GetEquipStateFromExtra(*list);
				if (st != EquipState::kNotWorn) {
					firstWorn = firstWorn ? firstWorn : list;
				}
				if (st == EquipState::kNotWorn) {
					firstNotWorn = firstNotWorn ? firstNotWorn : list;
				}
			}

			if (!a_desired.has_value()) {
				return firstWorn ? firstWorn : first;
			}
			if (*a_desired == EquipState::kNotWorn) {
				return firstNotWorn ? firstNotWorn : first;
			}
			// Specific worn hand state is not required here.
			return firstWorn ? firstWorn : first;
		}
	}

	std::optional<RE::ExtraDataList*> ResolveSelectedExtraStrict(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		const std::optional<SelectedInstanceContext>& a_selected,
		std::optional<EquipState> a_desiredState,
		const char* /*a_site*/)
	{
		if (!a_actor || !a_object) {
			return std::nullopt;
		}
		if (!a_selected.has_value()) {
			return std::nullopt;
		}

		// Plain-stack selection can resolve to a null xList.
		if (!a_selected->xList && !a_selected->capturedFromNonUniqueRow && !a_desiredState.has_value()) {
			return static_cast<RE::ExtraDataList*>(nullptr);
		}

		RE::ExtraDataList* resolved = nullptr;
		if (a_selected->xList) {
			resolved = a_selected->xListBelongsToTargetActor ? a_selected->xList : ResolveByPointerIdentity(a_actor, a_object, a_selected->xList);
		}

		if (!resolved) {
			// Non-unique rows and stale xList hints need a safe fallback.
			resolved = FindAnyMatchingDesired(a_actor, a_object, a_desiredState);
		}

		if (a_desiredState.has_value()) {
			if (!resolved) {
				return std::nullopt;
			}
			if (GetEquipStateFromExtra(*resolved) != *a_desiredState && *a_desiredState != EquipState::kUnknown) {
			}
		}

		return resolved;
	}
}
