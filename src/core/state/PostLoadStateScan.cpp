#include "PostLoadStateScan.h"

#include "ActorStateCleanup.h"
#include "KnownFollowerState.h"
#include "Logging.h"

#include "RE/E/ExtraAshPileRef.h"

#include <vector>

namespace FEC::PostLoadStateScan
{
	void Run()
	{
		auto* taskInterface = SKSE::GetTaskInterface();
		if (!taskInterface) {
			return;
		}

		taskInterface->AddTask([]() {
			// Snapshot before purging; the live known-follower set may be erased during cleanup.
			std::vector<RE::FormID> candidates;
			candidates.reserve(KnownFollowerState::GetKnownFollowerCount());
			KnownFollowerState::ForEachKnownFollower([&](RE::FormID a_formID) {
				candidates.push_back(a_formID);
			});

			std::size_t purgedAshPile = 0;
			std::size_t purgedDynamic = 0;

			for (const auto formID : candidates) {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID);
				if (!actor) {
					// Dynamic 0xFF forms are not recreated on load, and no delete event will fire for them.
					// Keep missing persistent forms, such as actors from unloaded plugins.
					if ((formID & 0xFF000000) == 0xFF000000) {
						ActorStateCleanup::PurgeActorFromAllStores(formID);
						++purgedDynamic;
					}
					continue;
				}
				if (!actor->extraList.HasType<RE::ExtraAshPileRef>()) {
					continue;
				}

				ActorStateCleanup::PurgeActorFromAllStores(formID);
				++purgedAshPile;
			}

			const auto purgedTotal = purgedAshPile + purgedDynamic;
			if (purgedTotal > 0) {
				logger::info("PostLoadStateScan: purged {} stale actor(s) ({} ash pile(s), {} dead dynamic)",
					purgedTotal, purgedAshPile, purgedDynamic);
			} else {
				logger::debug("PostLoadStateScan: scanned {} known follower(s), nothing to purge", candidates.size());
			}
		});
	}
}
