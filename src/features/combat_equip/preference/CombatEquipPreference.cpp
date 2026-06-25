#include "CombatEquipPreference.h"

#include "ActorScope.h"
#include "CombatEquipPreferencePolicy.h"
#include "PluginSettings.h"

#include <array>
#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace FEC
{
	namespace
	{
		using Cat = CombatEquipPreference::Category;

		[[nodiscard]] const char* CategoryName(Cat a_cat) noexcept
		{
			switch (a_cat) {
			case Cat::kOneHandRight:
				return "OneHandRight";
			case Cat::kOneHandLeft:
				return "OneHandLeft";
			case Cat::kShieldLeft:
				return "ShieldLeft";
			case Cat::kTwoHand:
				return "TwoHand";
			case Cat::kBow:
				return "Bow";
			case Cat::kArrow:
				return "Arrow";
			case Cat::kCrossbow:
				return "Crossbow";
			case Cat::kBolt:
				return "Bolt";
			case Cat::kStaffRight:
				return "StaffRight";
			case Cat::kStaffLeft:
				return "StaffLeft";
			case Cat::kScrollRight:
				return "ScrollRight";
			case Cat::kScrollLeft:
				return "ScrollLeft";
			case Cat::kScrollBoth:
				return "ScrollBoth";
			case Cat::kHeadgear:
				return "Headgear";
			default:
				return "?";
			}
		}

		constexpr std::size_t CategoryCount() noexcept
		{
			return static_cast<std::size_t>(Cat::kTotal);
		}

		constexpr std::size_t ToIndex(Cat a_cat) noexcept
		{
			return static_cast<std::size_t>(a_cat);
		}

		struct ActorState
		{
			std::array<std::optional<CombatEquipPreference::Entry>, CategoryCount()> entries{};
		};

		[[nodiscard]] bool IsEmptyLocked(const ActorState& a_state) noexcept
		{
			for (const auto& e : a_state.entries) {
				if (e.has_value()) {
					return false;
				}
			}
			return true;
		}

		class CombatEquipPreferenceStore
		{
		public:
			void Clear() noexcept
			{
				std::unique_lock lock(_mutex);
				_byActor.clear();
			}

			void CaptureUserEquip(RE::FormID a_actorID, RE::FormID a_objectID, Cat a_cat, const InstanceSignature& a_sig)
			{
				const auto idx = ToIndex(a_cat);
				if (idx >= CategoryCount()) {
					return;
				}

				CombatEquipPreference::Entry entry;
				entry.baseObjectID = a_objectID;
				entry.signature = a_sig;

				std::unique_lock lock(_mutex);
				auto& st = _byActor[a_actorID];
				ApplyClearMaskLocked(st, FEC::CombatEquip::Preference::Policy::CrossClearMaskOnCaptureEquip(a_cat));
				st.entries[idx] = std::move(entry);
			}

			void CaptureUserUnequipClear(RE::FormID a_actorID, Cat a_cat)
			{
				const auto idx = ToIndex(a_cat);
				if (idx >= CategoryCount()) {
					return;
				}

				std::unique_lock lock(_mutex);
				auto it = _byActor.find(a_actorID);
				if (it == _byActor.end()) {
					return;
				}

				it->second.entries[idx].reset();
				if (IsEmptyLocked(it->second)) {
					_byActor.erase(it);
				}
			}

			std::vector<CombatEquipPreference::SnapshotEntry> SnapshotEntries() const
			{
				std::vector<CombatEquipPreference::SnapshotEntry> out;
				std::shared_lock lock(_mutex);
				for (const auto& [actorID, st] : _byActor) {
					for (std::size_t i = 0; i < st.entries.size(); i++) {
						if (!st.entries[i].has_value()) {
							continue;
						}
						CombatEquipPreference::SnapshotEntry e;
						e.actorID = actorID;
						e.category = static_cast<Cat>(i);
						e.entry = *st.entries[i];
						out.emplace_back(std::move(e));
					}
				}
				return out;
			}

			void SetLoadedEntry(RE::FormID a_actorID, Cat a_cat, CombatEquipPreference::Entry a_entry)
			{
				if (a_actorID == 0 || a_entry.baseObjectID == 0) {
					return;
				}

				const auto idx = ToIndex(a_cat);
				if (idx >= CategoryCount()) {
					return;
				}

				std::unique_lock lock(_mutex);
				auto& st = _byActor[a_actorID];
				st.entries[idx] = std::move(a_entry);
			}

			std::optional<CombatEquipPreference::Entry> GetEntry(RE::FormID a_actorID, Cat a_cat) const
			{
				if (a_actorID == 0) {
					return std::nullopt;
				}

				const auto idx = ToIndex(a_cat);
				if (idx >= CategoryCount()) {
					return std::nullopt;
				}

				std::shared_lock lock(_mutex);
				auto it = _byActor.find(a_actorID);
				if (it == _byActor.end()) {
					return std::nullopt;
				}
				if (!it->second.entries[idx].has_value()) {
					return std::nullopt;
				}
				return it->second.entries[idx];
			}

			void EraseActor(RE::FormID a_actorID) noexcept
			{
				if (a_actorID == 0) {
					return;
				}

				std::unique_lock lock(_mutex);
				_byActor.erase(a_actorID);
			}

		private:
			static void ApplyClearMaskLocked(ActorState& a_state, std::uint16_t a_mask) noexcept
			{
				for (std::size_t i = 0; i < CategoryCount(); i++) {
					if ((a_mask & (1u << i)) != 0) {
						a_state.entries[i].reset();
					}
				}
			}

			mutable std::shared_mutex _mutex;
			std::unordered_map<RE::FormID, ActorState> _byActor;
		};

		CombatEquipPreferenceStore g_store;
	}

	void CombatEquipPreference::Clear() noexcept
	{
		g_store.Clear();
	}

	void CombatEquipPreference::CaptureUserEquip(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		bool a_leftHand,
		const std::optional<InstanceSignature>& a_sig)
	{
		{
			const auto& cfg = PluginSettings::Get();
			if (!cfg.combatEquipPreference.enableScoring && !cfg.combatEquipEnforcement.enableAmmoPreference && !cfg.combatEquipRestore.enableHeadgearAutoEquip) {
				return;
			}
		}

		if (!a_actor || !a_object || a_actor->IsDead()) {
			return;
		}

		const auto actorID = a_actor->GetFormID();
		const auto objectID = a_object->GetFormID();
		if (actorID == 0 || objectID == 0) {
			return;
		}

		const auto cat = FEC::CombatEquip::Preference::Policy::TryClassifyCEPCategory(a_object, a_leftHand);
		if (!cat.has_value()) {
			return;
		}

		// Ammo is not part of the combat-score path. Arrows and bolts are stored
		// as base-only preferences for CombatEquipOverrideAmmo; other categories require instance identity.
		if (!a_sig.has_value()) {
			if (*cat == Category::kArrow || *cat == Category::kBolt) {
				g_store.CaptureUserEquip(actorID, objectID, *cat, InstanceSignature{});
			} else {
				return;
			}
		} else {
			g_store.CaptureUserEquip(actorID, objectID, *cat, *a_sig);
		}
		if (spdlog::should_log(spdlog::level::debug)) {
			logger::debug(
				"CEPR: capture equip actor={:08X} object={:08X} cat={} ({}) hand={}",
				actorID,
				objectID,
				static_cast<std::uint32_t>(static_cast<std::uint8_t>(*cat)),
				CategoryName(*cat),
				a_leftHand ? "left" : "right");
		}
	}

	void CombatEquipPreference::CaptureUserEquip(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		bool a_leftHand,
		RE::ExtraDataList* a_xList,
		bool a_hasSelection)
	{
		{
			const auto& cfg = PluginSettings::Get();
			if (!cfg.combatEquipPreference.enableScoring && !cfg.combatEquipEnforcement.enableAmmoPreference && !cfg.combatEquipRestore.enableHeadgearAutoEquip) {
				return;
			}
		}

		if (!a_actor || !a_object || a_actor->IsDead()) {
			return;
		}

		const auto actorID = a_actor->GetFormID();
		const auto objectID = a_object->GetFormID();
		if (actorID == 0 || objectID == 0) {
			return;
		}

		const auto cat = FEC::CombatEquip::Preference::Policy::TryClassifyCEPCategory(a_object, a_leftHand);
		if (!cat.has_value()) {
			return;
		}

		const bool allowBaseOnly = (*cat == Category::kArrow || *cat == Category::kBolt);
		if (!a_xList && !a_hasSelection) {
			if (allowBaseOnly) {
				g_store.CaptureUserEquip(actorID, objectID, *cat, InstanceSignature{});
			} else {
				return;
			}
		} else {
			InstanceSignature sig{};
			if (a_xList) {
				sig = BuildInstanceSignature(*a_xList, a_object);
			}
			g_store.CaptureUserEquip(actorID, objectID, *cat, sig);
		}

		if (spdlog::should_log(spdlog::level::debug)) {
			logger::debug(
				"CEPR: capture equip actor={:08X} object={:08X} cat={} ({}) hand={}",
				actorID,
				objectID,
				static_cast<std::uint32_t>(static_cast<std::uint8_t>(*cat)),
				CategoryName(*cat),
				a_leftHand ? "left" : "right");
		}
	}

	void CombatEquipPreference::CaptureUserUnequip(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		bool a_leftHand)
	{
		{
			const auto& cfg = PluginSettings::Get();
			if (!cfg.combatEquipPreference.enableScoring && !cfg.combatEquipEnforcement.enableAmmoPreference && !cfg.combatEquipRestore.enableHeadgearAutoEquip) {
				return;
			}
		}

		if (!a_actor || !a_object || a_actor->IsDead()) {
			return;
		}

		const auto actorID = a_actor->GetFormID();
		if (actorID == 0) {
			return;
		}

		const auto cat = FEC::CombatEquip::Preference::Policy::TryClassifyCEPCategory(a_object, a_leftHand);
		if (!cat.has_value()) {
			return;
		}
		if (!FEC::CombatEquip::Preference::Policy::ShouldClearOnCaptureUnequip(*cat, a_leftHand)) {
			return;
		}
		g_store.CaptureUserUnequipClear(actorID, *cat);
		if (spdlog::should_log(spdlog::level::debug)) {
			logger::debug(
				"CEPR: capture unequip-clear actor={:08X} cat={} ({}) hand={}",
				actorID,
				static_cast<std::uint32_t>(static_cast<std::uint8_t>(*cat)),
				CategoryName(*cat),
				a_leftHand ? "left" : "right");
		}
	}

	std::vector<CombatEquipPreference::SnapshotEntry> CombatEquipPreference::SnapshotEntries()
	{
		return g_store.SnapshotEntries();
	}

	void CombatEquipPreference::SetLoadedEntry(RE::FormID a_actorID, Category a_cat, Entry a_entry)
	{
		g_store.SetLoadedEntry(a_actorID, a_cat, std::move(a_entry));
	}

	std::optional<CombatEquipPreference::Entry> CombatEquipPreference::GetEntry(RE::FormID a_actorID, Category a_cat)
	{
		return g_store.GetEntry(a_actorID, a_cat);
	}

	void CombatEquipPreference::ClearCategoryEntry(RE::FormID a_actorID, Category a_cat)
	{
		if (a_actorID == 0) {
			return;
		}
		g_store.CaptureUserUnequipClear(a_actorID, a_cat);
		if (spdlog::should_log(spdlog::level::debug)) {
			logger::debug(
				"CEPR: force-clear actor={:08X} cat={} ({})",
				a_actorID,
				static_cast<std::uint32_t>(static_cast<std::uint8_t>(a_cat)),
				CategoryName(a_cat));
		}
	}

	void CombatEquipPreference::EraseActor(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return;
		}
		g_store.EraseActor(a_actorID);
		if (spdlog::should_log(spdlog::level::debug)) {
			logger::debug("CEPR: erase actor={:08X}", a_actorID);
		}
	}

	void CombatEquipPreference::SeedFromEquippedIfEmpty(RE::Actor* a_actor)
	{
		if (!a_actor) {
			return;
		}

		const auto actorID = a_actor->GetFormID();
		if (actorID == 0) {
			return;
		}

		// Seed headgear independently of melee preference, but only when body armor is allowed.
		if (ActorScope::BodyEquipAllowed(a_actor) && !g_store.GetEntry(actorID, Category::kHeadgear).has_value()) {
			constexpr RE::BGSBipedObjectForm::BipedObjectSlot kHeadSlots[] = {
				RE::BGSBipedObjectForm::BipedObjectSlot::kHead,
				RE::BGSBipedObjectForm::BipedObjectSlot::kHair,
				RE::BGSBipedObjectForm::BipedObjectSlot::kCirclet,
			};
			for (auto slot : kHeadSlots) {
				auto* worn = a_actor->GetWornArmor(slot);
				if (!worn || !worn->GetPlayable()) {
					continue;
				}
				auto cat = FEC::CombatEquip::Preference::Policy::TryClassifyCEPCategory(worn, false);
				if (cat.has_value() && *cat == Category::kHeadgear) {
					Entry entry;
					entry.baseObjectID = worn->GetFormID();
					g_store.SetLoadedEntry(actorID, Category::kHeadgear, std::move(entry));
					logger::debug("CEPR: auto-seed headgear actor={:08X} object={:08X}",
						actorID, worn->GetFormID());
					break;
				}
			}
		}

		// One-time melee seed; user-selected melee preferences take precedence.
		static constexpr Category kMeleeCategories[] = {
			Category::kOneHandRight,
			Category::kOneHandLeft,
			Category::kShieldLeft,
			Category::kTwoHand,
		};
		for (auto cat : kMeleeCategories) {
			if (g_store.GetEntry(actorID, cat).has_value()) {
				return;
			}
		}

		auto seedHand = [&](bool a_leftHand) {
			auto* form = a_actor->GetEquippedObject(a_leftHand);
			if (!form) {
				return;
			}
			RE::TESBoundObject* obj = nullptr;
			if (auto* w = form->As<RE::TESObjectWEAP>()) {
				obj = w;
			} else if (auto* a = form->As<RE::TESObjectARMO>()) {
				obj = a;
			}
			if (!obj) {
				return;
			}
			auto cat = FEC::CombatEquip::Preference::Policy::TryClassifyCEPCategory(obj, a_leftHand);
			if (!cat.has_value()) {
				return;
			}
			Entry entry;
			entry.baseObjectID = obj->GetFormID();
			g_store.SetLoadedEntry(actorID, *cat, std::move(entry));
			logger::debug("CEPR: auto-seed {} actor={:08X} object={:08X} cat={} ({})",
				a_leftHand ? "left " : "right", actorID, obj->GetFormID(),
				static_cast<int>(*cat), CategoryName(*cat));
		};

		seedHand(false);
		seedHand(true);
	}
}
