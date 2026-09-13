// Shared ContainerMenu PostDisplay hook with ordered listener dispatch.

#pragma once

#include "PCH.h"

#include <functional>

namespace FEC
{
	class ContainerMenuDisplayHook
	{
	public:
		using ListenerHandle = std::uint64_t;
		using Listener = std::function<void(RE::ContainerMenu*)>;

		// Installs the ContainerMenu PostDisplay vfunc hook once.
		static void Install();

		// Best-effort uninstall: only restores the vfunc if it still points to our thunk.
		// Avoids stomping another plugin if it replaced the slot after us.
		static void Uninstall();

		// Runs before the original PostDisplay builds the item list.
		static ListenerHandle AddPreDisplayListener(Listener a_listener);

		// Runs after the original PostDisplay and returns a handle for later unregister.
		static ListenerHandle AddPostDisplayListener(Listener a_listener);

		// Returns true if a listener was removed.
		static bool UnregisterPostDisplayListener(ListenerHandle a_handle);

		// Naming alias for UnregisterPostDisplayListener.
		static bool UnregisterListener(ListenerHandle a_handle);
	};
}
