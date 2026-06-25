// Detours trampoline hook for `Actor::AddObjectToContainer` on Actor, Character, and PlayerCharacter.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <functional>

namespace FEC
{
	class AddObjectToContainerHook
	{
	public:
		struct Context
		{
			RE::Actor* toActor{ nullptr };
			RE::TESBoundObject* object{ nullptr };
			RE::ExtraDataList* extraList{ nullptr };
			std::int32_t count{ 0 };
			RE::TESObjectREFR* fromRefr{ nullptr };
		};

		using ListenerHandle = std::uint64_t;
		using PreListener = std::function<void(const Context&)>;
		using PostListener = std::function<void(const Context&)>;
		// Returns true to block the original call.
		using Handler = std::function<bool(const Context&)>;

		static void Install();
		static void Uninstall();

		static ListenerHandle AddPreListener(PreListener a_listener);
		static ListenerHandle AddPostListener(PostListener a_listener);
		static ListenerHandle AddHandler(Handler a_handler);

		static bool RemoveListener(ListenerHandle a_handle);
	};
}
