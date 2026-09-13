#include "SyntheticXList.h"

#include "InventoryUtil.h"

#include <RE/I/InventoryChanges.h>
#include <RE/I/InventoryEntryData.h>
#include <RE/M/MemoryManager.h>
#include <SKSE/Version.h>

#include <unordered_set>

namespace FEC::SyntheticXList
{
	namespace
	{
		// Tracks synthetic xLists allocated by this module.
		std::unordered_set<RE::ExtraDataList*> g_synthetics;

		// BaseExtraList gained a virtual destructor in AE 1.6.629.
		// SE and AE < 1.6.629: data*(+0x00) presence*(+0x08) lock(+0x10) = 0x18
		// AE >= 1.6.629:       vptr(+0x00) data*(+0x08) presence*(+0x10) lock(+0x18) = 0x20
		[[nodiscard]] bool RuntimeHasExtraDataListVPtr()
		{
			return REL::Module::get().version().compare(SKSE::RUNTIME_SSE_1_6_629) != std::strong_ordering::less;
		}

		// Only valid on AE >= 1.6.629 where offset 0 is the vtable pointer.
		[[nodiscard]] std::uintptr_t GetDonorVPtr(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return 0;
			}
			auto* changes = a_actor->GetInventoryChanges();
			if (!changes || !changes->entryList) {
				return 0;
			}
			for (auto* entry : *changes->entryList) {
				if (!entry || !entry->extraLists) {
					continue;
				}
				for (auto* x : *entry->extraLists) {
					if (x) {
						return *reinterpret_cast<std::uintptr_t*>(x);
					}
				}
			}
			return 0;
		}

		[[nodiscard]] bool ShouldLogSyntheticXListDebug() noexcept
		{
			return spdlog::should_log(spdlog::level::debug);
		}

		[[nodiscard]] RE::FormID SafeFormID(const RE::TESForm* a_form) noexcept
		{
			return a_form ? a_form->GetFormID() : 0;
		}

		[[nodiscard]] const char* SafeName(const RE::TESForm* a_form) noexcept
		{
			if (!a_form) {
				return "";
			}

			const auto* name = a_form->GetName();
			return name ? name : "";
		}

		template <class Fn>
		void LogSyntheticXListDebug(Fn&& a_fn)
		{
			if (ShouldLogSyntheticXListDebug()) {
				a_fn();
			}
		}

	}

	RE::ExtraDataList* Create(RE::Actor* a_actor, std::int32_t a_count)
	{
		const bool needsVPtr = RuntimeHasExtraDataListVPtr();
		const std::size_t allocSize = needsVPtr ? 0x20 : 0x18;

		std::uintptr_t donorVPtr = 0;
		if (needsVPtr) {
			donorVPtr = GetDonorVPtr(a_actor);
			if (donorVPtr == 0) {
				logger::warn("SyntheticXList: no donor vptr found for actor {:08X}",
					SafeFormID(a_actor));
				return nullptr;
			}
		}

		// calloc zeroes data, presence, and BSReadWriteLock into valid empty state.
		auto* raw = RE::calloc(1, allocSize);
		if (!raw) {
			logger::warn("SyntheticXList: calloc({:#x}) failed", allocSize);
			return nullptr;
		}

		// Copy vptr from donor only on AE >= 1.6.629.
		// On older runtimes, offset 0 is BSExtraData* data, not a vtable pointer.
		// Writing a donor value there would alias two lists' data chains and crash.
		if (needsVPtr) {
			*reinterpret_cast<std::uintptr_t*>(raw) = donorVPtr;
		}

		auto* xList = reinterpret_cast<RE::ExtraDataList*>(raw);

		// SetCount is a native trampoline (RELOCATION_ID(11471, 11617)).
		// It creates ExtraCount and allocates the presence bitfield automatically.
		// Do not call SetInventoryChanges here.
		if (a_count > 0) {
			xList->SetCount(static_cast<std::uint16_t>(a_count));
		}

		g_synthetics.insert(xList);

		if (needsVPtr) {
			LogSyntheticXListDebug([&] { logger::debug("SyntheticXList: created xList={:p} vptr={:016X} count={} layout=AE(0x20)",
				raw, donorVPtr, a_count); });
		} else {
			LogSyntheticXListDebug([&] { logger::debug("SyntheticXList: created xList={:p} count={} layout=SE(0x18)",
				raw, a_count); });
		}
		return xList;
	}

	RE::ExtraDataList* EnsureXList(RE::Actor* a_actor, RE::InventoryEntryData* a_entry)
	{
		if (!a_actor || !a_entry) {
			return nullptr;
		}

		// Reuse only synthetic xLists created by this module.
		if (a_entry->extraLists && !a_entry->extraLists->empty()) {
			for (auto* x : *a_entry->extraLists) {
				if (x && IsSynthetic(x)) {
					LogSyntheticXListDebug([&] { logger::debug("SyntheticXList: EnsureXList reusing synthetic xList={:p}",
						static_cast<void*>(x)); });
					return x;
				}
			}
		}

		// Compute implicit copies not represented by xLists.
		// Use GetTotalCount, not countDelta: template-container copies are not in
		// InventoryChanges and can have countDelta == 0. Using countDelta here can
		// miss free base copies and leave the caller with EquipBase(nullptr).
		std::int32_t accountedByXLists = 0;
		if (a_entry->extraLists) {
			for (auto* x : *a_entry->extraLists) {
				if (x) {
					const auto c = x->GetCount();
					accountedByXLists += (c > 0) ? c : 1;
				}
			}
		}
		auto* object = a_entry->GetObject();
		const std::int32_t totalCount = (a_actor && object)
			? InventoryUtil::GetTotalCount(a_actor, object)
			: a_entry->countDelta;
		const auto implicitCount = totalCount - accountedByXLists;
		if (implicitCount <= 0) {
			LogSyntheticXListDebug([&] { logger::debug("SyntheticXList: EnsureXList no implicit instances (totalCount={}, accounted={})",
				totalCount, accountedByXLists); });
			return nullptr;
		}

		auto* synth = Create(a_actor, static_cast<std::int32_t>(implicitCount));
		if (!synth) {
			return nullptr;
		}

		// AddExtraList allocates the BSSimpleList when needed.
		a_entry->AddExtraList(synth);

		auto* obj = a_entry->GetObject();
		LogSyntheticXListDebug([&] { logger::debug("SyntheticXList: injected into entry object={:08X} '{}' count={}",
			SafeFormID(obj),
			SafeName(obj),
			implicitCount); });
		return synth;
	}

	bool IsSynthetic(RE::ExtraDataList* a_xList)
	{
		return a_xList && g_synthetics.contains(a_xList);
	}
}

