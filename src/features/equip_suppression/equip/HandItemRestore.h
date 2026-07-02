// Preserves user-driven follower hand item selections across outfit/equipment refreshes and save/load.

#pragma once

#include "PCH.h"

#include "InstanceSignature.h"

#include <optional>
#include <vector>

namespace FEC
{
	class HandItemRestore
	{
	public:
		enum class SlotKind : std::uint8_t
		{
			kRightHand = 0,
			kLeftHand,
			kAmmo,
		};

		struct Entry
		{
			RE::FormID baseObjectID{ 0 };
			std::optional<InstanceSignature> signature{};
		};

		struct SnapshotEntry
		{
			RE::FormID actorID{ 0 };
			SlotKind kind{ SlotKind::kRightHand };
			Entry entry{};
		};

		static void Install();
		static void Uninstall();

		static std::vector<SnapshotEntry> SnapshotEntries();
		static void SetLoadedEntry(RE::FormID a_actorID, SlotKind a_kind, Entry a_entry);
		static void ClearSnapshots();
		static void EraseActor(RE::FormID a_actorID);

		static void CaptureUserEquip(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			bool a_leftHand,
			const std::optional<InstanceSignature>& a_sig);

		// Avoids an InstanceSignature dependency in equip_mode.
		// If a_xList is null, a_hasSelection controls whether an empty signature is stored.
		static void CaptureUserEquip(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			bool a_leftHand,
			RE::ExtraDataList* a_xList,
			bool a_hasSelection);

		static void CaptureUserUnequip(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			bool a_leftHand);

		// Captures the enrollment baseline so refresh handling starts with a hand item record.
		static void CaptureBaselineIfEmpty(RE::Actor* a_actor);

		static void RequestReconcile(RE::Actor* a_actor);
		static void RequestReconcileAfterMenuClose(RE::Actor* a_actor);

		static std::vector<Entry> GetSnapshotEntries(RE::FormID a_actorID);
	};
}
