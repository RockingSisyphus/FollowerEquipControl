// Hooks Actor::AddObjectToContainer with Detours instead of patching the shared vtable.
// This avoids shared-vtable conflicts with other SKSE plugins.

#include "AddObjectToContainerHook.h"

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
		class AddObjectToContainerVfuncHook
		{
		public:
			using ListenerHandle = AddObjectToContainerHook::ListenerHandle;
			using Context = AddObjectToContainerHook::Context;

			static void Install()
			{
				if (_installed) {
					return;
				}

				const auto vtbl = REL::Relocation<std::uintptr_t>(RE::VTABLE_Actor[0]);
				const auto* vptr = reinterpret_cast<const std::uintptr_t*>(vtbl.address());
				const auto funcAddr = vptr[Relocations::kTESObjectREFR_AddObjectToContainerVfuncIndex];
				if (!funcAddr) {
					logger::error("AddObjectToContainerHook: Actor::AddObjectToContainer vtable entry is null");
					return;
				}

				_func = reinterpret_cast<decltype(_func)>(funcAddr);

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());
				if (const auto err = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); err != NO_ERROR) {
					DetourTransactionAbort();
					logger::error("AddObjectToContainerHook: failed to attach detour (err={})", err);
					return;
				}
				if (const auto err = DetourTransactionCommit(); err != NO_ERROR) {
					logger::error("AddObjectToContainerHook: failed to commit detour (err={})", err);
					return;
				}

				_installed = true;
				logger::info("AddObjectToContainerHook: installed (Detours trampoline on Actor::AddObjectToContainer)");
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
					logger::warn("AddObjectToContainerHook: failed to detach detour (err={})", err);
					return;
				}
				if (const auto err = DetourTransactionCommit(); err != NO_ERROR) {
					logger::warn("AddObjectToContainerHook: failed to commit detour removal (err={})", err);
					return;
				}

				_installed = false;
				logger::info("AddObjectToContainerHook: uninstalled");
			}

			static ListenerHandle AddPre(AddObjectToContainerHook::PreListener a_listener)
			{
				if (!a_listener) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_pre.emplace_back(handle, std::move(a_listener));
				return handle;
			}

			static ListenerHandle AddPost(AddObjectToContainerHook::PostListener a_listener)
			{
				if (!a_listener) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_post.emplace_back(handle, std::move(a_listener));
				return handle;
			}

			static ListenerHandle AddHandler(AddObjectToContainerHook::Handler a_handler)
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

			static void thunk(RE::Actor* a_this, RE::TESBoundObject* a_object, RE::ExtraDataList* a_extraList, std::int32_t a_count, RE::TESObjectREFR* a_fromRefr)
			{
				if (a_this && a_object && spdlog::should_log(spdlog::level::trace)) {
					const char* srcType = "NULL";
					if (a_fromRefr) {
						if (a_fromRefr->As<RE::Actor>()) {
							auto* srcActor = a_fromRefr->As<RE::Actor>();
							srcType = srcActor->IsDead() ? "corpse" : "living-actor";
						} else if (a_fromRefr->GetBaseObject() && a_fromRefr->GetBaseObject()->As<RE::TESObjectCONT>()) {
							srcType = "container";
						} else {
							srcType = "other-ref";
						}
					}
					logger::trace(
						"AddObjectToContainerHook: thunk actor={:08X} ({}) obj={:08X} ({}) count={} from={:08X} ({}) srcType={}",
						a_this->GetFormID(),
						a_this->GetName(),
						a_object->GetFormID(),
						a_object->GetName(),
						a_count,
						a_fromRefr ? a_fromRefr->GetFormID() : 0,
						a_fromRefr ? a_fromRefr->GetName() : "NULL",
						srcType);
				}

				if (!a_this || !a_object) {
					_func(a_this, a_object, a_extraList, a_count, a_fromRefr);
					return;
				}

				if (_handlers.empty() && _pre.empty() && _post.empty()) {
					_func(a_this, a_object, a_extraList, a_count, a_fromRefr);
					return;
				}

				Context ctx{ a_this, a_object, a_extraList, a_count, a_fromRefr };

				{
					std::lock_guard lock(_mutex);
					for (auto& [handle, handler] : _handlers) {
						if (handler && handler(ctx)) {
							if (spdlog::should_log(spdlog::level::trace)) {
								logger::trace(
								    "AddObjectToContainerHook: BLOCKED actor={:08X} obj={:08X}",
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

				_func(a_this, a_object, a_extraList, a_count, a_fromRefr);

				if (!_post.empty()) {
					std::lock_guard lock(_mutex);
					for (auto& [handle, listener] : _post) {
						if (listener) listener(ctx);
					}
				}
			}

		private:
			using PreEntry = std::pair<ListenerHandle, AddObjectToContainerHook::PreListener>;
			using PostEntry = std::pair<ListenerHandle, AddObjectToContainerHook::PostListener>;
			using HandlerEntry = std::pair<ListenerHandle, AddObjectToContainerHook::Handler>;

			inline static decltype(&thunk) _func{ nullptr };
			inline static bool _installed{ false };

			inline static std::recursive_mutex _mutex;
			inline static ListenerHandle _nextHandle{ 1 };
			inline static std::vector<PreEntry> _pre;
			inline static std::vector<PostEntry> _post;
			inline static std::vector<HandlerEntry> _handlers;
		};
	}

	void AddObjectToContainerHook::Install()
	{
		AddObjectToContainerVfuncHook::Install();
	}

	void AddObjectToContainerHook::Uninstall()
	{
		AddObjectToContainerVfuncHook::Uninstall();
	}

	AddObjectToContainerHook::ListenerHandle AddObjectToContainerHook::AddPreListener(PreListener a_listener)
	{
		Install();
		return AddObjectToContainerVfuncHook::AddPre(std::move(a_listener));
	}

	AddObjectToContainerHook::ListenerHandle AddObjectToContainerHook::AddPostListener(PostListener a_listener)
	{
		Install();
		return AddObjectToContainerVfuncHook::AddPost(std::move(a_listener));
	}

	AddObjectToContainerHook::ListenerHandle AddObjectToContainerHook::AddHandler(Handler a_handler)
	{
		Install();
		return AddObjectToContainerVfuncHook::AddHandler(std::move(a_handler));
	}

	bool AddObjectToContainerHook::RemoveListener(ListenerHandle a_handle)
	{
		return AddObjectToContainerVfuncHook::Remove(a_handle);
	}
}
