#include "SpellSuppressionState.h"

#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace FEC::SpellSuppressionState
{
	namespace
	{
		std::mutex g_mutex;
		std::unordered_map<RE::FormID, std::unordered_set<RE::FormID>> g_data;
	}

	bool IsSuppressed(RE::FormID a_actorID, RE::FormID a_spellID) noexcept
	{
		if (a_actorID == 0 || a_spellID == 0) {
			return false;
		}
		std::lock_guard lk{ g_mutex };
		const auto it = g_data.find(a_actorID);
		if (it == g_data.end()) {
			return false;
		}
		return it->second.count(a_spellID) > 0;
	}

	bool Suppress(RE::FormID a_actorID, RE::FormID a_spellID)
	{
		if (a_actorID == 0 || a_spellID == 0) {
			return false;
		}
		std::lock_guard lk{ g_mutex };
		return g_data[a_actorID].insert(a_spellID).second;
	}

	bool Unsuppress(RE::FormID a_actorID, RE::FormID a_spellID)
	{
		if (a_actorID == 0 || a_spellID == 0) {
			return false;
		}
		std::lock_guard lk{ g_mutex };
		const auto it = g_data.find(a_actorID);
		if (it == g_data.end()) {
			return false;
		}
		const bool erased = it->second.erase(a_spellID) > 0;
		if (it->second.empty()) {
			g_data.erase(it);
		}
		return erased;
	}

	void EraseActor(RE::FormID a_actorID) noexcept
	{
		if (a_actorID == 0) {
			return;
		}
		std::lock_guard lk{ g_mutex };
		g_data.erase(a_actorID);
	}

	void Clear() noexcept
	{
		std::lock_guard lk{ g_mutex };
		g_data.clear();
	}

	void ForEachSuppressed(const PairVisitor& a_visitor)
	{
		if (!a_visitor) {
			return;
		}
		// Snapshot under lock to avoid recursive callbacks while holding the mutex.
		std::vector<std::pair<RE::FormID, RE::FormID>> snapshot;
		{
			std::lock_guard lk{ g_mutex };
			for (const auto& [actorID, spells] : g_data) {
				for (auto spellID : spells) {
					snapshot.emplace_back(actorID, spellID);
				}
			}
		}
		for (const auto& [a, s] : snapshot) {
			a_visitor(a, s);
		}
	}

	std::vector<RE::FormID> GetSuppressedSpells(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return {};
		}
		std::lock_guard lk{ g_mutex };
		const auto it = g_data.find(a_actorID);
		if (it == g_data.end()) {
			return {};
		}
		return std::vector<RE::FormID>(it->second.begin(), it->second.end());
	}

	void RestoreAndEraseActor(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return;
		}

		// Erase synchronously so cleanup completes even if async restore fails.
		const std::vector<RE::FormID> spellIDs = GetSuppressedSpells(a_actorID);
		EraseActor(a_actorID);

		if (spellIDs.empty()) {
			return;
		}

		// Actor/SpellItem mutation must run on the game thread.
		auto* tasks = SKSE::GetTaskInterface();
		if (!tasks) {
			logger::warn("SpellSuppressionState: task interface unavailable; "
				"could not restore {} spell(s) to actor {:08X}", spellIDs.size(), a_actorID);
			return;
		}

		tasks->AddTask([a_actorID, spellIDs]() {
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
			if (!actor) {
				return;
			}
			std::size_t restored = 0;
			for (const RE::FormID spellID : spellIDs) {
				if (spellID == 0) {
					continue;
				}
				auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(spellID);
				if (!spell) {
					continue;
				}
				if (!actor->HasSpell(spell)) {
					actor->AddSpell(spell);
					++restored;
				}
			}
			if (restored > 0) {
				logger::info("SpellSuppressionState: restored {} suppressed spell(s) to actor {:08X}",
					restored, a_actorID);
			}
		});
	}

	std::uint32_t GetTotalCount() noexcept
	{
		std::lock_guard lk{ g_mutex };
		std::uint32_t total = 0;
		for (const auto& [_, spells] : g_data) {
			total += static_cast<std::uint32_t>(spells.size());
		}
		return total;
	}
}
