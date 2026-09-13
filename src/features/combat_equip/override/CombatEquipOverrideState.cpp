#include "CombatEquipOverrideState.h"

#include <mutex>
#include <unordered_map>

namespace FEC::CombatEquipOverride::State
{
	namespace
	{
		struct AmmoState
		{
			RE::FormID ammoID{ 0 };
		};

		std::mutex g_lock;
		std::unordered_map<RE::FormID, AmmoState> g_lastAmmoByActor;
	}

	void RememberLastAmmoOverride(RE::FormID a_actorID, RE::FormID a_ammoBaseID)
	{
		if (a_actorID == 0 || a_ammoBaseID == 0) {
			return;
		}
		std::scoped_lock lock(g_lock);
		g_lastAmmoByActor[a_actorID] = AmmoState{ a_ammoBaseID };
	}

	std::optional<RE::FormID> GetLastAmmoOverride(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return std::nullopt;
		}
		std::scoped_lock lock(g_lock);
		const auto it = g_lastAmmoByActor.find(a_actorID);
		if (it == g_lastAmmoByActor.end() || it->second.ammoID == 0) {
			return std::nullopt;
		}
		return it->second.ammoID;
	}

	void Clear()
	{
		std::scoped_lock lock(g_lock);
		g_lastAmmoByActor.clear();
	}

	void EraseActor(RE::FormID a_actorID)
	{
		if (a_actorID == 0) {
			return;
		}
		std::scoped_lock lock(g_lock);
		g_lastAmmoByActor.erase(a_actorID);
	}
}
