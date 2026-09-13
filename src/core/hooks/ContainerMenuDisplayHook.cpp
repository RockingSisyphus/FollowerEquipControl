#include "ContainerMenuDisplayHook.h"

#include "MemoryUtil.h"
#include "Relocations.h"

#include <mutex>
#include <cstdint>
#include <utility>
#include <vector>

namespace FEC
{
	namespace
	{
		class ContainerMenuPostDisplayHook
		{
		public:
			using ListenerHandle = ContainerMenuDisplayHook::ListenerHandle;
			using ListenerEntry = std::pair<ListenerHandle, ContainerMenuDisplayHook::Listener>;

			class MenuOpenCloseSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
			{
			public:
				static MenuOpenCloseSink* GetSingleton()
				{
					static MenuOpenCloseSink s;
					return std::addressof(s);
				}

				RE::BSEventNotifyControl ProcessEvent(
					const RE::MenuOpenCloseEvent* a_event,
					RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) override
				{
					if (!a_event) {
						return RE::BSEventNotifyControl::kContinue;
					}
					if (a_event->menuName != RE::ContainerMenu::MENU_NAME) {
						return RE::BSEventNotifyControl::kContinue;
					}
					if (a_event->opening) {
						InstallNow();
					}
					return RE::BSEventNotifyControl::kContinue;
				}
			};

			static void Install()
			{
				RequestInstall();
			}

			static void RequestInstall()
			{
				_installRequested = true;

				// Install the vfunc hook as late as possible: when ContainerMenu actually opens.
				auto* ui = RE::UI::GetSingleton();
				if (ui && !_sinkInstalled) {
					ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuOpenCloseSink::GetSingleton());
					_sinkInstalled = true;
				}

				// If the menu is already open when a listener is added, install immediately.
				if (ui) {
					auto menu = ui->GetMenu<RE::ContainerMenu>();
					if (menu) {
						InstallNow();
					}
				}
			}

			static void Uninstall()
			{
				if (!_installed) {
					return;
				}

				REL::Relocation<std::uintptr_t> vtbl{ Relocations::kContainerMenuVtbl.address() };
				if (!vtbl.address()) {
					logger::warn("ContainerMenuPostDisplayHook: vtbl relocation address not found (cannot uninstall)");
					return;
				}

				const auto idx = Relocations::kContainerMenuPostDisplayVfuncIndex;
				const auto thunkAddr = util::unrestricted_cast<std::uintptr_t>(thunk);
				auto* const slot = reinterpret_cast<std::uintptr_t*>(vtbl.address() + (idx * sizeof(std::uintptr_t)));
				if (!slot) {
					logger::warn("ContainerMenuPostDisplayHook: vfunc slot pointer null (cannot uninstall)");
					return;
				}

				const auto current = *slot;
				if (current != thunkAddr) {
					// Another plugin likely overwrote the vfunc after us. Avoid stomping it.
					logger::info(
						"ContainerMenuPostDisplayHook: skipping uninstall; vfunc overwritten (current={:p}, expectedThunk={:p})",
						reinterpret_cast<const void*>(current),
						reinterpret_cast<const void*>(thunkAddr));
					return;
				}

				if (_origAddr != 0) {
					(void)vtbl.write_vfunc(idx, _origAddr);
				}

				{
					std::lock_guard lock(_mutex);
					_preListeners.clear();
					_postListeners.clear();
					_nextHandle = 1;
				}

				_installed = false;
				logger::info("Uninstalled ContainerMenu PostDisplay hook");
			}

			static ListenerHandle AddPreListener(ContainerMenuDisplayHook::Listener a_listener)
			{
				RequestInstall();
				if (!a_listener) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_preListeners.emplace_back(handle, std::move(a_listener));
				return handle;
			}

			static ListenerHandle AddPostListener(ContainerMenuDisplayHook::Listener a_listener)
			{
				RequestInstall();
				if (!a_listener) {
					return 0;
				}

				std::lock_guard lock(_mutex);
				const auto handle = _nextHandle++;
				_postListeners.emplace_back(handle, std::move(a_listener));
				return handle;
			}

			static bool RemoveListener(ListenerHandle a_handle)
			{
				if (a_handle == 0) {
					return false;
				}

				std::lock_guard lock(_mutex);
				for (auto it = _preListeners.begin(); it != _preListeners.end(); ++it) {
					if (it->first == a_handle) {
						_preListeners.erase(it);
						return true;
					}
				}
				for (auto it = _postListeners.begin(); it != _postListeners.end(); ++it) {
					if (it->first == a_handle) {
						_postListeners.erase(it);
						return true;
					}
				}
				return false;
			}

