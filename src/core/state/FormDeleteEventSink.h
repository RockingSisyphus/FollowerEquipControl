// Cleans per-actor state when actors leave the world.
// TESFormDeleteEvent covers ScriptEventSourceHolder form deletion, but may miss some 0xFF dynamic refs.
// TESDeathEvent is the primary cleanup path for expired or dismissed summoned actors before garbage collection.

#pragma once

#include "PCH.h"

#include "RE/T/TESDeathEvent.h"
#include "RE/T/TESFormDeleteEvent.h"

namespace FEC
{
	class FormDeleteEventSink :
		public RE::BSTEventSink<RE::TESFormDeleteEvent>,
		public RE::BSTEventSink<RE::TESDeathEvent>
	{
	public:
		static FormDeleteEventSink* GetSingleton()
		{
			static FormDeleteEventSink instance;
			return std::addressof(instance);
		}

		FormDeleteEventSink(const FormDeleteEventSink&) = delete;
		FormDeleteEventSink(FormDeleteEventSink&&) = delete;

		FormDeleteEventSink& operator=(const FormDeleteEventSink&) = delete;
		FormDeleteEventSink& operator=(FormDeleteEventSink&&) = delete;

		static void Install();
		static void Uninstall();

		virtual RE::BSEventNotifyControl ProcessEvent(
			const RE::TESFormDeleteEvent* a_event,
			RE::BSTEventSource<RE::TESFormDeleteEvent>* a_source) override;

		virtual RE::BSEventNotifyControl ProcessEvent(
			const RE::TESDeathEvent* a_event,
			RE::BSTEventSource<RE::TESDeathEvent>* a_source) override;

	protected:
		FormDeleteEventSink() = default;
		~FormDeleteEventSink() override = default;
	};
}
