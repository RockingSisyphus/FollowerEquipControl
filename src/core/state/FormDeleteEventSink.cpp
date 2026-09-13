#include "FormDeleteEventSink.h"

#include "ActorStateCleanup.h"
#include "KnownFollowerState.h"
#include "Logging.h"

#include "RE/S/ScriptEventSourceHolder.h"

namespace FEC
{
	namespace
	{
		bool g_installed{ false };
	}

	void FormDeleteEventSink::Install()
	{
		if (g_installed) {
			return;
		}

		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (!holder) {
			return;
		}

		holder->AddEventSink<RE::TESFormDeleteEvent>(GetSingleton());
		holder->AddEventSink<RE::TESDeathEvent>(GetSingleton());
		g_installed = true;
		logger::info("FormDeleteEventSink: installed TESFormDeleteEvent + TESDeathEvent sinks");
	}

	void FormDeleteEventSink::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
		if (holder) {
			holder->RemoveEventSink<RE::TESFormDeleteEvent>(GetSingleton());
			holder->RemoveEventSink<RE::TESDeathEvent>(GetSingleton());
		}

		g_installed = false;
		logger::info("FormDeleteEventSink: uninstalled TESFormDeleteEvent + TESDeathEvent sinks");
	}

	RE::BSEventNotifyControl FormDeleteEventSink::ProcessEvent(
		const RE::TESFormDeleteEvent* a_event,
		RE::BSTEventSource<RE::TESFormDeleteEvent>*)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const auto formID = a_event->formID;
		if (formID == 0) {
			return RE::BSEventNotifyControl::kContinue;
		}

		// Only purge dynamic forms; persistent FormIDs may be reused after deletion.
		if ((formID & 0xFF000000) != 0xFF000000) {
			return RE::BSEventNotifyControl::kContinue;
		}

		ActorStateCleanup::PurgeActorFromAllStores(formID);

		logger::info("FormDeleteEventSink: TESFormDeleteEvent — purged dynamic actor {:08X}", formID);

		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl FormDeleteEventSink::ProcessEvent(
		const RE::TESDeathEvent* a_event,
		RE::BSTEventSource<RE::TESDeathEvent>*)
	{
		if (!a_event) {
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* objRef = a_event->actorDying.get();
		if (!objRef) {
			return RE::BSEventNotifyControl::kContinue;
		}

		auto* actor = objRef->As<RE::Actor>();
		if (!actor) {
			return RE::BSEventNotifyControl::kContinue;
		}

		const auto formID = actor->GetFormID();

		// Persistent actors may die and later be resurrected; keep their saved state.
		if ((formID & 0xFF000000) != 0xFF000000) {
			return RE::BSEventNotifyControl::kContinue;
		}

		if (!KnownFollowerState::IsPersistedKnown(actor)) {
			return RE::BSEventNotifyControl::kContinue;
		}

		// Expiring summons can fire TESDeathEvent(dead=false), enter death animation,
		// and then have their dynamic FormID recycled without dead=true or TESFormDeleteEvent.
		// Purge on the first death event so the next actor using this FormID does not inherit scope.
		// This is safe for tracked 0xFF actors; a later dead=true event is a no-op.
		const auto* name = actor->GetName();
		ActorStateCleanup::PurgeActorFromAllStores(formID);
		logger::info("FormDeleteEventSink: TESDeathEvent — purged dynamic actor {:08X} '{}' (dead={})",
			formID, name ? name : "<unnamed>", a_event->dead);

		return RE::BSEventNotifyControl::kContinue;
	}
}
