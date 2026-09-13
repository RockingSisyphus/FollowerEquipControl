// Shared GFx probes for weapon recharge UI state and SkyUI platform detection.

#pragma once

#include "PCH.h"

#include <initializer_list>
#include <string_view>

namespace FEC::WeaponRecharge::GfxUtil
{
	[[nodiscard]] bool TryGetMemberBool(const RE::GFxValue& a_obj, const char* a_name, bool& a_out);
	[[nodiscard]] bool TryGetMemberNumber(const RE::GFxValue& a_obj, const char* a_name, double& a_out);
	[[nodiscard]] bool TryGetMemberString(const RE::GFxValue& a_obj, const char* a_name, std::string_view& a_out);
	[[nodiscard]] bool TryGetNestedNumber(const RE::GFxValue& a_root, std::initializer_list<const char*> a_path, double& a_out);

	[[nodiscard]] bool IsSkyUiPcPlatform(const RE::GFxValue& a_root);
	[[nodiscard]] bool IsSkyUiPresent(const RE::GFxValue& a_root);
	[[nodiscard]] std::int32_t GetSkyUiPlatform(const RE::GFxValue& a_root);
	[[nodiscard]] bool IsItemSelected(const RE::GFxValue& a_root);
	[[nodiscard]] bool IsChargeActionAvailable(const RE::GFxValue& a_root);
	[[nodiscard]] bool TryGetActiveSegment(const RE::GFxValue& a_root, std::int32_t& a_out);
}
