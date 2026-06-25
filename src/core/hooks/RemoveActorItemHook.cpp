// Detours hook for Actor::RemoveItem.
// Patches the function body instead of shared vtable entries, avoiding conflicts with other SKSE plugins.

#include "RemoveActorItemHook.h"

#include "ActorScope.h"
#include "Logging.h"
#include "Relocations.h"

#include <Windows.h>
#include <detours/detours.h>
#include <mutex>
#include <utility>
#include <vector>

#include <RE/I/InventoryChanges.h>
#include <RE/I/InventoryEntryData.h>

// Windows headers can define GetObject as GetObjectW/A, which breaks CommonLibSSE member calls.
#ifdef GetObject
#	undef GetObject
#endif

namespace FEC
{
	namespace
	{
		class RemoveActorItemVfuncHook
		{
		public:
			using ListenerHandle = RemoveActorItemHook::ListenerHandle;
			using Context = RemoveActorItemHook::Context;

			static void Install()
			{
				if (_installed) {
					return;
				}

				// Read Actor's vtable slot only to find the engine function address.
				// Detours patches the function body; the vtable entry itself is not modified.
				const auto vtbl = REL::Relocation<std::uintptr_t>(RE::VTABLE_Actor[0]);
				const auto* vptr = reinterpret_cast<const std::uintptr_t*>(vtbl.address());
				const auto funcAddr = vptr[Relocations::kTESObjectREFR_RemoveItemVfuncIndex];
				if (!funcAddr) {
					logger::error("RemoveActorItemHook: Actor::RemoveItem vtable entry is null");
					return;
				}

				_func = reinterpret_cast<decltype(_func)>(funcAddr);

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());
				if (const auto err = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); err != NO_ERROR) {
					DetourTransactionAbort();
					logger::error("RemoveActorItemHook: failed to attach detour (err={})", err);
					return;
				}
				if (const auto err = DetourTransactionCommit(); err != NO_ERROR) {
					logger::error("RemoveActorItemHook: failed to commit detour (err={})", err);
					return;
				}

				_installed = true;
				logger::info("RemoveActorItemHook: installed (Detours trampoline on Actor::RemoveItem)");
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
					logger::warn("RemoveActorItemHook: failed to detach detour (err={})", err);
					return;
				}
				if (const auto err = DetourTransactionCommit(); err != NO_ERROR) {
					logger::warn("RemoveActorItemHook: failed to commit detour removal (err={})", err);
					return;
				}

