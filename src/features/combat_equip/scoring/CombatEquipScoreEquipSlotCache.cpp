#include "CombatEquipScoreEquipSlotCache.h"

#include <mutex>

namespace FEC::CombatEquipScoreEquipSlotCache
{
	namespace
	{
		[[nodiscard]] const RE::BGSEquipSlot* GetDefaultEquipSlot(RE::DEFAULT_OBJECT a_id) noexcept
		{
			auto* dom = RE::BGSDefaultObjectManager::GetSingleton();
			if (!dom) {
				return nullptr;
			}
			return dom->GetObject<RE::BGSEquipSlot>(a_id);
		}
	}

	const HandSlots& GetHandSlots() noexcept
	{
		static std::once_flag once;
		static HandSlots slots;
		std::call_once(once, []() {
			slots.left = GetDefaultEquipSlot(RE::DEFAULT_OBJECT::kLeftHandEquip);
			slots.right = GetDefaultEquipSlot(RE::DEFAULT_OBJECT::kRightHandEquip);
		});
		return slots;
	}
}
