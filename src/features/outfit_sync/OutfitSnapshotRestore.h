// Preserves user-driven follower armor selections across UpdateNPCOutfit suppression and save/load.

#pragma once

#include "PCH.h"

#include "InstanceSignature.h"

#include <optional>
#include <unordered_set>
#include <vector>

namespace FEC
{
	class OutfitSnapshotRestore
	{
	public:
		struct Entry
		{
			RE::FormID baseObjectID{ 0 };
			std::optional<InstanceSignature> signature{};
		};

		struct SnapshotEntry
		{
			RE::FormID actorID{ 0 };
			std::uint32_t slotID{ 0 };
			Entry entry{};
		};

		static void Install();
		static void Uninstall();

		static std::vector<SnapshotEntry> SnapshotEntries();
		static void SetLoadedEntry(RE::FormID a_actorID, std::uint32_t a_slotID, Entry a_entry);
		static void ClearSnapshots();
		static void EraseActor(RE::FormID a_actorID);

		static void CaptureUserEquip(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			const std::optional<InstanceSignature>& a_sig);

		// Avoids an InstanceSignature dependency in equip_mode.
		// If a_xList is null, a_hasSelection controls whether an empty signature is stored.
		static void CaptureUserEquip(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			RE::ExtraDataList* a_xList,
			bool a_hasSelection);

		static void CaptureUserUnequip(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object);

		// Captures the enrollment baseline so suppression starts with an armor record.
		static void CaptureBaselineIfEmpty(RE::Actor* a_actor);

		static void OnUpdateNpcOutfitSuppressed(RE::Actor* a_actor);

		// Called after vanilla UpdateNPCOutfit runs for an external defaultOutfit change.
		// Rebuilds the snapshot from worn state; caller gates this with allowOutfitChanges.
		static void OnOutfitFormChanged(RE::Actor* a_actor);

		// Called after external armor equip or unequip completes on a managed follower.
		// Reseeds armor from worn state for direct EquipObject outfit systems.
		static void OnExternalArmorEquip(RE::Actor* a_actor);

		static std::unordered_set<RE::FormID> GetSnapshotBaseObjectIDs(RE::FormID a_actorID);

		static std::vector<Entry> GetSnapshotEntries(RE::FormID a_actorID);
	};
}
