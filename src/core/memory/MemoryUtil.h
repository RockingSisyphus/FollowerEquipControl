// Windows memory helpers for defensive hook installation: pointer plausibility and page-protection checks.

#pragma once

#include "PCH.h"

#include <cstddef>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

namespace FEC::MemoryUtil
{
	[[nodiscard]] inline bool IsPlausiblePointer(const void* a_ptr) noexcept
	{
		const auto v = reinterpret_cast<std::uintptr_t>(a_ptr);
		if (v < 0x10000) {
			return false;
		}
		if ((v & (alignof(void*) - 1)) != 0) {
			return false;
		}
		return true;
	}

	[[nodiscard]] inline bool IsReadableMemory(const void* a_ptr, std::size_t a_bytes) noexcept
	{
		if (!a_ptr || a_bytes == 0) {
			return false;
		}
		if (!IsPlausiblePointer(a_ptr)) {
			return false;
		}

		MEMORY_BASIC_INFORMATION mbi{};
		if (VirtualQuery(a_ptr, &mbi, sizeof(mbi)) == 0) {
			return false;
		}
		if (mbi.State != MEM_COMMIT) {
			return false;
		}
		if ((mbi.Protect & PAGE_GUARD) != 0 || (mbi.Protect & PAGE_NOACCESS) != 0) {
			return false;
		}

		const auto prot = (mbi.Protect & 0xFF);
		const bool readable = (prot == PAGE_READONLY) || (prot == PAGE_READWRITE) || (prot == PAGE_WRITECOPY) ||
			(prot == PAGE_EXECUTE_READ) || (prot == PAGE_EXECUTE_READWRITE) || (prot == PAGE_EXECUTE_WRITECOPY);
		if (!readable) {
			return false;
		}

		const auto start = reinterpret_cast<std::uintptr_t>(a_ptr);
		const auto regionStart = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
		const auto regionEnd = regionStart + static_cast<std::uintptr_t>(mbi.RegionSize);
		const auto end = start + static_cast<std::uintptr_t>(a_bytes);
		return end <= regionEnd;
	}

	[[nodiscard]] inline bool IsExecutableMemory(const void* a_ptr) noexcept
	{
		if (!a_ptr) {
			return false;
		}
		if (!IsPlausiblePointer(a_ptr)) {
			return false;
		}

		MEMORY_BASIC_INFORMATION mbi{};
		if (VirtualQuery(a_ptr, &mbi, sizeof(mbi)) == 0) {
			return false;
		}
		if (mbi.State != MEM_COMMIT) {
			return false;
		}
		if ((mbi.Protect & PAGE_GUARD) != 0 || (mbi.Protect & PAGE_NOACCESS) != 0) {
			return false;
		}

		const auto prot = (mbi.Protect & 0xFF);
		return (prot == PAGE_EXECUTE) || (prot == PAGE_EXECUTE_READ) || (prot == PAGE_EXECUTE_READWRITE) || (prot == PAGE_EXECUTE_WRITECOPY);
	}
}
