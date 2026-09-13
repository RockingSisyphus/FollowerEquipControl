#include "OutfitItemBlocker.h"

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
		class InitOutfitItemsHook
		{
		public:
			static void Install()
			{
				if (_installed) {
					return;
				}

				const auto addr = Relocations::kInitOutfitItems.address();
				if (!addr) {
					if (!_missingAddrLogged) {
						logger::warn("OutfitItemBlocker: InitOutfitItems address not found");
						_missingAddrLogged = true;
					}
					return;
				}

				_func = reinterpret_cast<decltype(_func)>(addr);

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());
				if (const auto attachErr = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); attachErr != NO_ERROR) {
					DetourTransactionAbort();
					logger::error("OutfitItemBlocker: failed to attach detour (err={})", attachErr);
					return;
				}
				if (DetourTransactionCommit() != NO_ERROR) {
					logger::error("OutfitItemBlocker: failed to install detour");
					return;
				}

				_installed = true;
				logger::info("OutfitItemBlocker: installed InitOutfitItems detour");
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
					logger::warn("OutfitItemBlocker: failed to detach detour (err={})", detachErr);
					return;
				}
				if (const auto commitErr = DetourTransactionCommit(); commitErr != NO_ERROR) {
					logger::warn("OutfitItemBlocker: failed to commit detour removal (err={})", commitErr);
					return;
				}

				_installed = false;
				logger::info("OutfitItemBlocker: uninstalled InitOutfitItems detour");
			}

		private:
			static void thunk(RE::InventoryChanges* a_this, RE::BGSOutfit* a_outfit, std::uint16_t a_npcLevel)
			{
				if (a_this && a_this->owner) {
					auto* actor = a_this->owner->As<RE::Actor>();
					if (actor) {
						const bool isFollower = !actor->IsPlayerRef() && ActorScope::IsAffectedFollower(actor);
						if (isFollower) {
							if (spdlog::should_log(spdlog::level::trace)) {
								const auto id = actor->GetFormID();
								bool firstTime = false;
								{
									std::lock_guard lock(_logMutex);
									firstTime = _loggedActors.insert(id).second;
								}
								if (firstTime) {
									logger::trace(
										"OutfitItemBlocker: InitOutfitItems BLOCKED for {:08X} ({}) outfit={:08X} level={}",
										id,
										actor->GetName(),
										a_outfit ? a_outfit->GetFormID() : 0,
										a_npcLevel);
								}
							}
							return;
						}
					}
				}

				_func(a_this, a_outfit, a_npcLevel);
			}

			inline static bool _installed{ false };
			inline static bool _missingAddrLogged{ false };
			inline static decltype(&thunk) _func{ nullptr };
			inline static std::mutex _logMutex{};
			inline static std::unordered_set<RE::FormID> _loggedActors{};
		};
	}

	void OutfitItemBlocker::Install()
	{
		if (!PluginSettings::Get().itemInjectionBlocking.enableOutfitItemBlocker) {
			Uninstall();
			return;
		}

		InitOutfitItemsHook::Install();
	}

	void OutfitItemBlocker::Uninstall()
	{
		InitOutfitItemsHook::Uninstall();
	}
}
