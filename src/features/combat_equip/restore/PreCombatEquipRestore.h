// Captures and restores pre-combat weapon, shield, and ammo state.

#pragma once

#include "PCH.h"

#include "InstanceSignature.h"

#include <optional>
#include <vector>

namespace FEC::PreCombatEquipRestore
{
	enum class SlotKind : std::uint8_t
	{
		kNone = 0,
		kWeapon = 1,
		kShield = 2,
	};

	struct SlotSnapshot
	{
		SlotKind kind{ SlotKind::kNone };
		RE::FormID baseObjectID{ 0 };
		std::optional<InstanceSignature> signature{};
	};

	struct SnapshotEntry
	{
		RE::FormID actorID{ 0 };
		SlotSnapshot right{};
		SlotSnapshot left{};
		RE::FormID ammoID{ 0 };
	};

	void Install();
	void Uninstall();

	std::vector<SnapshotEntry> SnapshotEntries();
	void SetLoadedEntry(SnapshotEntry a_entry);
	void ClearSnapshots();
	void EraseActor(RE::FormID a_actorID);
}
