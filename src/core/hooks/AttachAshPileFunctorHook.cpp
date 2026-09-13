// Detours hook for SkyrimScript::AttachAshPileFunctor::operator().
// Detours patches the function body instead of the shared vtable entry, avoiding vtable conflicts with other SKSE plugins.

#include "AttachAshPileFunctorHook.h"

#include "ActorStateCleanup.h"
#include "KnownFollowerState.h"
#include "Logging.h"
#include "Relocations.h"

#include <RE/A/AttachAshPileFunctor.h>
#include <RE/V/Variable.h>

#include <Windows.h>
#include <detours/detours.h>

namespace FEC
{
	namespace
	{
		// BSScript::Variable is returned through a hidden out-parameter on MSVC x64.
		// The detour signature must match the raw ABI exactly to avoid stack corruption.
		using FuncT = RE::BSScript::Variable*(*)(
			RE::SkyrimScript::AttachAshPileFunctor* a_this,
			RE::BSScript::Variable*                 a_retVal);

		FuncT _func      = nullptr;
		bool  _installed = false;

		RE::BSScript::Variable* thunk(
			RE::SkyrimScript::AttachAshPileFunctor* a_this,
			RE::BSScript::Variable*                 a_retVal)
		{
			// Capture the handle before the original functor can mutate or release itself.
			const RE::ActorHandle actorHandle = a_this ? a_this->targetActor : RE::ActorHandle{};

			// Attach ExtraAshPileRef through the original functor.
			auto* result = _func(a_this, a_retVal);

			// Purge immediately after ash-pile attachment. Check persisted state first to avoid log noise for untracked actors.
			if (actorHandle) {
				auto actor = actorHandle.get();
				if (actor) {
					const auto formID = actor->GetFormID();
					if (formID != 0) {
						const bool wasTracked = KnownFollowerState::IsPersistedKnown(actor.get());
						ActorStateCleanup::PurgeActorFromAllStores(formID);
						if (wasTracked) {
							logger::info("AttachAshPileFunctorHook: purged ash-piled actor {:08X} '{}'",
								formID, actor->GetName());
						}
					}
				}
			}

			return result;
		}
	}

	void AttachAshPileFunctorHook::Install()
	{
		if (_installed) {
			return;
		}

		const auto vtbl = REL::Relocation<std::uintptr_t>(Relocations::kAttachAshPileFunctorVtbl);
		const auto* vptr = reinterpret_cast<const std::uintptr_t*>(vtbl.address());
		const auto funcAddr = vptr[Relocations::kAttachAshPileFunctor_CallOperatorVfuncIndex];
		if (!funcAddr) {
			logger::error("AttachAshPileFunctorHook: vtable entry for operator() is null");
			return;
		}

		_func = reinterpret_cast<FuncT>(funcAddr);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		if (const auto err = DetourAttach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); err != NO_ERROR) {
			DetourTransactionAbort();
			logger::error("AttachAshPileFunctorHook: failed to attach detour (err={})", err);
			return;
		}
		if (const auto err = DetourTransactionCommit(); err != NO_ERROR) {
			logger::error("AttachAshPileFunctorHook: failed to commit detour (err={})", err);
			return;
		}

		_installed = true;
		logger::info("AttachAshPileFunctorHook: installed (Detours trampoline on AttachAshPileFunctor::operator())");
	}

	void AttachAshPileFunctorHook::Uninstall()
	{
		if (!_installed) {
			return;
		}

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		if (const auto err = DetourDetach(reinterpret_cast<PVOID*>(&_func), reinterpret_cast<PVOID>(thunk)); err != NO_ERROR) {
			DetourTransactionAbort();
			logger::warn("AttachAshPileFunctorHook: failed to detach detour (err={})", err);
			return;
		}
		if (const auto err = DetourTransactionCommit(); err != NO_ERROR) {
			logger::warn("AttachAshPileFunctorHook: failed to commit detour removal (err={})", err);
			return;
		}

		_installed = false;
		logger::info("AttachAshPileFunctorHook: uninstalled");
	}
}
