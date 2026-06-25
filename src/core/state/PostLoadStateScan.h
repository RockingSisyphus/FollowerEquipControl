// After a save load, purges stale per-actor state for known followers that no longer need tracking.
// Covers ash-piled actors still present in the session and dead 0xFF dynamic actors from prior sessions.
// TESFormDeleteEvent does not fire for those dynamic actors across sessions.

#pragma once

#include "PCH.h"

namespace FEC::PostLoadStateScan
{
	// Safe to call from kPostLoadGame or kNewGame.
	void Run();
}
