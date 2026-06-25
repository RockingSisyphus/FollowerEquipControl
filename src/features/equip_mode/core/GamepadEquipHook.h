// Hooks SkyUI ContainerMenu gamepad input for follower left/right-hand equip.
// Installs a Scaleform handleInput hook only while the mod key is held.

#pragma once

namespace RE
{
	class ContainerMenu;
}

namespace FEC::EquipMode::Core::GamepadEquipHook
{
	// Installs at most once per menu instance.
	void TryInstallHooks(RE::ContainerMenu* menu);

	// Removes the C++-backed GFxValue before GFxMovieRoot::dtor_impl.
	void TryUninstallHooks(RE::ContainerMenu* menu);
}
