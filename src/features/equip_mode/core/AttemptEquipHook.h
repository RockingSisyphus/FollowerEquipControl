// Hooks ContainerMenu's AttemptEquip Scaleform call.
// PC uses the engine slot as the hand override; gamepad blocks AttemptEquip while
// the mod key is held so handleInput-driven transfers take precedence.

#pragma once

namespace RE
{
	class ContainerMenu;
}

namespace FEC::EquipMode::Core::AttemptEquipHook
{
	// Installs at most once per menu instance.
	void TryInstallHook(RE::ContainerMenu* menu);

	// Removes the C++-backed GFxValue before GFxMovieRoot::dtor_impl.
	void TryUninstallHook(RE::ContainerMenu* menu);
}
