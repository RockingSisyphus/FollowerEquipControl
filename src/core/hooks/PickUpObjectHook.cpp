// Detours patches the function body instead of shared Actor vtable entries, avoiding vtable conflicts with other SKSE plugins.

#include "PickUpObjectHook.h"

#include "Logging.h"
#include "Relocations.h"

#include <Windows.h>
#include <detours/detours.h>
#include <mutex>
#include <utility>
#include <vector>

namespace FEC
{
	namespace
	{
		class PickUpObjectVfuncHook
		{
		public:
			using ListenerHandle = PickUpObjectHook::ListenerHandle;
			using Context = PickUpObjectHook::Context;

			static void Install()
			{
				if (_installed) {
					return;
				}

				const auto vtbl = REL::Relocation<std::uintptr_t>(RE::VTABLE_Actor[0]);
				const auto* vptr = reinterpret_cast<const std::uintptr_t*>(vtbl.address());
				const auto funcAddr = vptr[Relocations::kActor_PickUpObjectVfuncIndex];
				if (!funcAddr) {
					logger::error("PickUpObjectHook: Actor::PickUpObject vtable entry is null");
					return;
				}

				_func = reinterpret_cast<decltype(_func)>(funcAddr);

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());
				if (const auto err = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); err != NO_ERROR) {
					DetourTransactionAbort();
					logger::error("PickUpObjectHook: failed to attach detour (err={})", err);
					return;
				}
				if (const auto err = DetourTransactionCommit(); err != NO_ERROR) {
					logger::error("PickUpObjectHook: failed to commit detour (err={})", err);
					return;
				}

				_installed = true;
				logger::info("PickUpObjectHook: installed (Detours trampoline on Actor::PickUpObject)");
			}

			static void Uninstall()
			{
				if (!_installed) {
					return;
				}

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());
				if (const auto err = DetourDetach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); err != NO_ERROR) {
					DetourTransactionAbort();
					logger::warn("PickUpObjectHook: failed to detach detour (err={})", err);
					return;
				}
				if (const auto err = DetourTransactionCommit(); err != NO_ERROR) {
					logger::warn("PickUpObjectHook: failed to commit detour removal (err={})", err);
					return;
				}

				_installed = false;
				logger::info("PickUpObjectHook: uninstalled");
			}

			static ListenerHandle AddPre(PickUpObjectHook::PreListener a_listener)
			{
				if (!a_listener) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_pre.emplace_back(handle, std::move(a_listener));
				return handle;
			}

			static ListenerHandle AddPost(PickUpObjectHook::PostListener a_listener)
			{
				if (!a_listener) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_post.emplace_back(handle, std::move(a_listener));
				return handle;
			}

			static ListenerHandle AddHandler(PickUpObjectHook::Handler a_handler)
			{
				if (!a_handler) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_handlers.emplace_back(handle, std::move(a_handler));
				return handle;
			}

			static bool Remove(ListenerHandle a_handle)
			{
				if (a_handle == 0) {
					return false;
				}

				std::lock_guard lock(_mutex);
				for (auto it = _pre.begin(); it != _pre.end(); ++it) {
					if (it->first == a_handle) {
						_pre.erase(it);
						return true;
					}
				}
				for (auto it = _handlers.begin(); it != _handlers.end(); ++it) {
					if (it->first == a_handle) {
						_handlers.erase(it);
						return true;
					}
				}
				for (auto it = _post.begin(); it != _post.end(); ++it) {
					if (it->first == a_handle) {
						_post.erase(it);
						return true;
					}
				}
				return false;
			}

			static void thunk(RE::Actor* a_this, RE::TESObjectREFR* a_object, std::int32_t a_count, bool a_arg3, bool a_playSound)
			{
				if (a_this && a_object && spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"PickUpObjectHook: thunk actor={:08X} ({}) obj={:08X} ({}) count={} arg3={} playSound={}",
						a_this->GetFormID(),
						a_this->GetName(),
						a_object->GetFormID(),
						a_object->GetName(),
						a_count,
						a_arg3,
						a_playSound);
				}

				if (!a_this || !a_object) {
					_func(a_this, a_object, a_count, a_arg3, a_playSound);
					return;
				}

				if (_handlers.empty() && _pre.empty() && _post.empty()) {
					_func(a_this, a_object, a_count, a_arg3, a_playSound);
					return;
				}

				Context ctx{ a_this, a_object, a_count, a_arg3, a_playSound };

				{
					std::lock_guard lock(_mutex);
					for (auto& [handle, handler] : _handlers) {
						if (handler && handler(ctx)) {
							if (spdlog::should_log(spdlog::level::trace)) {
								logger::trace(
								    "PickUpObjectHook: BLOCKED actor={:08X} obj={:08X}",
								    a_this->GetFormID(),
								    a_object->GetFormID());
								}
								return;
						}
					}
					for (auto& [handle, listener] : _pre) {
						if (listener) listener(ctx);
					}
				}

				_func(a_this, a_object, a_count, a_arg3, a_playSound);

				if (!_post.empty()) {
					std::lock_guard lock(_mutex);
					for (auto& [handle, listener] : _post) {
						if (listener) listener(ctx);
					}
				}
			}

		private:
			using PreEntry = std::pair<ListenerHandle, PickUpObjectHook::PreListener>;
			using PostEntry = std::pair<ListenerHandle, PickUpObjectHook::PostListener>;
			using HandlerEntry = std::pair<ListenerHandle, PickUpObjectHook::Handler>;

			inline static decltype(&thunk) _func{ nullptr };
			inline static bool _installed{ false };

			inline static std::recursive_mutex _mutex;
			inline static ListenerHandle _nextHandle{ 1 };
			inline static std::vector<PreEntry> _pre;
			inline static std::vector<PostEntry> _post;
			inline static std::vector<HandlerEntry> _handlers;
		};
	}

	void PickUpObjectHook::Install()
	{
		PickUpObjectVfuncHook::Install();
	}

	void PickUpObjectHook::Uninstall()
	{
		PickUpObjectVfuncHook::Uninstall();
	}

	PickUpObjectHook::ListenerHandle PickUpObjectHook::AddPreListener(PreListener a_listener)
	{
		Install();
		return PickUpObjectVfuncHook::AddPre(std::move(a_listener));
	}

	PickUpObjectHook::ListenerHandle PickUpObjectHook::AddPostListener(PostListener a_listener)
	{
		Install();
		return PickUpObjectVfuncHook::AddPost(std::move(a_listener));
	}

	PickUpObjectHook::ListenerHandle PickUpObjectHook::AddHandler(Handler a_handler)
	{
		Install();
		return PickUpObjectVfuncHook::AddHandler(std::move(a_handler));
	}

	bool PickUpObjectHook::RemoveListener(ListenerHandle a_handle)
	{
		return PickUpObjectVfuncHook::Remove(a_handle);
	}
}
