#include "ContainerMenuTransferHook.h"

#include "AddObjectToContainerHook.h"
#include "ContainerMenuUtil.h"
#include "Relocations.h"

#include <RE/E/ExtraWorn.h>
#include <RE/E/ExtraWornLeft.h>
#include <RE/I/InventoryChanges.h>
#include <RE/I/InventoryEntryData.h>

#include <mutex>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <detours/detours.h>

// Windows headers can define GetObject as a macro and break CommonLibSSE-NG member calls.
#ifdef GetObject
#	undef GetObject
#endif

namespace FEC
{
	namespace
	{
		struct TransferXListCapture
		{
			std::uint32_t depth{ 0 };
			RE::TESBoundObject* expectedObject{ nullptr };
			std::uint16_t expectedCount{ 0 };
			std::uint8_t expectedMode{ 0 };
			RE::Actor* toActor{ nullptr };
			RE::TESObjectREFR* fromRefr{ nullptr };
			RE::ExtraDataList* xList{ nullptr };
			bool nonUnique{ false };
		};

		thread_local TransferXListCapture g_xListCapture;
		AddObjectToContainerHook::ListenerHandle g_addPreHandle{ 0 };

		void EnsureAddObjectObserverInstalled()
		{
			if (g_addPreHandle != 0) {
				return;
			}

			g_addPreHandle = AddObjectToContainerHook::AddPreListener([](const AddObjectToContainerHook::Context& addCtx) {
				if (g_xListCapture.depth == 0) {
					return;
				}
				if (!addCtx.toActor || !addCtx.object) {
					return;
				}
				if (addCtx.object != g_xListCapture.expectedObject) {
					return;
				}
				if (addCtx.count <= 0) {
					return;
				}
				if (static_cast<std::uint16_t>(addCtx.count) != g_xListCapture.expectedCount) {
					// Count transfers can be split internally; avoid capturing the wrong add.
					return;
				}
				if (!addCtx.extraList) {
					return;
				}

				if (!g_xListCapture.xList) {
					g_xListCapture.xList = addCtx.extraList;
					g_xListCapture.toActor = addCtx.toActor;
					g_xListCapture.fromRefr = addCtx.fromRefr;
				} else if (g_xListCapture.xList != addCtx.extraList) {
					g_xListCapture.nonUnique = true;
				}
			});
		}

		void BeginXListCapture(const ContainerMenuTransferHook::Context& ctx)
		{
			if (g_xListCapture.depth == 0) {
				g_xListCapture = {};
				g_xListCapture.expectedObject = ctx.object;
				g_xListCapture.expectedCount = ctx.count;
				g_xListCapture.expectedMode = ctx.mode;
			}
			++g_xListCapture.depth;
		}

		void EndXListCapture()
		{
			if (g_xListCapture.depth == 0) {
				return;
			}
			--g_xListCapture.depth;
			if (g_xListCapture.depth == 0) {
				// Leave captured values available to post-transfer code.
			}
		}

		class AddObjectToContainerDetour
		{
		public:
			using ListenerHandle = ContainerMenuTransferHook::ListenerHandle;
			using Context = ContainerMenuTransferHook::Context;

			static void Install()
			{
				if (_installed) {
					return;
				}

				// Capture the engine-selected instance through AddObjectToContainer observation.
				EnsureAddObjectObserverInstalled();

				const auto addr = Relocations::kContainerMenuAddObjectToContainer.address();
				if (!addr) {
					if (!_missingAddrLogged) {
						logger::warn("ContainerMenuTransferHook: AddObjectToContainer address not found");
						_missingAddrLogged = true;
					}
					return;
				}

				_func = reinterpret_cast<decltype(_func)>(addr);

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());
				if (const auto attachErr = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); attachErr != NO_ERROR) {
					DetourTransactionAbort();
					logger::error("ContainerMenuTransferHook: failed to attach AddObjectToContainer detour (err={})", attachErr);
					return;
				}
				if (DetourTransactionCommit() != NO_ERROR) {
					logger::error("ContainerMenuTransferHook: failed to install AddObjectToContainer detour");
					return;
				}

				_installed = true;
				logger::info("ContainerMenuTransferHook: installed AddObjectToContainer detour");
			}

