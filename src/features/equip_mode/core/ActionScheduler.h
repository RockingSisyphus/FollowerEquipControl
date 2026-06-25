// Defers work to the next frame through SKSE's task interface to avoid hook re-entrancy.

#pragma once

#include "PCH.h"

namespace FEC::EquipMode::Core
{
	class ActionScheduler
	{
	public:
		template <class Fn>
		static void NextFrame(Fn&& a_fn)
		{
			if (auto* taskInterface = SKSE::GetTaskInterface()) {
				taskInterface->AddTask(std::forward<Fn>(a_fn));
			}
		}
	};
}
