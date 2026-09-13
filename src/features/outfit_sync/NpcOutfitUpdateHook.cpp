#include "NpcOutfitUpdateHook.h"

#include "ActorScope.h"
#include "ContainerMenuUtil.h"
#include "HandItemRestore.h"
#include "OutfitSnapshotRestore.h"
#include "PluginSettings.h"
#include "Relocations.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <detours/detours.h>

#include <mutex>
#include <unordered_map>

namespace FEC
{
	namespace
	{
		// Pre-1.6.629 AE lacks relocation ID 418622; calling .address() can
		// report_and_fail inside id2offset. Use a byte signature on those builds.
		// Pattern captured from AE 1.6.1170 and shared by known AE builds.

		[[nodiscard]] std::uintptr_t ScanText(
			const std::uint8_t* a_pattern,
			const bool*         a_mask,
			std::size_t         a_len) noexcept
		{
			const auto text = REL::Module::get().segment(REL::Segment::Name::textx);
			const auto* start = reinterpret_cast<const std::uint8_t*>(text.address());
			const auto  size  = text.size();

			if (size < a_len) {
				return 0;
			}

			for (std::size_t i = 0, end = size - a_len; i <= end; ++i) {
				bool match = true;
				for (std::size_t j = 0; j < a_len; ++j) {
					if (a_mask[j] && start[i + j] != a_pattern[j]) {
						match = false;
						break;
					}
				}
				if (match) {
					return text.address() + i;
				}
			}
			return 0;
		}

		[[nodiscard]] std::uintptr_t ScanForUpdateNPCOutfit() noexcept
		{
			// First 20 bytes of the AE UpdateNPCOutfit prologue.
			// 48 8B C4 44 88 48 20 48 89 50 10 48 89 48 08 55 57 41 54 48
			static constexpr std::uint8_t kPattern[] = {
				0x48, 0x8B, 0xC4, 0x44, 0x88, 0x48, 0x20, 0x48, 0x89, 0x50,
				0x10, 0x48, 0x89, 0x48, 0x08, 0x55, 0x57, 0x41, 0x54, 0x48
			};
			static constexpr bool kMask[] = {
				true, true, true, true, true, true, true, true, true, true,
				true, true, true, true, true, true, true, true, true, true
			};
			return ScanText(kPattern, kMask, sizeof(kPattern));
		}

		class UpdateNpcOutfitHook
		{
		public:
			static void Install()
			{
				if (_installed) {
					return;
				}

				std::uintptr_t addr = 0;

				// Avoid relocation ID 418622 on pre-1.6.629 AE; it is missing there.
				if (REL::Module::IsAE()) {
					const auto ver = REL::Module::get().version();
					if (ver.patch() < 629) {
						addr = ScanForUpdateNPCOutfit();
						if (!addr) {
							logger::warn(
								"NpcOutfitUpdateHook: pattern scan failed on "
								"pre-1.6.629 AE ({}) — hook not installed",
								ver.string());
							return;
						}
						logger::info(
							"NpcOutfitUpdateHook: found UpdateNPCOutfit via "
							"pattern scan at {:012X} (pre-1.6.629 AE {})",
							addr, ver.string());
					}
				}

				if (!addr) {
					addr = Relocations::kUpdateNPCOutfit.address();
					if (!addr) {
						if (!_missingAddrLogged) {
							logger::warn("NpcOutfitUpdateHook: UpdateNPCOutfit address not found");
							_missingAddrLogged = true;
						}
						return;
					}
				}

				_func = reinterpret_cast<decltype(_func)>(addr);

				DetourTransactionBegin();
				DetourUpdateThread(GetCurrentThread());
				if (const auto attachErr = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); attachErr != NO_ERROR) {
					DetourTransactionAbort();
					logger::error("NpcOutfitUpdateHook: failed to attach detour (err={})", attachErr);
					return;
				}
				if (DetourTransactionCommit() != NO_ERROR) {
					logger::error("NpcOutfitUpdateHook: failed to install detour");
					return;
				}

				_installed = true;
				logger::info("NpcOutfitUpdateHook: installed UpdateNPCOutfit detour");
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
					logger::warn("NpcOutfitUpdateHook: failed to detach detour (err={})", detachErr);
					return;
				}
				if (const auto commitErr = DetourTransactionCommit(); commitErr != NO_ERROR) {
					logger::warn("NpcOutfitUpdateHook: failed to commit detour removal (err={})", commitErr);
					return;
				}
				_installed = false;
			}

