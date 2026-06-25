#include "QuickTrade.h"

#include "ActorInclusion.h"
#include "ActorScope.h"
#include "Controls.h"
#include "PluginSettings.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <SKSE/InputMap.h>

#include "RE/C/ContainerMenu.h"
#include "RE/D/DialogueMenu.h"
#include "RE/M/MenuOpenCloseEvent.h"
#include "RE/M/MenuTopicManager.h"
#include "RE/U/UI.h"
#include "RE/U/UIMessageQueue.h"
#include "REX/W32/KERNEL32.h"

namespace FEC::QuickTrade
{
	namespace
	{
		bool g_installed{ false };
		std::atomic<bool> g_pseudoSessionActive{ false };

		// Tracks the actor whose pseudo-session must be closed, even if MTM.speaker changes.
		// Game-thread only: set by OpenQuickTrade and cleared by ClearPseudoSession.
		RE::ObjectRefHandle g_pseudoSessionTarget{};

		// May point to another module's hook; preserve the chain.
		using ProcessButton_t = void(RE::ActivateHandler*, RE::ButtonEvent*, RE::PlayerControlsData*);
		ProcessButton_t* g_originalProcessButton{ nullptr };

		// Cached on the game thread; HookedProcessButton only reads these values.
		// Keyboard uses DI scan code; gamepad uses SKSE keycode 266-281. 0 = unbound.
		std::uint32_t g_sprintDik{ 0 };
		std::uint32_t g_sprintGamepadKey{ 0 };

		// Resolve the crosshair handle through TESObjectREFR before casting.
		// crosshair->targetActor can resolve to a non-actor REFR; Actor::LookupByHandle
		// does not validate FormType and can crash on Actor-only field access.
		RE::NiPointer<RE::Actor> GetCrosshairActor()
		{
			auto* crosshair = RE::CrosshairPickData::GetSingleton();
			if (!crosshair) {
				return {};
			}
			if (!crosshair->targetActor) {
				return {};
			}
			RE::NiPointer<RE::TESObjectREFR> refr;
			RE::LookupReferenceByHandle(crosshair->targetActor.native_handle(), refr);
			if (!refr) {
				return {};
			}
			if (refr->GetFormType() != RE::FormType::ActorCharacter) {
				return {};
			}
			RE::NiPointer<RE::Actor> actor{ static_cast<RE::Actor*>(refr.get()) };
			if (actor->IsDeleted() || !actor->Is3DLoaded()) {
				return {};
			}
			if (actor->IsDead()) {
				return {};
			}
			return actor;
		}

		// Mirrors OpenQuickTrade's SetDialogueWithPlayer(true) with vfunc 0x40 false.
		// Do not use InitiateDialogue or EndDialogue here: InitiateDialogue pushes an
		// AI dialogue package that EndDialogue may not pop for actors without authored
		// dialogue topics, leaving activation-blocking dialogue state behind.
		// CommonLib MenuOpenCloseEvent dispatches on the main game thread.
		void ClearPseudoSession()
		{
			logger::info("QuickTrade: ClearPseudoSession | target={:08X}",
				g_pseudoSessionTarget ? g_pseudoSessionTarget.native_handle() : 0);

			g_pseudoSessionActive.store(false, std::memory_order_release);

			// Mirrors the speaker-less DialogueMenu kShow queued in OpenQuickTrade.
			if (auto* msgQueue = RE::UIMessageQueue::GetSingleton()) {
				msgQueue->AddMessage(RE::DialogueMenu::MENU_NAME,
					RE::UI_MESSAGE_TYPE::kHide, nullptr);
			} else {
				logger::warn("QuickTrade: ClearPseudoSession — UIMessageQueue null, kHide NOT sent");
			}

			const auto targetHandle = g_pseudoSessionTarget;
			g_pseudoSessionTarget = RE::ObjectRefHandle{};
			if (targetHandle) {
				if (auto actor = RE::Actor::LookupByHandle(targetHandle.native_handle())) {
					// Deleted actors cannot safely receive SetDialogueWithPlayer or state writes.
					if (!actor->IsDeleted()) {
						actor->SetDialogueWithPlayer(false, false, nullptr);
						if (auto* state = actor->AsActorState()) {
							state->actorState2.talkingToPlayer = 0;
						}
						actor->GetActorRuntimeData().dialogueItemTarget = RE::ObjectRefHandle{};
					}
				} else {
					logger::warn("QuickTrade: ClearPseudoSession — target {:08X} no longer resolves",
						targetHandle.native_handle());
				}
			}

			if (auto* player = RE::PlayerCharacter::GetSingleton()) {
				if (auto* proc = player->GetActorRuntimeData().currentProcess; proc && proc->high) {
					proc->high->talkingToPC = false;
				}
			}

			if (auto* mtm = RE::MenuTopicManager::GetSingleton()) {
				REX::W32::EnterCriticalSection(&mtm->criticalSection);
				mtm->speaker = RE::ObjectRefHandle{};
				mtm->lastSpeaker = RE::ObjectRefHandle{};
				REX::W32::LeaveCriticalSection(&mtm->criticalSection);
			}

			logger::info("QuickTrade: session cleared");
		}