			static void Uninstall()
			{
				if (!_installed) {
					return;
				}

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());
				if (const auto detachErr = DetourDetach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); detachErr != NO_ERROR) {
					DetourTransactionAbort();
					logger::warn("ContainerMenuTransferHook: failed to detach AddObjectToContainer detour (err={})", detachErr);
					return;
				}
				if (const auto commitErr = DetourTransactionCommit(); commitErr != NO_ERROR) {
					logger::warn("ContainerMenuTransferHook: failed to commit detour removal (err={})", commitErr);
					return;
				}

				_installed = false;
				logger::info("ContainerMenuTransferHook: uninstalled AddObjectToContainer detour");
			}

			static ListenerHandle AddPre(ContainerMenuTransferHook::PreListener a_listener)
			{
				if (!a_listener) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_pre.emplace_back(handle, std::move(a_listener));
				return handle;
			}

			static ListenerHandle AddPost(ContainerMenuTransferHook::PostListener a_listener)
			{
				if (!a_listener) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_post.emplace_back(handle, std::move(a_listener));
				return handle;
			}

			static ListenerHandle AddHandler(ContainerMenuTransferHook::Handler a_handler)
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

		private:
			using PreEntry = std::pair<ListenerHandle, ContainerMenuTransferHook::PreListener>;
			using PostEntry = std::pair<ListenerHandle, ContainerMenuTransferHook::PostListener>;
			using HandlerEntry = std::pair<ListenerHandle, ContainerMenuTransferHook::Handler>;

			static bool thunk(RE::ContainerMenu* a_this, RE::TESBoundObject** a_object, std::uint16_t a_count, std::uint8_t a_mode)
			{
				if (!a_this || !a_object || !*a_object) {
					return _func(a_this, a_object, a_count, a_mode);
				}

				Context ctx{ a_this, *a_object, a_count, a_mode };

				if (spdlog::should_log(spdlog::level::debug)) {
					const char* direction = (a_mode == 0x00) ? "Player->NPC" : (a_mode == 0x01) ? "NPC->Player" : "unknown";
					const auto containerMode = a_this->GetContainerMode();
					auto dbgTarget = ContainerMenuUtil::ResolveActorHandle(a_this->GetTargetRefHandle());
					logger::debug(
						"ContainerMenuTransferHook: transfer obj={:08X} ({}) count={} mode=0x{:02X} ({}) containerMode={} target={:08X} ({}) isDead={}",
						(*a_object)->GetFormID(),
						(*a_object)->GetName(),
						a_count,
						a_mode,
						direction,
						static_cast<std::uint32_t>(containerMode),
						dbgTarget ? dbgTarget->GetFormID() : 0u,
						dbgTarget ? dbgTarget->GetName() : "?",
						dbgTarget ? dbgTarget->IsDead() : false);
				}

				// Copy under lock so callbacks can safely register/unregister.
				thread_local std::vector<ContainerMenuTransferHook::PreListener> preCopy;
				thread_local std::vector<ContainerMenuTransferHook::PostListener> postCopy;
				thread_local std::vector<ContainerMenuTransferHook::Handler> handlerCopy;
				preCopy.clear();
				postCopy.clear();
				handlerCopy.clear();
				{
					std::lock_guard lock(_mutex);
					preCopy.reserve(_pre.size());
					for (auto& entry : _pre) {
						preCopy.emplace_back(entry.second);
					}
					postCopy.reserve(_post.size());
					for (auto& entry : _post) {
						postCopy.emplace_back(entry.second);
					}
					handlerCopy.reserve(_handlers.size());
					for (auto& entry : _handlers) {
						handlerCopy.emplace_back(entry.second);
					}
				}

				for (auto& handler : handlerCopy) {
					if (handler && handler(ctx)) {
						return false;
					}
				}

				// Trace xList state before vanilla transfer without dereferencing UI clone xLists.
				if (spdlog::should_log(spdlog::level::trace)) {
					auto* target = ContainerMenuUtil::ResolveActorHandle(a_this->GetTargetRefHandle()).get();
					if (target && !target->IsPlayerRef()) {
						auto* itemList = a_this->GetRuntimeData().itemList;
						auto* selected = itemList ? itemList->GetSelectedItem() : nullptr;
						auto* uiEntry = selected ? selected->data.objDesc : nullptr;
						if (uiEntry && uiEntry->GetObject() == *a_object) {
							std::uint32_t uiNonNull = 0;
							if (uiEntry->extraLists) {
								for (auto* x : *uiEntry->extraLists) { if (x) ++uiNonNull; }
							}
							logger::trace(
								"ContainerMenuTransferHook: vanilla-path uiEntry obj={:08X} uiXListCount={}",
								(*a_object)->GetFormID(), uiNonNull);
							if (uiEntry->extraLists) {
								std::uint32_t idx = 0;
								for (auto* x : *uiEntry->extraLists) {
									if (x) {
										// UI clone xList pointers can be dangling after baseFallback equip cycles.
										// Log pointer addresses only; never call ExtraDataList methods on them.
										logger::trace(
											"ContainerMenuTransferHook: vanilla-path uiXList[{}]={:p}",
											idx, static_cast<void*>(x));
									}
									++idx;
								}
							}
						}
						auto* changes = target->GetInventoryChanges();
						if (changes && changes->entryList) {
							for (auto* realEntry : *changes->entryList) {
								if (!realEntry || realEntry->GetObject() != *a_object) continue;
								std::uint32_t realNonNull = 0;
								if (realEntry->extraLists) {
									for (auto* x : *realEntry->extraLists) { if (x) ++realNonNull; }
								}
								logger::trace(
									"ContainerMenuTransferHook: vanilla-path realEntry={:p} obj={:08X} realXListCount={} countDelta={}",
									static_cast<void*>(realEntry), (*a_object)->GetFormID(),
									realNonNull, realEntry->countDelta);
								if (realEntry->extraLists) {
									std::uint32_t ridx = 0;
									for (auto* x : *realEntry->extraLists) {
										if (x) {
											logger::trace(
												"ContainerMenuTransferHook: vanilla-path realXList[{}]={:p} wornR={} wornL={}",
												ridx, static_cast<void*>(x),
												x->HasType<RE::ExtraWorn>(),
												x->HasType<RE::ExtraWornLeft>());
										}
										++ridx;
									}
								}
								break;
							}
						}
					}
				}

				// Pre-listeners arm state immediately before the original call.
				for (auto& listener : preCopy) {
					if (listener) {
						listener(ctx);
					}
				}


				BeginXListCapture(ctx);
				const auto result = _func(a_this, a_object, a_count, a_mode);
				EndXListCapture();

				ctx.transferredXListNonUnique = g_xListCapture.nonUnique;
				ctx.transferredXList = g_xListCapture.nonUnique ? nullptr : g_xListCapture.xList;
				ctx.transferredToActor = g_xListCapture.toActor;
				ctx.transferredFromRefr = g_xListCapture.fromRefr;

				if (spdlog::should_log(spdlog::level::trace)) {
					auto* ptTarget = ContainerMenuUtil::ResolveActorHandle(a_this->GetTargetRefHandle()).get();
					if (ptTarget && !ptTarget->IsPlayerRef()) {
						logger::trace(
							"ContainerMenuTransferHook: post-transfer target={:08X} ({}) isDead={}",
							ptTarget->GetFormID(), ptTarget->GetName(), ptTarget->IsDead());
						auto* changes = ptTarget->GetInventoryChanges();
						if (changes && changes->entryList) {
							for (auto* realEntry : *changes->entryList) {
								if (!realEntry || realEntry->GetObject() != ctx.object) continue;
								std::uint32_t postXListCount = 0;
								if (realEntry->extraLists) {
									for (auto* x : *realEntry->extraLists) { if (x) ++postXListCount; }
								}
								logger::trace(
									"ContainerMenuTransferHook: post-transfer realEntry={:p} obj={:08X} countDelta={} xListCount={}",
									static_cast<void*>(realEntry), ctx.object->GetFormID(),
									realEntry->countDelta, postXListCount);
								break;
							}
						}
					}
				}

				for (auto& listener : postCopy) {
					if (listener) {
						listener(ctx);
					}
				}

				return result;
			}

			inline static bool _installed{ false };
			inline static bool _missingAddrLogged{ false };
			inline static decltype(&thunk) _func{ nullptr };

			inline static std::mutex _mutex;
			inline static ListenerHandle _nextHandle{ 1 };
			inline static std::vector<PreEntry> _pre;
			inline static std::vector<PostEntry> _post;
			inline static std::vector<HandlerEntry> _handlers;
		};
	}

	void ContainerMenuTransferHook::Install()
	{
		AddObjectToContainerDetour::Install();
	}

	void ContainerMenuTransferHook::Uninstall()
	{
		AddObjectToContainerDetour::Uninstall();
	}

	ContainerMenuTransferHook::ListenerHandle ContainerMenuTransferHook::AddPreListener(PreListener a_listener)
	{
		Install();
		return AddObjectToContainerDetour::AddPre(std::move(a_listener));
	}

	ContainerMenuTransferHook::ListenerHandle ContainerMenuTransferHook::AddPostListener(PostListener a_listener)
	{
		Install();
		return AddObjectToContainerDetour::AddPost(std::move(a_listener));
	}

	ContainerMenuTransferHook::ListenerHandle ContainerMenuTransferHook::AddHandler(Handler a_handler)
	{
		Install();
		return AddObjectToContainerDetour::AddHandler(std::move(a_handler));
	}

	bool ContainerMenuTransferHook::RemoveListener(ListenerHandle a_handle)
	{
		return AddObjectToContainerDetour::Remove(a_handle);
	}
}
