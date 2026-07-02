#include "StaleWeightCacheFix.h"

#include "ContainerMenuUtil.h"
#include "KnownFollowerState.h"
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

namespace FEC::EquipMode::Fixes
{
	namespace
	{
		bool g_installed{ false };

		inline decltype(&StaleWeightCacheFix::Thunk) g_original{ nullptr };

		// ComputeInventoryWeight -> GetInventory can re-enter GetInventoryWeight.
		thread_local bool g_inThunk{ false };

		// Inventory item weights can be fractional.
		constexpr float kEpsilon = 1.0f;

		// Verification is per ContainerMenu session.
		bool g_verified{ false };

		[[nodiscard]] float ComputeInventoryWeight(RE::TESObjectREFR* a_owner)
		{
			if (!a_owner) {
				return 0.0f;
			}

			double total = 0.0;
			auto inv = a_owner->GetInventory();
			for (const auto& [obj, pair] : inv) {
				const auto count = pair.first;
				const auto& entryPtr = pair.second;
				auto* entry = entryPtr.get();
				if (!entry || count <= 0) {
					continue;
				}
				const double w = static_cast<double>(entry->GetWeight());
				if (w <= 0.0) {
					continue;
				}
				total += w * static_cast<double>(count);
			}

			return static_cast<float>(total);
		}
	}

	float StaleWeightCacheFix::Thunk(RE::InventoryChanges* a_this)
	{
		if (g_inThunk || !a_this || !a_this->owner) {
			return g_original(a_this);
		}

		if (!ContainerMenuUtil::IsContainerMenuOpen()) {
			g_verified = false;
			return g_original(a_this);
		}

		// Dead actors can have transient inventory state while SkyrimSoulsRE ticks during ContainerMenu.
		// Calling GetInventory on them can AV inside the tree build.
		auto* actor = a_this->owner->As<RE::Actor>();
		if (!actor || actor->IsPlayerRef() || actor->IsDead() ||
			!KnownFollowerState::IsPlayerTeammateOrPersistedKnown(actor)) {
			return g_original(a_this);
		}

		// Snapshot changed before the engine call.
		const bool wasChanged = a_this->changed;

		const float result = g_original(a_this);

		if (g_verified && !wasChanged) {
			return result;
		}

		g_inThunk = true;
		const float computed = ComputeInventoryWeight(a_this->owner);
		g_inThunk = false;

		g_verified = true;

		const float diff = result - computed;
		if (diff > kEpsilon || diff < -kEpsilon) {
			logger::info(
				"StaleWeightCacheFix: {:08X} ({}) engine={:.1f} → computed={:.1f}",
				actor->GetFormID(), actor->GetName(), result, computed);

			a_this->totalWeight = computed;
			return computed;
		}

		return result;
	}

	void StaleWeightCacheFix::Install()
	{
		if (!PluginSettings::Get().engineFixes.enableStaleWeightCacheFix) {
			Uninstall();
			return;
		}

		if (g_installed) {
			return;
		}

		const auto addr = Relocations::kGetInventoryWeight.address();
		if (!addr) {
			logger::warn("StaleWeightCacheFix: failed to resolve GetInventoryWeight address");
			return;
		}

		g_original = reinterpret_cast<decltype(g_original)>(addr);

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());

		if (DetourAttach(reinterpret_cast<PVOID*>(&g_original), &Thunk) != NO_ERROR) {
			DetourTransactionAbort();
			logger::error("StaleWeightCacheFix: DetourAttach failed");
			return;
		}

		if (DetourTransactionCommit() != NO_ERROR) {
			logger::error("StaleWeightCacheFix: DetourTransactionCommit failed");
			return;
		}

		g_installed = true;
		logger::info("StaleWeightCacheFix: installed (GetInventoryWeight detour)");
	}

	void StaleWeightCacheFix::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());
		DetourDetach(reinterpret_cast<PVOID*>(&g_original), &Thunk);
		DetourTransactionCommit();

		g_installed = false;
		logger::info("StaleWeightCacheFix: uninstalled (GetInventoryWeight detour)");
	}
}
