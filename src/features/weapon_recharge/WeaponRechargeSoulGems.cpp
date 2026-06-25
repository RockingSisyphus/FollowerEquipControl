#include "WeaponRechargeSoulGems.h"

#include <algorithm>
#include <map>

#include <RE/E/ExtraSoul.h>
#include <RE/I/InventoryChanges.h>

namespace FEC::WeaponRecharge
{
	namespace
	{
		void GatherOne(RE::Actor* a_actor, SoulGemSource a_source, std::vector<SoulGemOption>& a_out)
		{
			if (!a_actor) {
				return;
			}

			std::map<RE::FormID, std::int32_t> remainingByForm;
			std::map<RE::FormID, RE::TESSoulGem*> gemByForm;

			auto inv = a_actor->GetInventory([&](RE::TESBoundObject& a_obj) {
				return a_obj.Is(RE::FormType::SoulGem);
			});
			for (auto& [obj, pair] : inv) {
				auto* gem = obj ? obj->As<RE::TESSoulGem>() : nullptr;
				if (!gem) {
					continue;
				}
				const auto count = pair.first;
				if (count <= 0) {
					continue;
				}
				remainingByForm[gem->GetFormID()] = count;
				gemByForm[gem->GetFormID()] = gem;
			}

			// Group by form and soul to capture per-instance ExtraSoul while preserving base-form souls.
			std::map<std::pair<RE::FormID, RE::SOUL_LEVEL>, std::int32_t> counts;

			auto* invChanges = a_actor->GetInventoryChanges(false);
			if (invChanges && invChanges->entryList) {
				for (auto* entry : *invChanges->entryList) {
					if (!entry) {
						continue;
					}
					auto* obj = entry->GetObject();
					auto* gem = obj ? obj->As<RE::TESSoulGem>() : nullptr;
					if (!gem) {
						continue;
					}

					const auto formId = gem->GetFormID();
					auto itRemaining = remainingByForm.find(formId);
					if (itRemaining == remainingByForm.end()) {
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
						const auto soul = xSoul->GetContainedSoul();
						if (soul <= RE::SOUL_LEVEL::kNone) {
							continue;
						}

						counts[{ formId, soul }] += 1;
						if (itRemaining->second > 0) {
							itRemaining->second -= 1;
						}
					}
				}
			}

			// Remaining stack count uses the base-form soul state.
			for (const auto& [formId, count] : remainingByForm) {
				if (count <= 0) {
					continue;
				}
				auto itGem = gemByForm.find(formId);
				if (itGem == gemByForm.end() || !itGem->second) {
					continue;
				}
				const auto soul = itGem->second->GetContainedSoul();
				if (soul <= RE::SOUL_LEVEL::kNone) {
					continue;
				}
				counts[{ formId, soul }] += count;
			}

			for (const auto& [key, count] : counts) {
				if (count <= 0) {
					continue;
				}
				a_out.push_back(SoulGemOption{
					.gemFormID = key.first,
					.source = a_source,
					.count = count,
					.soul = key.second,
				});
			}
		}

		[[nodiscard]] int SoulRank(RE::SOUL_LEVEL a_soul)
		{
			return static_cast<int>(a_soul);
		}
	}

	std::vector<SoulGemOption> GatherFilledSoulGems(RE::Actor* a_player, RE::Actor* a_follower)
	{
		std::vector<SoulGemOption> result;
		result.reserve(32);

		GatherOne(a_player, SoulGemSource::kPlayer, result);
		GatherOne(a_follower, SoulGemSource::kFollower, result);

		std::sort(result.begin(), result.end(), [&](const SoulGemOption& a, const SoulGemOption& b) {
			// Sort by strongest soul, then source, then formID.
			if (a.soul != b.soul) {
				return SoulRank(a.soul) > SoulRank(b.soul);
			}
			if (a.source != b.source) {
				return static_cast<std::uint8_t>(a.source) < static_cast<std::uint8_t>(b.source);
			}
			return a.gemFormID < b.gemFormID;
		});

		return result;
	}
}
