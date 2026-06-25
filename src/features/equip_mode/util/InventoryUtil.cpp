#include "InventoryUtil.h"

namespace FEC::InventoryUtil
{
	std::int32_t GetTotalCount(RE::Actor* a_actor, RE::TESBoundObject* a_object)
	{
		if (!a_actor || !a_object) {
			return 0;
		}
		auto inv = a_actor->GetInventory([&](RE::TESBoundObject& a_obj) {
			return &a_obj == a_object;
		});
		auto it = inv.find(a_object);
		return it != inv.end() ? it->second.first : 0;
	}
}