		private:
			static void thunk(RE::TESNPC* a_npc, RE::Actor* a_actor, std::int64_t a_unk1, bool a_checkDead, int a_unk2, char a_unk3)
			{
				if (a_actor && ActorScope::IsAffectedFollower(a_actor)) {
					const RE::FormID actorID = a_actor->GetFormID();
					const RE::FormID curOutfit = (a_npc && a_npc->defaultOutfit) ?
						a_npc->defaultOutfit->GetFormID() : RE::FormID{ 0 };

					// Trace unk1 to observe transient AI-package outfit pointers.
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace(
							"NpcOutfitUpdateHook: thunk actor={:08X} ({}) defaultOutfit={:08X} unk1={:016X} checkDead={} unk2={} unk3={}",
							actorID,
							a_actor->GetName(),
							curOutfit,
							static_cast<std::uint64_t>(a_unk1),
							a_checkDead,
							a_unk2,
							static_cast<int>(a_unk3));
					}

					// Outfit FormID changes indicate an external system assigned a new BGSOutfit.
					bool outfitChanged = false;
					{
						std::lock_guard lock(_lastSeenMutex);
						auto it = _lastSeenOutfit.find(actorID);
						if (it == _lastSeenOutfit.end()) {
							// First sight: record but do not treat as a change.
							_lastSeenOutfit.emplace(actorID, curOutfit);
						} else if (it->second != curOutfit) {
							outfitChanged = true;
							it->second = curOutfit;
						}
					}

					const auto& cfg = PluginSettings::Get().outfitSync;
					if (!cfg.enableUpdateNpcOutfitSuppression) {
						_func(a_npc, a_actor, a_unk1, a_checkDead, a_unk2, a_unk3);
						HandItemRestore::RequestReconcileAfterMenuClose(a_actor);
						return;
					}

					if (cfg.allowOutfitChanges && cfg.enableOutfitSnapshotRestore) {
						// Actors with no defaultOutfit, such as Inigo, use AI-package outfit overrides.
						// Allow vanilla to apply the package outfit, then reseed the snapshot from worn state.
						const bool hasNoDefaultOutfit = (curOutfit == 0);

						if (outfitChanged || hasNoDefaultOutfit) {
							if (spdlog::should_log(spdlog::level::debug)) {
								logger::debug(
									"NpcOutfitUpdateHook: allowing vanilla UpdateNPCOutfit for {:08X} ({}) — outfitChanged={} hasNoDefaultOutfit={} (curOutfit={:08X})",
									actorID, a_actor->GetName(), outfitChanged, hasNoDefaultOutfit, curOutfit);
							}
							_func(a_npc, a_actor, a_unk1, a_checkDead, a_unk2, a_unk3);
							OutfitSnapshotRestore::OnOutfitFormChanged(a_actor);
							HandItemRestore::RequestReconcileAfterMenuClose(a_actor);
							return;
						}
					}

					if (spdlog::should_log(spdlog::level::debug)) {
						logger::debug(
							"NpcOutfitUpdateHook: suppressing vanilla UpdateNPCOutfit for {:08X} ({})",
							actorID,
							a_actor->GetName());
					}
					OutfitSnapshotRestore::OnUpdateNpcOutfitSuppressed(a_actor);
					HandItemRestore::RequestReconcileAfterMenuClose(a_actor);
					return;
				}

				return _func(a_npc, a_actor, a_unk1, a_checkDead, a_unk2, a_unk3);
			}

			inline static bool _installed{ false };
			inline static bool _missingAddrLogged{ false };
			inline static decltype(&thunk) _func{ nullptr };
			inline static std::unordered_map<RE::FormID, RE::FormID> _lastSeenOutfit{};
			inline static std::mutex _lastSeenMutex{};
		};
	}

	void NpcOutfitUpdateHook::Install()
	{
		const auto& settings = PluginSettings::Get();
		const bool needsOutfitSync = settings.outfitSync.enableUpdateNpcOutfitSuppression;
		const bool needsHandItemRestore = settings.autoEquipBlocking.enableNonCombatEquipBlocker &&
			settings.autoEquipBlocking.enableHandItemRestore;

		if (!needsOutfitSync && !needsHandItemRestore) {
			Uninstall();
			return;
		}

		UpdateNpcOutfitHook::Install();
	}

	void NpcOutfitUpdateHook::Uninstall()
	{
		UpdateNpcOutfitHook::Uninstall();
	}
}