		// Use a speaker-less DialogueMenu as the engine auto-close cascade trigger.
		// Combat, death, and cell-change watchers key off DialogueMenu in the UI stack;
		// MTM.speaker is not needed and causes per-tick topic evaluation stutter.
		// SetDialogueWithPlayer is vfunc 0x40 on TESObjectREFR and does not push an
		// AI dialogue package. Do not call Actor::InitiateDialogue here.
		void OpenQuickTrade(RE::Actor* a_target)
		{
			if (!a_target) {
				return;
			}
			const auto handle = a_target->GetHandle();

			// Do not set MTM.speaker; the DialogueMenu kShow is enough for auto-close cascade.
			g_pseudoSessionActive.store(true, std::memory_order_release);
			g_pseudoSessionTarget = handle;

			if (auto* task = SKSE::GetTaskInterface()) {
				task->AddTask([handle]() {
					auto actor = RE::Actor::LookupByHandle(handle.native_handle());
					if (!actor || actor->IsDeleted() || !actor->Is3DLoaded()) {
						logger::warn("QuickTrade: task — target {:08X} invalid at task time; clearing",
							handle.native_handle());
						ClearPseudoSession();
						return;
					}

					// ClearPseudoSession may run before this SKSE task; do not reopen a torn-down session.
					if (!g_pseudoSessionActive.load(std::memory_order_acquire)) {
						logger::warn("QuickTrade: task — session already cleared before task ran; aborting");
						return;
					}

					// vfunc 0x40 on TESObjectREFR; unlike InitiateDialogue, this does not push an AI package.
					actor->SetDialogueWithPlayer(true, false, nullptr);

					// SetDialogueWithPlayer may no-op for actors without authored dialogue.
					// Directly set the cluster read by the engine cascade watchers.
					if (auto* player = RE::PlayerCharacter::GetSingleton()) {
						if (auto* state = actor->AsActorState()) {
							state->actorState2.talkingToPlayer = 1;
						}
						actor->GetActorRuntimeData().dialogueItemTarget = player->GetHandle();
						if (auto* proc = player->GetActorRuntimeData().currentProcess; proc && proc->high) {
							proc->high->talkingToPC = true;
						}
					}

					// DialogueMenu kShow drives combat, death, and cell-change auto-close cascade.
					if (auto* msgQueue = RE::UIMessageQueue::GetSingleton()) {
						msgQueue->AddMessage(RE::DialogueMenu::MENU_NAME,
							RE::UI_MESSAGE_TYPE::kShow, nullptr);
					}

					actor->OpenContainer(3);  // 3 = kNPCMode
					logger::info("QuickTrade: opened for {:08X} ({})",
						actor->GetFormID(), actor->GetName());
				});
			} else {
				logger::warn("QuickTrade: SKSE task interface unavailable; clearing pseudo-session");
				ClearPseudoSession();
			}
		}

		class CloseSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			static CloseSink* GetSingleton()
			{
				static CloseSink s;
				return std::addressof(s);
			}

			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent* a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}

