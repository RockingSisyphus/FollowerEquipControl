// Shared Detours hook for Actor::RemoveItem with listener and handler dispatch.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <functional>

namespace FEC
{
	class RemoveActorItemHook
	{
	public:
		struct Context
		{
			RE::TESObjectREFR* sourceRefr{ nullptr };
			RE::TESBoundObject* item{ nullptr };
			std::int32_t count{ 0 };
			RE::ITEM_REMOVE_REASON reason{};
			RE::ExtraDataList* extraList{ nullptr };
			RE::TESObjectREFR* moveToRefr{ nullptr };
			const RE::NiPoint3* dropLoc{ nullptr };
			const RE::NiPoint3* rotate{ nullptr };
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
