// Captures and restores pre-combat headgear state.

#pragma once

#include "PCH.h"

#include "InstanceSignature.h"

#include <optional>
#include <vector>

namespace FEC::HeadgearAutoEquip
{
	struct WornHeadgearItem
	{
		RE::FormID baseObjectID{ 0 };
		std::optional<InstanceSignature> signature{};
	};

	struct SnapshotEntry
	{
		RE::FormID actorID{ 0 };
		// Headgear worn before combat.
		std::vector<WornHeadgearItem> wornHeadgear;
	};

	void Install();
	void Uninstall();

	std::vector<SnapshotEntry> SnapshotEntries();
	void SetLoadedEntry(SnapshotEntry a_entry);
	void ClearSnapshots();
	void EraseActor(RE::FormID a_actorID);
}
