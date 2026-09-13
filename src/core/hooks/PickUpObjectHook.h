// Shared Detours hook for Actor::PickUpObject with listener and handler dispatch.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <functional>

namespace FEC
{
	class PickUpObjectHook
	{
	public:
		struct Context
		{
			RE::Actor* actor{ nullptr };
			RE::TESObjectREFR* object{ nullptr };
			std::int32_t count{ 0 };
			bool arg3{ false };
			bool playSound{ true };
		};

		using ListenerHandle = std::uint64_t;
		using PreListener = std::function<void(const Context&)>;
		using PostListener = std::function<void(const Context&)>;
		// Return true to block the original call.
		using Handler = std::function<bool(const Context&)>;

		static void Install();
		static void Uninstall();

		static ListenerHandle AddPreListener(PreListener a_listener);
		static ListenerHandle AddPostListener(PostListener a_listener);
		static ListenerHandle AddHandler(Handler a_handler);

		static bool RemoveListener(ListenerHandle a_handle);
	};
}
