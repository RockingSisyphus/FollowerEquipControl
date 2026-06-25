// Stores player-selected equip preferences used by follower combat behavior.

#pragma once

#include "PCH.h"

#include "InstanceSignature.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace FEC
{
	class CombatEquipPreference
	{
	public:
		enum class Category : std::uint8_t
		{
			kOneHandRight = 0,
			kOneHandLeft,
			kShieldLeft,
			kTwoHand,

			kBow,
			kArrow,
			kCrossbow,
			kBolt,

			kStaffRight,
			kStaffLeft,

			kScrollRight,
			kScrollLeft,
			kScrollBoth,

			kHeadgear,

			kTotal
		};

		struct Entry
		{
			RE::FormID baseObjectID{ 0 };
			InstanceSignature signature{};
		};

		struct SnapshotEntry
		{
			RE::FormID actorID{ 0 };
			Category category{ Category::kOneHandRight };
			Entry entry{};
		};

		static void Clear() noexcept;

		// Trade equip-mode capture entry points. Missing or meaningless inputs fail open.
		static void CaptureUserEquip(
			RE::Actor* a_actor,
			RE::TESBoundObject* a_object,
			bool a_leftHand,
			const std::optional<InstanceSignature>& a_sig);
		// Selection-based overload avoids an InstanceSignature dependency in equip_mode.
		// Null a_xList with a_hasSelection stores an empty signature.
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

		// Serializer boundary. Stored signatures are full InstanceSignature values.
		static std::vector<SnapshotEntry> SnapshotEntries();
		static void SetLoadedEntry(RE::FormID a_actorID, Category a_cat, Entry a_entry);

		// Direct preference clear used by the headgear secondary-key toggle.
		static void ClearCategoryEntry(RE::FormID a_actorID, Category a_cat);

		// Cleanup for actors leaving the world.
		static void EraseActor(RE::FormID a_actorID);

		// Returns std::nullopt if no entry is stored.
		static std::optional<Entry> GetEntry(RE::FormID a_actorID, Category a_cat);

		// Seeds currently equipped gear when the actor has no matching preference yet.
		// One-time, fail-open; called from ContainerMenuOpenActions.
		static void SeedFromEquippedIfEmpty(RE::Actor* a_actor);
	};
}
