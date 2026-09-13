// Shared ContainerMenu transfer detour with typed context and listener dispatch.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <functional>

namespace FEC
{
	class ContainerMenuTransferHook
	{
	public:
		struct Context
		{
			RE::ContainerMenu* menu{ nullptr };
			RE::TESBoundObject* object{ nullptr };
			std::uint16_t count{ 0 };
			std::uint8_t mode{ 0 };
			// Best-effort capture of the engine-selected destination instance. Set only when the original transfer runs.
			RE::Actor* transferredToActor{ nullptr };
			RE::TESObjectREFR* transferredFromRefr{ nullptr };
			RE::ExtraDataList* transferredXList{ nullptr };
			// Set when the capture observes multiple xLists; transferredXList is cleared so consumers fail closed.
			bool transferredXListNonUnique{ false };
		};

		using ListenerHandle = std::uint64_t;
		using PreListener = std::function<void(const Context&)>;
		using PostListener = std::function<void(const Context&)>;
		// Return true to block the original transfer.
		using Handler = std::function<bool(const Context&)>;

		static void Install();
		static void Uninstall();

		static ListenerHandle AddPreListener(PreListener a_listener);
		static ListenerHandle AddPostListener(PostListener a_listener);
		static ListenerHandle AddHandler(Handler a_handler);

		static bool RemoveListener(ListenerHandle a_handle);
	};
}
