// ContainerMenu helpers for target resolution, close listeners, and refresh scheduling.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <functional>

namespace FEC
{
	namespace ContainerMenuUtil
	{
		using ListenerHandle = std::uint64_t;

		// Installs the ContainerMenu open/close watcher.
		void InstallMenuOpenCloseWatcher();
		void UninstallMenuOpenCloseWatcher();

		// Returns true while ContainerMenu is tearing down after a close event.
		bool IsContainerMenuClosing();

		// Registers a callback for ContainerMenu close and returns a removable handle.
		ListenerHandle AddOnContainerMenuCloseListener(std::function<void()> a_listener);
		bool RemoveOnContainerMenuCloseListener(ListenerHandle a_handle);

		// Returns true if ContainerMenu is currently open.
		bool IsContainerMenuOpen();

		// Returns the active ContainerMenu, if any.
		RE::GPtr<RE::ContainerMenu> GetOpenContainerMenu();

		// Validates the resolved reference is actually an Actor before returning it.
		// Menu target handles can point to non-Actor refs, and Actor-only calls on the wrong type can crash.
		RE::NiPointer<RE::Actor> ResolveActorHandle(RE::RefHandle a_handle);

		// Returns the NPC target for an open ContainerMenu in NPC mode, or nullptr.
		RE::NiPointer<RE::Actor> GetNpcTarget(RE::ContainerMenu* a_menu);

		// Returns the target if it is eligible for mod features.
		RE::NiPointer<RE::Actor> GetAffectedTarget(RE::ContainerMenu* a_menu);

		// Queues a next-frame refresh if the menu is still open and still targeting that actor.
		void QueueRefreshForActor(RE::FormID a_actorID);

		// Queues a next-frame refresh of the open ContainerMenu.
		void QueueRefreshForOpenContainerMenu();

		// Returns the ItemList for the given ContainerMenu, or nullptr.
		// Valid only on the game thread during the current frame.
		[[nodiscard]] RE::ItemList* GetItemList(RE::ContainerMenu* a_menu);

		// Refreshes the actor's 3D and schedules a menu item-list refresh.
		void Refresh3DAndMenu(RE::Actor* a_actor);
	}
}
