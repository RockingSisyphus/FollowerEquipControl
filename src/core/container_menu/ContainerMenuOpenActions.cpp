#include "ContainerMenuOpenActions.h"

#include "ActorScope.h"
#include "CombatEquipPreference.h"
#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "HandItemRestore.h"
#include "KnownFollowerState.h"
#include "OutfitSnapshotRestore.h"

namespace FEC
{
	namespace
	{
		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_listenerHandle{ 0 };
		ContainerMenuUtil::ListenerHandle g_menuCloseHandle{ 0 };

		/// Tracks the actor seeded this menu session to prevent re-seeding after an explicit clear.
		RE::FormID g_seededActorId{ 0 };

		/// Tracks the actor with an outfit-apply SKSE task already queued this menu session.
		/// Prevents AddTask from being called on every ContainerMenu::Display frame.
		RE::FormID g_outfitTaskQueuedForActor{ 0 };

		/// Tracks the actor already enrolled with AddKnownFollower this menu session.
		/// Prevents AddKnownFollower from being called on every ContainerMenu::Display frame.
		RE::FormID g_enrolledActorId{ 0 };

		/// Tracks the actor already evaluated by the admission guard this menu session.
		/// Prevents repeated ShouldAdmitForContainerMenu lookups on every Display frame.
		RE::FormID g_processedActorId{ 0 };
		bool g_processedResult{ false };
	}

	void ContainerMenuOpenActions::Install()
	{
		if (g_installed) {
			return;
		}

		ContainerMenuDisplayHook::Install();
		ContainerMenuUtil::InstallMenuOpenCloseWatcher();

		g_listenerHandle = ContainerMenuDisplayHook::AddPreDisplayListener([](RE::ContainerMenu* a_menu) {
			if (!a_menu) {
				return;
			}

			const auto containerMode = a_menu->GetContainerMode();

			// Resolve the target actor.
			// NPC mode uses GetNpcTarget.
			// Other modes resolve the target handle directly and only accept ActorCharacter refs.
			RE::NiPointer<RE::Actor> target;
			if (containerMode == RE::ContainerMenu::ContainerMode::kNPCMode) {
				target = ContainerMenuUtil::GetNpcTarget(a_menu);
			} else {
				// Non-NPC modes can resolve to containers, not actors.
				// Actor::LookupByHandle blindly casts, so only accept ActorCharacter refs here to avoid AVs.
				RE::NiPointer<RE::TESObjectREFR> refr;
				RE::LookupReferenceByHandle(a_menu->GetTargetRefHandle(), refr);
				if (refr && refr->GetFormType() == RE::FormType::ActorCharacter) {
					target.reset(static_cast<RE::Actor*>(refr.get()));
				}
			}

			if (!target || target->IsPlayerRef()) {
				return;
			}

			// Cache the admission result for this menu session.
			// Dead actors are cached too so the trace log fires once per session instead of every Display call.
			const auto targetID = target->GetFormID();
			if (target->IsDead()) {
				if (g_processedActorId != targetID) {
					g_processedActorId = targetID;
					g_processedResult = false;
					if (spdlog::should_log(spdlog::level::trace)) {
						logger::trace("ContainerMenuOpenActions: skipping dead actor {:08X} ({}) containerMode={}",
							targetID, target->GetName(), static_cast<int>(containerMode));
					}
				}
				return;
			}
			if (g_processedActorId == targetID) {
				if (!g_processedResult) {
					return;
				}
			} else {
				g_processedActorId = targetID;
				g_processedResult = ActorScope::ShouldAdmitForContainerMenu(target.get(), containerMode);
				if (spdlog::should_log(spdlog::level::debug)) {
					logger::debug("ContainerMenuOpenActions: target={:08X} ({}) admitted={} containerMode={}",
						targetID, target->GetName(), g_processedResult, static_cast<int>(containerMode));
				}
				if (!g_processedResult) {
					return;
				}
			}

			// Enroll the actor once per menu session so AddKnownFollower is not called every Display frame.
			if (g_enrolledActorId != targetID) {
				g_enrolledActorId = targetID;
				(void)KnownFollowerState::AddKnownFollower(target.get());
			}

			// Seed combat preferences once per menu session so an explicit user clear stays cleared.
			// If no melee preference exists yet, seed the currently equipped weapons to avoid a no-preference state forcing unarmed combat.
			const auto actorID = target->GetFormID();
			if (g_seededActorId != actorID) {
				g_seededActorId = actorID;
				CombatEquipPreference::SeedFromEquippedIfEmpty(target.get());
			}

			// Defer baseline capture to the SKSE task so OutfitItemSanitizer can promote worn outfit items first.
			// If the sanitizer makes no changes, the current outfit remains the correct baseline to preserve.
			if (auto* taskInterface = SKSE::GetTaskInterface(); taskInterface && g_outfitTaskQueuedForActor != actorID) {
				g_outfitTaskQueuedForActor = actorID;
				taskInterface->AddTask([actorID]() {
					auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
					if (!actor) {
						return;
					}
					if (actor->IsDead()) {
						if (spdlog::should_log(spdlog::level::trace)) {
							logger::trace("ContainerMenuOpenActions: SKSE task skipping dead actor {:08X} ({})",
								actorID, actor->GetName());
						}
						return;
					}

					// Capture the baseline so restore features preserve the correct state after later refreshes.
					OutfitSnapshotRestore::CaptureBaselineIfEmpty(actor);
					HandItemRestore::CaptureBaselineIfEmpty(actor);
				});
			}
		});

		g_menuCloseHandle = ContainerMenuUtil::AddOnContainerMenuCloseListener([]() {
			g_seededActorId = 0;
			g_outfitTaskQueuedForActor = 0;
			g_enrolledActorId = 0;
			g_processedActorId = 0;
			g_processedResult = false;
		});

		g_installed = true;
	}

	void ContainerMenuOpenActions::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_listenerHandle != 0) {
			ContainerMenuDisplayHook::UnregisterListener(g_listenerHandle);
			g_listenerHandle = 0;
		}
		if (g_menuCloseHandle != 0) {
			ContainerMenuUtil::RemoveOnContainerMenuCloseListener(g_menuCloseHandle);
			g_menuCloseHandle = 0;
		}

		g_seededActorId = 0;
		g_outfitTaskQueuedForActor = 0;
		g_installed = false;
	}
}