		private:
			static void InstallNow()
			{
				if (_installed || !_installRequested) {
					return;
				}

				REL::Relocation<std::uintptr_t> vtbl{ Relocations::kContainerMenuVtbl.address() };
				if (!vtbl.address()) {
					if (!_vtblMissingLogged) {
						logger::warn("ContainerMenuPostDisplayHook: vtbl relocation address not found");
						_vtblMissingLogged = true;
					}
					return;
				}

				const auto idx = Relocations::kContainerMenuPostDisplayVfuncIndex;
				auto* const slot = reinterpret_cast<std::uintptr_t*>(vtbl.address() + (idx * sizeof(std::uintptr_t)));
				if (!slot || !MemoryUtil::IsReadableMemory(slot, sizeof(std::uintptr_t))) {
					logger::warn(
						"ContainerMenuPostDisplayHook: refusing to install; vfunc slot unreadable (vtbl={:p}, idx={})",
						reinterpret_cast<const void*>(vtbl.address()),
						idx);
					return;
				}
				const auto current = *slot;
				if (!MemoryUtil::IsPlausiblePointer(reinterpret_cast<const void*>(current)) ||
					!MemoryUtil::IsExecutableMemory(reinterpret_cast<const void*>(current))) {
					logger::warn(
						"ContainerMenuPostDisplayHook: refusing to install; unexpected vfunc target (vtbl={:p}, idx={}, current={:p})",
						reinterpret_cast<const void*>(vtbl.address()),
						idx,
						reinterpret_cast<const void*>(current));
					return;
				}

				_origAddr = vtbl.write_vfunc(idx, thunk);
				_func = _origAddr;
				_installed = true;
				logger::info("Installed ContainerMenu PostDisplay hook");
				logger::debug(
					"ContainerMenuPostDisplayHook: signature vtbl={:p} idx={} original={:p} thunk={:p}",
					reinterpret_cast<const void*>(vtbl.address()),
					idx,
					reinterpret_cast<const void*>(_origAddr),
					reinterpret_cast<const void*>(util::unrestricted_cast<std::uintptr_t>(thunk)));
			}

			static void thunk(RE::ContainerMenu* a_this)
			{
				// Copy under lock to avoid holding the mutex while executing listeners.
				// (Listeners may schedule tasks, log, or add additional listeners.)
				thread_local std::vector<ContainerMenuDisplayHook::Listener> preCopy;
				thread_local std::vector<ContainerMenuDisplayHook::Listener> postCopy;
				preCopy.clear();
				postCopy.clear();
				{
					std::lock_guard lock(_mutex);
					preCopy.reserve(_preListeners.size());
					for (auto& entry : _preListeners) {
						preCopy.emplace_back(entry.second);
					}
					postCopy.reserve(_postListeners.size());
					for (auto& entry : _postListeners) {
						postCopy.emplace_back(entry.second);
					}
				}

				for (auto& listener : preCopy) {
					if (listener) {
						listener(a_this);
					}
				}

				// Call original.
				_func(a_this);

				for (auto& listener : postCopy) {
					if (listener) {
						listener(a_this);
					}
				}
			}

			inline static REL::Relocation<decltype(thunk)> _func;
			inline static bool _installed{ false };
			inline static std::uintptr_t _origAddr{ 0 };
			inline static bool _installRequested{ false };
			inline static bool _sinkInstalled{ false };
			inline static bool _vtblMissingLogged{ false };

			inline static std::mutex _mutex;
			inline static std::vector<ListenerEntry> _preListeners;
			inline static std::vector<ListenerEntry> _postListeners;
			inline static ListenerHandle _nextHandle{ 1 };
		};
	}

	void ContainerMenuDisplayHook::Install()
	{
		ContainerMenuPostDisplayHook::Install();
	}

	void ContainerMenuDisplayHook::Uninstall()
	{
		ContainerMenuPostDisplayHook::Uninstall();
	}

	ContainerMenuDisplayHook::ListenerHandle ContainerMenuDisplayHook::AddPostDisplayListener(Listener a_listener)
	{
		return ContainerMenuPostDisplayHook::AddPostListener(std::move(a_listener));
	}

	ContainerMenuDisplayHook::ListenerHandle ContainerMenuDisplayHook::AddPreDisplayListener(Listener a_listener)
	{
		return ContainerMenuPostDisplayHook::AddPreListener(std::move(a_listener));
	}

	bool ContainerMenuDisplayHook::UnregisterPostDisplayListener(ListenerHandle a_handle)
	{
		return ContainerMenuPostDisplayHook::RemoveListener(a_handle);
	}

	bool ContainerMenuDisplayHook::UnregisterListener(ListenerHandle a_handle)
	{
		return UnregisterPostDisplayListener(a_handle);
	}
}
