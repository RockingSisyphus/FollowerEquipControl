#include "SignatureResolve.h"

#include <cmath>
#include <cstdint>
#include <memory>

#include "RE/E/ExtraPoison.h"
#include "RE/E/ExtraWorn.h"
#include "RE/E/ExtraWornLeft.h"

namespace FEC::SignatureResolve
{
	namespace
	{
		[[nodiscard]] bool IsPoisoned(const RE::ExtraDataList* a_list) noexcept
		{
			return a_list && a_list->HasType<RE::ExtraPoison>();
		}

		void ConsiderPreferPoisoned(RE::ExtraDataList*& a_best, RE::ExtraDataList* a_candidate) noexcept
		{
			if (!a_candidate) {
				return;
			}
			if (!a_best) {
				a_best = a_candidate;
				return;
			}
			if (!IsPoisoned(a_best) && IsPoisoned(a_candidate)) {
				a_best = a_candidate;
			}
		}

		[[nodiscard]] InstanceSignature::EquipState GetEquipStateFromExtra(const RE::ExtraDataList& a_list)
		{
			if (a_list.HasType<RE::ExtraWornLeft>()) {
				return InstanceSignature::EquipState::kWornLeft;
			}
			if (a_list.HasType<RE::ExtraWorn>()) {
				return InstanceSignature::EquipState::kWornRight;
			}
			return InstanceSignature::EquipState::kNotWorn;
		}

		[[nodiscard]] RE::ExtraDataList* ResolveIdentity(
			RE::InventoryEntryData* a_entry,
			RE::TESBoundObject* a_object,
			const InstanceSignature& a_signature,
			std::optional<InstanceSignature::EquipState> a_desiredState,
			const std::unordered_set<RE::ExtraDataList*>* a_excludeSet = nullptr)
		{
			if (!a_entry || !a_object) {
				return nullptr;
			}
			if (!a_entry->extraLists || a_entry->extraLists->empty()) {
				return nullptr;
			}

			const auto targetIdentity = NormalizeStableIdentity(a_signature);

			RE::ExtraDataList* firstMatch = nullptr;
			RE::ExtraDataList* firstDesiredStateMatch = nullptr;
			RE::ExtraDataList* firstWornMatch = nullptr;
			RE::ExtraDataList* firstNotWornMatch = nullptr;

			for (auto* xList : *a_entry->extraLists) {
				if (!xList) {
					continue;
				}
				if (a_excludeSet && a_excludeSet->contains(xList)) {
					continue;
				}

				const auto built = BuildInstanceSignature(*xList, a_object);
				const auto candIdentity = NormalizeStableIdentity(built);
				if (!StableIdentityEquals(candIdentity, targetIdentity)) {
					continue;
				}

				ConsiderPreferPoisoned(firstMatch, xList);

				const auto st = built.equipState;
				if (a_desiredState.has_value() && st == *a_desiredState) {
					ConsiderPreferPoisoned(firstDesiredStateMatch, xList);
				}
				const bool isWorn = (st == InstanceSignature::EquipState::kWornLeft) || (st == InstanceSignature::EquipState::kWornRight);
				if (isWorn) {
					ConsiderPreferPoisoned(firstWornMatch, xList);
				}
				if (st == InstanceSignature::EquipState::kNotWorn) {
					ConsiderPreferPoisoned(firstNotWornMatch, xList);
				}
			}

			if (!firstMatch) {
				return nullptr;
			}

			if (a_desiredState.has_value()) {
				// Prefer already-correct worn state; otherwise prefer not-worn to avoid swapping across hands.
				return firstDesiredStateMatch ? firstDesiredStateMatch : (firstNotWornMatch ? firstNotWornMatch : (firstWornMatch ? firstWornMatch : firstMatch));
			} else {
				// Prefer worn first to avoid unnecessary swaps when both worn and not-worn instances exist.
				return firstWornMatch ? firstWornMatch : (firstNotWornMatch ? firstNotWornMatch : firstMatch);
			}
		}

