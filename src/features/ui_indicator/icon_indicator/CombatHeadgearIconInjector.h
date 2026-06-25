// Adds a SkyUI ContainerMenu icon for the combat headgear preference.
// Requires SkyUI inventory entry formatting and external SWF icon loading.

#pragma once

namespace FEC
{
	class CombatHeadgearIconInjector
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
