// Address Library relocation IDs and stable vfunc indices used by hooks.

#pragma once

#include "PCH.h"

namespace FEC::Relocations
{
	inline const REL::RelocationID kContainerMenuVtbl{ 268222, 215061 };
	inline const REL::RelocationID kItemListUpdate{ 50099, 51031 };
	inline const REL::RelocationID kUpdateNPCOutfit{ 24234, 418622 };
	inline const REL::RelocationID kContainerMenuAddObjectToContainer{ 50212, 51141 };
	inline const REL::RelocationID kActorEquipManagerEquipSpell{ 37939, 38895 };
	inline const REL::RelocationID kGetInventoryWeight{ 15883, 16123 };
	inline const REL::RelocationID kInitLeveledItems{ 15889, 16129 };
	inline const REL::RelocationID kInitOutfitItems{ 15833, 16072 };
	inline const REL::RelocationID kAttachAshPileFunctorVtbl{ 272443, 217344 };

	// Vfunc indices are stable across supported SE/AE runtimes and are documented
	// in CommonLibSSE-NG headers (e.g., TESObjectREFR::AddObjectToContainer // 5A).
	inline constexpr std::size_t kContainerMenuPostDisplayVfuncIndex = 0x06;
	inline constexpr std::size_t kTESObjectREFR_RemoveItemVfuncIndex = 0x56;
	inline constexpr std::size_t kTESObjectREFR_AddObjectToContainerVfuncIndex = 0x5A;
	inline constexpr std::size_t kActor_PickUpObjectVfuncIndex = 0xCC;
	inline constexpr std::size_t kCombatInventoryItem_CalculateScoreVfuncIndex = 0x0C;
	inline constexpr std::size_t kTESObjectCONT_ActivateVfuncIndex = 0x37;
	inline constexpr std::size_t kAttachAshPileFunctor_CallOperatorVfuncIndex = 0x01;
	inline constexpr std::size_t kActor_UseAmmoVfuncIndex_SEAE = 0x0D2;
	inline constexpr std::size_t kActor_UseAmmoVfuncIndex_VR = 0x0D4;
}