		[[nodiscard]] RE::ExtraDataList* ResolveAnyInstancePreferState(
			RE::InventoryEntryData* a_entry,
			std::optional<InstanceSignature::EquipState> a_desiredState)
		{
			if (!a_entry || !a_entry->extraLists || a_entry->extraLists->empty()) {
				return nullptr;
			}

			RE::ExtraDataList* firstDesired = nullptr;
			RE::ExtraDataList* firstWorn = nullptr;
			RE::ExtraDataList* firstNotWorn = nullptr;

			for (auto* xList : *a_entry->extraLists) {
				if (!xList) {
					continue;
				}

				const auto st = GetEquipStateFromExtra(*xList);
				if (a_desiredState.has_value() && st == *a_desiredState) {
					ConsiderPreferPoisoned(firstDesired, xList);
				}

				const bool isWorn = (st == InstanceSignature::EquipState::kWornLeft) || (st == InstanceSignature::EquipState::kWornRight);
				if (isWorn) {
					ConsiderPreferPoisoned(firstWorn, xList);
				}
				if (st == InstanceSignature::EquipState::kNotWorn) {
					ConsiderPreferPoisoned(firstNotWorn, xList);
				}
			}

			if (firstDesired) {
				return firstDesired;
			}

			// If the requested worn state is missing, prefer not-worn so we do not steal the other hand's instance.
			if (a_desiredState.has_value()) {
				return firstNotWorn ? firstNotWorn : (firstWorn ? firstWorn : a_entry->extraLists->front());
			}

			return firstWorn ? firstWorn : (firstNotWorn ? firstNotWorn : a_entry->extraLists->front());
		}
	}

	ResolveResult Resolve(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		const InstanceSignature& a_signature,
		std::optional<InstanceSignature::EquipState> a_desiredState,
		Policy a_policy)
	{
		ResolveResult out{};
		if (!a_actor || !a_object) {
			return out;
		}

		auto inv = a_actor->GetInventory([&](RE::TESBoundObject& a_obj) {
			return std::addressof(a_obj) == a_object;
		});
		const auto it = inv.find(a_object);
		if (it == inv.end()) {
			return out;
		}

		auto* entry = it->second.second.get();
		if (!entry) {
			return out;
		}
		const auto totalCount = it->second.first;
		if (totalCount <= 0) {
			return out;
		}
		const bool hasAnyExtraLists = (entry->extraLists && !entry->extraLists->empty());

		if (auto* identity = ResolveIdentity(entry, a_object, a_signature, a_desiredState); identity) {
			out.kind = ResolveKind::kMatchedXList;
			out.xList = identity;
			return out;
		}

		if (a_policy == Policy::kIdentityOnly) {
			// Identity-only base matches are safe only when no per-instance extra lists exist.
			if (!a_signature.HasStableIdentity() && !hasAnyExtraLists) {
				out.kind = ResolveKind::kMatchedBaseOnly;
				out.xList = nullptr;
			}
			return out;
		}

		if (auto* any = ResolveAnyInstancePreferState(entry, a_desiredState); any) {
			out.kind = ResolveKind::kMatchedXList;
			out.xList = any;
			return out;
		}

		// Best-effort base-only match when no per-instance extra lists exist.
		if (!hasAnyExtraLists) {
			out.kind = ResolveKind::kMatchedBaseOnly;
			out.xList = nullptr;
			return out;
		}

		return out;
	}

	ResolveResult Resolve(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		const InstanceSignature& a_signature,
		std::optional<InstanceSignature::EquipState> a_desiredState,
		Policy a_policy,
		const std::unordered_set<RE::ExtraDataList*>& a_excludeSet)
	{
		ResolveResult out{};
		if (!a_actor || !a_object) {
			return out;
		}

		auto inv = a_actor->GetInventory([&](RE::TESBoundObject& a_obj) {
			return std::addressof(a_obj) == a_object;
		});
		const auto it = inv.find(a_object);
		if (it == inv.end()) {
			return out;
		}

		auto* entry = it->second.second.get();
		if (!entry) {
			return out;
		}
		const auto totalCount = it->second.first;
		if (totalCount <= 0) {
			return out;
		}
		const bool hasAnyExtraLists = (entry->extraLists && !entry->extraLists->empty());

		if (auto* identity = ResolveIdentity(entry, a_object, a_signature, a_desiredState, &a_excludeSet); identity) {
			out.kind = ResolveKind::kMatchedXList;
			out.xList = identity;
			return out;
		}

		if (a_policy == Policy::kIdentityOnly) {
			if (!a_signature.HasStableIdentity() && !hasAnyExtraLists) {
				out.kind = ResolveKind::kMatchedBaseOnly;
				out.xList = nullptr;
			}
			return out;
		}

		if (auto* any = ResolveAnyInstancePreferState(entry, a_desiredState); any) {
			out.kind = ResolveKind::kMatchedXList;
			out.xList = any;
			return out;
		}

		if (!hasAnyExtraLists) {
			out.kind = ResolveKind::kMatchedBaseOnly;
			out.xList = nullptr;
			return out;
		}

		return out;
	}
}
