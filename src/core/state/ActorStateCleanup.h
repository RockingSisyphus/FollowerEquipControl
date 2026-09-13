// Central purge point for per-actor state stores used by deletion, ash-pile, and post-load cleanup paths.

#pragma once

#include "PCH.h"

namespace FEC::ActorStateCleanup
{
	/// Purges one actor from every per-actor state store.
	/// Missing FormIDs are no-ops.
	/// Spell suppression restores spells asynchronously on the game thread before erasing its record.
	void PurgeActorFromAllStores(RE::FormID a_formID);
}
