#include "ActorStateCleanup.h"

#include "CombatEquipOverrideState.h"
#include "CombatEquipPreference.h"
#include "EquipGateTelemetry.h"
#include "HandItemRestore.h"
#include "HeadgearAutoEquip.h"
#include "KnownFollowerState.h"
#include "OutfitSnapshotRestore.h"
#include "PreCombatEquipRestore.h"
#include "SpellSuppressionState.h"

namespace FEC::ActorStateCleanup
{
	void PurgeActorFromAllStores(RE::FormID a_formID)
	{
		if (a_formID == 0) {
			return;
		}

		KnownFollowerState::EraseActor(a_formID);
		CombatEquipPreference::EraseActor(a_formID);
		HeadgearAutoEquip::EraseActor(a_formID);
		PreCombatEquipRestore::EraseActor(a_formID);
		OutfitSnapshotRestore::EraseActor(a_formID);
		HandItemRestore::EraseActor(a_formID);
		CombatEquipOverride::State::EraseActor(a_formID);
		EquipGate::Telemetry::EraseActor(a_formID);
		SpellSuppressionState::RestoreAndEraseActor(a_formID);
	}
}
