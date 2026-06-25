#include "InfiniteAmmo.h"

#include "ActorScope.h"
#include "Logging.h"
#include "MemoryUtil.h"
#include "PluginSettings.h"
#include "Relocations.h"
#include "WeaponBound.h"

#include <atomic>
#include <cstdint>
#include <mutex>

namespace FEC::InfiniteAmmo
{
	namespace
	{
		std::atomic<bool> g_installed{ false };
		std::mutex g_installLock;

		[[nodiscard]] std::size_t UseAmmoVfuncIndex() noexcept
		{
			return REL::Relocate(
				FEC::Relocations::kActor_UseAmmoVfuncIndex_SEAE,
				FEC::Relocations::kActor_UseAmmoVfuncIndex_SEAE,
				FEC::Relocations::kActor_UseAmmoVfuncIndex_VR);
		}

		using UseAmmoFn = std::uint32_t (*)(RE::Actor*, std::uint32_t);

		REL::Relocation<std::uintptr_t> g_vtblActor{};
		REL::Relocation<std::uintptr_t> g_vtblCharacter{};
		std::uintptr_t g_origActor{ 0 };
		std::uintptr_t g_origCharacter{ 0 };

		[[nodiscard]] bool IsEnabled() noexcept
		{
			return PluginSettings::Get().combatEquipRestore.enableInfiniteAmmo;
		}

		std::uint32_t UseAmmoThunk(RE::Actor* a_this, std::uint32_t a_shotCount)
		{
			std::uintptr_t orig = 0;
			if (a_this) {
				const auto vptr = *reinterpret_cast<std::uintptr_t*>(a_this);
				if (g_vtblCharacter.address() && vptr == g_vtblCharacter.address()) {
					orig = g_origCharacter;
				} else if (g_vtblActor.address() && vptr == g_vtblActor.address()) {
					orig = g_origActor;
				}
			}

			if (!orig) {
				return 0;
			}

			if (!IsEnabled() || !ActorScope::IsAffectedFollower(a_this)) {
				return reinterpret_cast<UseAmmoFn>(orig)(a_this, a_shotCount);
			}

			// Bound weapons conjure their own ammo; do not interfere.
			if (auto* equipped = a_this->GetEquippedObject(false); equipped) {
				if (WeaponBound::IsWeaponAndBound(equipped->As<RE::TESBoundObject>())) {
					return reinterpret_cast<UseAmmoFn>(orig)(a_this, a_shotCount);
				}
			}

			if (spdlog::should_log(spdlog::level::debug)) {
				logger::debug("InfiniteAmmo: suppressed {} ammo consumption for {:08X}", a_shotCount, a_this->GetFormID());
			}
			return 0;
		}

		void HookVTable(const REL::VariantID& a_vtableID, std::string_view a_tag,
			REL::Relocation<std::uintptr_t>& a_vtbl, std::uintptr_t& a_outOriginal)
		{
			if (a_outOriginal != 0) {
				return;
			}

			a_vtbl = REL::Relocation<std::uintptr_t>(a_vtableID);
			if (!a_vtbl.address()) {
				logger::warn("InfiniteAmmo: {} vtable address not found", a_tag);
				return;
			}

			const auto kIdx = UseAmmoVfuncIndex();
			auto* const vptr = reinterpret_cast<std::uintptr_t*>(a_vtbl.address());
			if (!MemoryUtil::IsReadableMemory(vptr, (kIdx + 1) * sizeof(std::uintptr_t))) {
				logger::warn("InfiniteAmmo: {} vtable not readable at index 0x{:X}", a_tag, kIdx);
				return;
			}

			const auto current = vptr[kIdx];
			if (!MemoryUtil::IsPlausiblePointer(reinterpret_cast<const void*>(current)) ||
				!MemoryUtil::IsExecutableMemory(reinterpret_cast<const void*>(current))) {
				logger::warn("InfiniteAmmo: {} vtable slot 0x{:X} not plausible/executable", a_tag, kIdx);
				return;
			}

			a_outOriginal = a_vtbl.write_vfunc(kIdx, UseAmmoThunk);
			if (a_outOriginal != 0) {
				logger::info("InfiniteAmmo: hooked UseAmmo on {} (orig={:X})", a_tag, a_outOriginal);
			}
		}

		void UnhookVTable(REL::Relocation<std::uintptr_t>& a_vtbl, std::uintptr_t& a_original)
		{
			if (!a_vtbl.address() || a_original == 0) {
				return;
			}

			const auto kIdx = UseAmmoVfuncIndex();
			auto* const vptr = reinterpret_cast<std::uintptr_t*>(a_vtbl.address());
			if (!MemoryUtil::IsReadableMemory(vptr, (kIdx + 1) * sizeof(std::uintptr_t))) {
				return;
			}

			// Restore only if the slot still points to our thunk.
			const auto current = vptr[kIdx];
			const auto ours = reinterpret_cast<std::uintptr_t>(&UseAmmoThunk);
			if (current != ours) {
				return;
			}

			(void)a_vtbl.write_vfunc(kIdx, a_original);
			a_original = 0;
		}
	}

	void Install()
	{
		if (g_installed.load()) {
			return;
		}

		std::lock_guard lock(g_installLock);
		if (g_installed.load()) {
			return;
		}

		// Hook Actor and Character vtables. Do not hook PlayerCharacter.
		HookVTable(RE::VTABLE_Actor[0], "Actor", g_vtblActor, g_origActor);
		HookVTable(RE::VTABLE_Character[0], "Character", g_vtblCharacter, g_origCharacter);

		g_installed.store((g_origActor != 0) || (g_origCharacter != 0));
		if (g_installed.load()) {
			logger::info("InfiniteAmmo: installed");
		}
	}

	void Uninstall()
	{
		if (!g_installed.load()) {
			return;
		}

		std::lock_guard lock(g_installLock);
		if (!g_installed.load()) {
			return;
		}

		UnhookVTable(g_vtblActor, g_origActor);
		UnhookVTable(g_vtblCharacter, g_origCharacter);

		g_installed.store(false);
		logger::info("InfiniteAmmo: uninstalled");
	}
}
