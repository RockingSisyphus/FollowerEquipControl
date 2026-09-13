// Actor-scope extension rules for third-party follower mods and frameworks.
// Files are read from <SKSE/Plugins>/FollowerEquipControl/Actors/*.ini at startup.
// Form pointers are resolved lazily on first use so SPID and other distributors can finish.
//
// Sections:
//   [IncludeByKeyword.Mode]  Keyword = EditorID, Plugin.esp|0xLocalID, ...
//   [IncludeByNPC.Mode]      NPC = Plugin.esp|0xLocalID, ...
//   [IncludeByFaction.Mode]  Faction = Plugin.esp|0xLocalID, ...
//
// Plain EditorIDs are supported only for keywords; TESNPC and TESFaction do not retain
// EditorIDs in vanilla Skyrim. Omit .Mode to default to NPCTrade.
// Mode suffixes: NPCTrade, Loot, Steal, Pickpocket.

#pragma once

#include "PCH.h"

namespace FEC::ActorInclusion
{
	// Loads inclusion INI entries as strings; pointer resolution is deferred until first use.
	// Safe at kDataLoaded before SPID or other distributors have run.
	void Load();

	// Checks inclusion rules for the given container mode.
	[[nodiscard]] bool MatchesForMode(RE::Actor* a_actor, RE::ContainerMenu::ContainerMode a_mode) noexcept;

	// Checks inclusion rules across all modes; used by scope guards outside a live menu.
	[[nodiscard]] bool MatchesAny(RE::Actor* a_actor) noexcept;
}