				// Watch DialogueMenu too: OpenContainer(3) can no-op for actors without
				// barter containers, leaving only the speaker-less DialogueMenu to close.
				// g_pseudoSessionActive prevents double-cleanup when both menus close.
				if (!a_event->opening) {
					const bool isContainer = (a_event->menuName == RE::ContainerMenu::MENU_NAME);
					const bool isDialogue  = (a_event->menuName == RE::DialogueMenu::MENU_NAME);
					if ((isContainer || isDialogue) &&
						g_pseudoSessionActive.load(std::memory_order_acquire)) {
						ClearPseudoSession();
					}
				}

				return RE::BSEventNotifyControl::kContinue;
			}
		};

		bool g_sinkInstalled{ false };

		[[nodiscard]] bool IsModifierHeld()
		{
			// A non-zero setting is read live; sentinel 0 uses cached Sprint binding.
			// Keyboard and gamepad are independent, and either device can satisfy the modifier.
			const auto& kc = PluginSettings::Get().keyboardControls;
			const auto& gc = PluginSettings::Get().gamepadControls;

			const std::uint32_t kbDik = (kc.quickTradeKeyDik != 0) ? kc.quickTradeKeyDik : g_sprintDik;
			if (kbDik != 0 && Controls::IsDikKeyDown(kbDik)) {
				return true;
			}

			const std::uint32_t gpKey = (gc.quickTradeGamepadKey != 0) ? gc.quickTradeGamepadKey : g_sprintGamepadKey;
			if (gpKey != 0 && Controls::IsGamepadButtonDown(gpKey)) {
				return true;
			}

			return false;
		}

		// Keep this close to vanilla Activate-on-NPC rejection behavior.
		// Only block cases that become harmful if bypassed by QuickTrade.
		[[nodiscard]] bool PreOpenGuardRejects(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return true;
			}

			if (a_actor->IsInCombat()) {
				logger::info("QuickTrade: PreOpenGuard — in combat");
				return true;
			}
			if (auto* player = RE::PlayerCharacter::GetSingleton();
				player && a_actor->IsHostileToActor(player)) {
				logger::info("QuickTrade: PreOpenGuard — hostile");
				return true;
			}
			if (a_actor->AsActorState() && a_actor->AsActorState()->IsBleedingOut()) {
				logger::info("QuickTrade: PreOpenGuard — bleedout");
				return true;
			}
			// Do not stomp an in-flight engine dialogue engagement.
			if (a_actor->AsActorState() && a_actor->AsActorState()->actorState2.talkingToPlayer != 0) {
				logger::info("QuickTrade: PreOpenGuard — already talking to player");
				return true;
			}
			if (a_actor->GetCurrentScene()) {
				logger::info("QuickTrade: PreOpenGuard — in scripted scene");
				return true;
			}

			return false;
		}

		void HookedProcessButton(RE::ActivateHandler* a_this, RE::ButtonEvent* a_event, RE::PlayerControlsData* a_data)
		{
			// ProcessButton runs on the main game thread for both press and release events.
			if (a_event && a_event->GetDevice() != RE::INPUT_DEVICE::kNone && a_event->IsDown()) {
				// Recover stale sessions when menus were closed without MenuOpenCloseEvent.
				// Keep this inside IsDown(); release fires before the SKSE task opens menus.
				if (g_pseudoSessionActive.load(std::memory_order_acquire)) {
					auto* ui = RE::UI::GetSingleton();
					if (ui && !ui->IsMenuOpen(RE::ContainerMenu::MENU_NAME) &&
							  !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) {
						logger::warn("QuickTrade: stale pseudo-session on Activate press; clearing");
						ClearPseudoSession();
					}
				}

				// Do not allow a second press to overwrite g_pseudoSessionTarget mid-session.
				if (!g_pseudoSessionActive.load(std::memory_order_acquire) && IsModifierHeld()) {
					auto target = GetCrosshairActor();
					if (target && ActorScope::ShouldAdmitForQuickTrade(target.get()) &&
						!PreOpenGuardRejects(target.get())) {
						OpenQuickTrade(target.get());
						return;
					}
				}
			}

			if (g_originalProcessButton) {
				g_originalProcessButton(a_this, a_event, a_data);
			}
		}
	}

	void Install()
	{
		// Do not call ClearPseudoSession here: Install also runs on settings-apply.
		// Save/load closes menus naturally; missed closes are handled on the next Activate press.

		// ControlMap::GetMappedKey is not safe from the input hook path.
		// Cache Sprint on the game thread and read only raw codes in HookedProcessButton.
		if (auto* controlMap = RE::ControlMap::GetSingleton()) {
			// Keyboard binding becomes a DI scan code.
			const auto kbKey = controlMap->GetMappedKey("Sprint", RE::INPUT_DEVICE::kKeyboard);
			g_sprintDik = (kbKey != RE::ControlMap::kInvalid) ? static_cast<std::uint32_t>(kbKey) : 0;

			// Gamepad binding becomes an SKSE keycode in the 266-281 range.
			const auto gpMask = controlMap->GetMappedKey("Sprint", RE::INPUT_DEVICE::kGamepad);
			if (gpMask != RE::ControlMap::kInvalid) {
				const auto skseKey = SKSE::InputMap::GamepadMaskToKeycode(gpMask);
				g_sprintGamepadKey = (skseKey >= Controls::kGamepadOffset && skseKey < Controls::kGamepadOffset + 16)
					? skseKey : 0;
			} else {
				g_sprintGamepadKey = 0;
			}

			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug("QuickTrade: sprint key cached -- keyboard DI={}, gamepad SKSE={}",
					g_sprintDik, g_sprintGamepadKey);
			}
		} else {
			logger::warn("QuickTrade: ControlMap not available -- sprint key detection disabled");
			g_sprintDik = 0;
			g_sprintGamepadKey = 0;
		}

		if (g_installed) {
			return;
		}

		auto* playerControls = RE::PlayerControls::GetSingleton();
		if (!playerControls) {
			logger::warn("QuickTrade: PlayerControls not available");
			return;
		}

		auto* activateHandler = playerControls->activateHandler;
		if (!activateHandler) {
			logger::warn("QuickTrade: ActivateHandler not available");
			return;
		}

		// vtable index 4 is ActivateHandler::ProcessButton; preserve any existing hook chain.
		auto* vtable = *reinterpret_cast<std::uintptr_t**>(activateHandler);
		g_originalProcessButton = reinterpret_cast<ProcessButton_t*>(vtable[4]);

		DWORD oldProtect;
		if (VirtualProtect(&vtable[4], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
			vtable[4] = reinterpret_cast<std::uintptr_t>(&HookedProcessButton);
			VirtualProtect(&vtable[4], sizeof(void*), oldProtect, &oldProtect);
		} else {
			logger::error("QuickTrade: failed to patch vtable");
			return;
		}

		g_installed = true;
		logger::info("QuickTrade: installed ActivateHandler hook");

		// CommonLib has no clean sink removal path; leave it registered and gate on state.
		if (!g_sinkInstalled) {
			if (auto* ui = RE::UI::GetSingleton()) {
				ui->AddEventSink<RE::MenuOpenCloseEvent>(CloseSink::GetSingleton());
				g_sinkInstalled = true;
			}
		}
	}

	void Uninstall()
	{
		if (!g_installed) {
			return;
		}

		auto* playerControls = RE::PlayerControls::GetSingleton();
		if (!playerControls) {
			return;
		}

		auto* activateHandler = playerControls->activateHandler;
		if (!activateHandler || !g_originalProcessButton) {
			return;
		}

		auto* vtable = *reinterpret_cast<std::uintptr_t**>(activateHandler);
		DWORD oldProtect;
		if (VirtualProtect(&vtable[4], sizeof(void*), PAGE_READWRITE, &oldProtect)) {
			vtable[4] = reinterpret_cast<std::uintptr_t>(g_originalProcessButton);
			VirtualProtect(&vtable[4], sizeof(void*), oldProtect, &oldProtect);
		}

		g_originalProcessButton = nullptr;
		g_installed = false;
		logger::info("QuickTrade: uninstalled ActivateHandler hook");
		
		ClearPseudoSession();
	}
}
