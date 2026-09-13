#include "LeveledItemBlocker.h"

#include "ActorScope.h"
#include "PluginSettings.h"
#include "Relocations.h"

#include <mutex>
#include <unordered_set>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>
#include <detours/detours.h>

namespace FEC
{
	namespace
	{
		class InitLeveledItemsHook
		{
		public:
			static void Install()
			{
				if (_installed) {
					return;
				}

				const auto addr = Relocations::kInitLeveledItems.address();
				if (!addr) {
					if (!_missingAddrLogged) {
						logger::warn("LeveledItemBlocker: InitLeveledItems address not found");
						_missingAddrLogged = true;
					}
					return;
				}

				_func = reinterpret_cast<decltype(_func)>(addr);

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());
				if (const auto attachErr = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); attachErr != NO_ERROR) {
					DetourTransactionAbort();
					logger::error("LeveledItemBlocker: failed to attach detour (err={})", attachErr);
					return;
				}
				if (DetourTransactionCommit() != NO_ERROR) {
					logger::error("LeveledItemBlocker: failed to install detour");
					return;
				}

				_installed = true;
				logger::info("LeveledItemBlocker: installed InitLeveledItems detour");
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
					logger::warn("LeveledItemBlocker: failed to detach detour (err={})", detachErr);
					return;
				}
				if (const auto commitErr = DetourTransactionCommit(); commitErr != NO_ERROR) {
					logger::warn("LeveledItemBlocker: failed to commit detour removal (err={})", commitErr);
					return;
				}

				_installed = false;
				logger::info("LeveledItemBlocker: uninstalled InitLeveledItems detour");
			}

		private:
			static void thunk(RE::InventoryChanges* a_this)
			{
				if (a_this && a_this->owner) {
					auto* actor = a_this->owner->As<RE::Actor>();
					if (actor) {
						const bool isFollower = !actor->IsPlayerRef() && ActorScope::IsAffectedFollower(actor);
						if (isFollower) {
							// Log once per actor; the engine can call InitLeveledItems repeatedly here.
							if (spdlog::should_log(spdlog::level::trace)) {
								const auto id = actor->GetFormID();
								bool firstTime = false;
								{
									std::lock_guard lock(_logMutex);
									firstTime = _loggedActors.insert(id).second;
								}
								if (firstTime) {
									logger::trace(
										"LeveledItemBlocker: InitLeveledItems BLOCKED for {:08X} ({})",
										id,
										actor->GetName());
								}
							}
							return;
						}
					}
				}

				_func(a_this);
			}

			inline static bool _installed{ false };
			inline static bool _missingAddrLogged{ false };
			inline static decltype(&thunk) _func{ nullptr };
			inline static std::mutex _logMutex{};
			inline static std::unordered_set<RE::FormID> _loggedActors{};
		};
	}

	void LeveledItemBlocker::Install()
	{
		if (!PluginSettings::Get().itemInjectionBlocking.enableLeveledItemBlocker) {
			Uninstall();
			return;
		}

		InitLeveledItemsHook::Install();
	}

	void LeveledItemBlocker::Uninstall()
	{
		InitLeveledItemsHook::Uninstall();
	}
}