				_installed = false;
				logger::info("RemoveActorItemHook: uninstalled");
			}

			static ListenerHandle AddPre(RemoveActorItemHook::PreListener a_listener)
			{
				if (!a_listener) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_pre.emplace_back(handle, std::move(a_listener));
				return handle;
			}

			static ListenerHandle AddPost(RemoveActorItemHook::PostListener a_listener)
			{
				if (!a_listener) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_post.emplace_back(handle, std::move(a_listener));
				return handle;
			}

			static ListenerHandle AddHandler(RemoveActorItemHook::Handler a_handler)
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

			// Actor::RemoveItem returns a struct through MSVC x64's hidden return buffer.
			// The signature must keep a_retBuf in the second parameter slot; otherwise
			// every following argument is decoded from the wrong register.
			static RE::ObjectRefHandle* thunk(
				RE::TESObjectREFR* a_this,
				RE::ObjectRefHandle* a_retBuf,
				RE::TESBoundObject* a_item,
				std::int32_t a_count,
				RE::ITEM_REMOVE_REASON a_reason,
				RE::ExtraDataList* a_extraList,
				RE::TESObjectREFR* a_moveToRef,
				const RE::NiPoint3* a_dropLoc,
				const RE::NiPoint3* a_rotate)
			{
				// Trace scoped transfer calls and validate the xList pointer before vanilla uses it.
				const bool inScope = [&]() -> bool {
					if (!a_this) return false;
					if (auto* srcActor = a_this->As<RE::Actor>();
						srcActor && ActorScope::IsAffectedFollower(srcActor)) {
						return true;
					}
					if (a_moveToRef) {
						if (auto* dstActor = a_moveToRef->As<RE::Actor>();
							dstActor && ActorScope::IsAffectedFollower(dstActor)) {
							return true;
						}
					}
					return false;
				}();
				if (inScope && a_this && a_item && spdlog::should_log(spdlog::level::trace)) {
					logger::trace(
						"RemoveActorItemHook: thunk source={:08X} ({}) item={:08X} ({}) count={} reason={} moveTo={:08X} ({}) extraList={:p} handlers={}",
						a_this->GetFormID(),
						a_this->GetName(),
						a_item->GetFormID(),
						a_item->GetName(),
						static_cast<std::int32_t>(a_count),
						static_cast<std::uint32_t>(a_reason),
						a_moveToRef ? a_moveToRef->GetFormID() : 0U,
						a_moveToRef ? a_moveToRef->GetName() : "",
						static_cast<void*>(a_extraList),
						_handlers.size());
					// Only compare the pointer against current inventory lists; do not dereference it.
					if (a_extraList) {
						if (auto* srcActor = a_this->As<RE::Actor>()) {
							auto* changes = srcActor->GetInventoryChanges();
							bool foundItem = false;
							bool foundXList = false;
							if (changes && changes->entryList) {
								for (auto* entry : *changes->entryList) {
									if (!entry || entry->GetObject() != a_item) continue;
									foundItem = true;
									if (entry->extraLists) {
										for (auto* x : *entry->extraLists) {
											if (x == a_extraList) { foundXList = true; break; }
										}
									}
									break;
								}
							}
							// Treat the xList as stale only when the item is still present but
							// this exact pointer is no longer attached to that item.
							// If the item cannot be found, leave the call untouched.
							if (foundItem && !foundXList) {
								logger::warn(
									"RemoveActorItemHook: STALE XLIST source={:08X} ({}) item={:08X} ({}) stale={:p} — passing nullptr (xList freed by upstream hook, item still in inventory)",
									a_this->GetFormID(), a_this->GetName(),
									a_item->GetFormID(), a_item->GetName(),
									static_cast<void*>(a_extraList));
								a_extraList = nullptr;
							}
						}
					}
				}

				if (!a_this || !a_item) {
					return _func(a_this, a_retBuf, a_item, a_count, a_reason, a_extraList, a_moveToRef, a_dropLoc, a_rotate);
				}

				if (_handlers.empty() && _pre.empty() && _post.empty()) {
					return _func(a_this, a_retBuf, a_item, a_count, a_reason, a_extraList, a_moveToRef, a_dropLoc, a_rotate);
				}

				Context ctx{ a_this, a_item, a_count, a_reason, a_extraList, a_moveToRef, a_dropLoc, a_rotate };

				// Handlers/listeners are expected to be registered at kDataLoaded,
				// keeping the vectors stable during iteration.
				{
					std::lock_guard lock(_mutex);
					for (auto& [handle, handler] : _handlers) {
						if (handler && handler(ctx)) {
							if (spdlog::should_log(spdlog::level::trace)) {
								logger::trace(
								    "RemoveActorItemHook: BLOCKED source={:08X} item={:08X} moveTo={:08X}",
								    a_this->GetFormID(),
								    a_item->GetFormID(),
								    a_moveToRef ? a_moveToRef->GetFormID() : 0);
								}
							*a_retBuf = RE::ObjectRefHandle{};
							return a_retBuf;
						}
					}
					for (auto& [handle, listener] : _pre) {
						if (listener) listener(ctx);
					}
				}

				auto* result = _func(a_this, a_retBuf, a_item, a_count, a_reason, a_extraList, a_moveToRef, a_dropLoc, a_rotate);

				if (!_post.empty()) {
					std::lock_guard lock(_mutex);
					for (auto& [handle, listener] : _post) {
						if (listener) listener(ctx);
					}
				}

				return result;
			}

		private:
			using PreEntry = std::pair<ListenerHandle, RemoveActorItemHook::PreListener>;
			using PostEntry = std::pair<ListenerHandle, RemoveActorItemHook::PostListener>;
			using HandlerEntry = std::pair<ListenerHandle, RemoveActorItemHook::Handler>;

			inline static decltype(&thunk) _func{ nullptr };
			inline static bool _installed{ false };

			inline static std::recursive_mutex _mutex;
			inline static ListenerHandle _nextHandle{ 1 };
			inline static std::vector<PreEntry> _pre;
			inline static std::vector<PostEntry> _post;
			inline static std::vector<HandlerEntry> _handlers;
		};
	}

	void RemoveActorItemHook::Install()
	{
		RemoveActorItemVfuncHook::Install();
	}

	void RemoveActorItemHook::Uninstall()
	{
		RemoveActorItemVfuncHook::Uninstall();
	}

	RemoveActorItemHook::ListenerHandle RemoveActorItemHook::AddPreListener(PreListener a_listener)
	{
		Install();
		return RemoveActorItemVfuncHook::AddPre(std::move(a_listener));
	}

	RemoveActorItemHook::ListenerHandle RemoveActorItemHook::AddPostListener(PostListener a_listener)
	{
		Install();
		return RemoveActorItemVfuncHook::AddPost(std::move(a_listener));
	}

	RemoveActorItemHook::ListenerHandle RemoveActorItemHook::AddHandler(Handler a_handler)
	{
		Install();
		return RemoveActorItemVfuncHook::AddHandler(std::move(a_handler));
	}

	bool RemoveActorItemHook::RemoveListener(ListenerHandle a_handle)
	{
		return RemoveActorItemVfuncHook::Remove(a_handle);
	}
}
