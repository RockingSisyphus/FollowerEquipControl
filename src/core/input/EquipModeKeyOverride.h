// Overrides SkyUI's hardcoded Player Equip Mode key at runtime by patching GFx root properties on ContainerMenu display.

#pragma once

namespace FEC
{
	class EquipModeKeyOverride
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
