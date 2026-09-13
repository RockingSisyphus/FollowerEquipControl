#include "ActorScopeExcludeState.h"

#include <mutex>
#include <unordered_set>
#include <vector>

namespace FEC::ActorScopeExcludeState
{
	namespace
	{
		std::mutex g_mutex;
		std::unordered_set<RE::FormID> g_data;
	}

	bool IsExcluded(RE::FormID a_actorID) noexcept
	{
		if (a_actorID == 0) {
			return false;
		}
		std::lock_guard lk{ g_mutex };
		return g_data.count(a_actorID) > 0;
	}

	bool Exclude(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return false;
		}
		std::lock_guard lk{ g_mutex };
		return g_data.insert(a_actorID).second;
	}

	bool Include(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return false;
		}
		std::lock_guard lk{ g_mutex };
		return g_data.erase(a_actorID) > 0;
	}

	void Clear() noexcept
	{
		std::lock_guard lk{ g_mutex };
		g_data.clear();
	}

	std::uint32_t GetCount() noexcept
	{
		std::lock_guard lk{ g_mutex };
		return static_cast<std::uint32_t>(g_data.size());
	}

	void ForEach(const Visitor& a_visitor)
	{
		// Snapshot under lock to avoid holding the lock during the callback,
		// which may itself call IsExcluded (which would deadlock on the same mutex).
		std::vector<RE::FormID> snapshot;
		{
			std::lock_guard lk{ g_mutex };
			snapshot.assign(g_data.begin(), g_data.end());
		}
		for (const auto id : snapshot) {
			a_visitor(id);
		}
	}
}
