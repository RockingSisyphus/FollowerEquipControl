// Adds SkyUI ContainerMenu icons for entries restored by OutfitSnapshotRestore.
// Requires SkyUI inventory entry formatting and external SWF icon loading.

#pragma once

namespace FEC
{
	class OutfitSyncIconInjector
	{
	public:
		static void Install();
		static void Uninstall();

		// Call after ContainerMenu equip or unequip actions; same-form swaps can change
		// the resolved xList without changing the raw base-object hash.
		static void NotifyEquipChanged() noexcept;
	};
}
