#include "WeaponRechargeGfxUtil.h"

namespace FEC::WeaponRecharge::GfxUtil
{
	bool TryGetMemberBool(const RE::GFxValue& a_obj, const char* a_name, bool& a_out)
	{
		if (!a_name) {
			return false;
		}
		RE::GFxValue v;
		if (!a_obj.GetMember(a_name, std::addressof(v)) || !v.IsBool()) {
			return false;
		}
		a_out = v.GetBool();
		return true;
	}

	bool TryGetMemberNumber(const RE::GFxValue& a_obj, const char* a_name, double& a_out)
	{
		if (!a_name) {
			return false;
		}
		RE::GFxValue v;
		if (!a_obj.GetMember(a_name, std::addressof(v)) || !v.IsNumber()) {
			return false;
		}
		a_out = v.GetNumber();
		return true;
	}

	bool TryGetMemberString(const RE::GFxValue& a_obj, const char* a_name, std::string_view& a_out)
	{
		if (!a_name) {
			return false;
		}
		RE::GFxValue v;
		if (!a_obj.GetMember(a_name, std::addressof(v)) || !v.IsString()) {
			return false;
		}
		const char* s = v.GetString();
		if (!s) {
			return false;
		}
		a_out = s;
		return true;
	}

	bool TryGetNestedNumber(const RE::GFxValue& a_root, std::initializer_list<const char*> a_path, double& a_out)
	{
		RE::GFxValue cur = a_root;
		for (const auto* key : a_path) {
			if (!cur.IsObject() && !cur.IsDisplayObject()) {
				return false;
			}
			RE::GFxValue next;
			if (!cur.GetMember(key, std::addressof(next))) {
				return false;
			}
			cur = next;
		}
		if (!cur.IsNumber()) {
			return false;
		}
		a_out = cur.GetNumber();
		return true;
	}

	bool IsSkyUiPcPlatform(const RE::GFxValue& a_root)
	{
		double platform{};
		if (!TryGetMemberNumber(a_root, "_platform", platform)) {
			return false;
		}
		return static_cast<std::int32_t>(platform) == 0;
	}

	bool IsSkyUiPresent(const RE::GFxValue& a_root)
	{
		double platform{};
		return TryGetMemberNumber(a_root, "_platform", platform);
	}

	std::int32_t GetSkyUiPlatform(const RE::GFxValue& a_root)
	{
		double platform{};
		if (!TryGetMemberNumber(a_root, "_platform", platform)) {
			return -1;
		}
		return static_cast<std::int32_t>(platform);
	}

	bool IsItemSelected(const RE::GFxValue& a_root)
	{
		double selectedIndex{};
		if (!TryGetNestedNumber(a_root, { "inventoryLists", "itemList", "selectedIndex" }, selectedIndex)) {
			return false;
		}
		return static_cast<std::int32_t>(selectedIndex) != -1;
	}

	bool IsChargeActionAvailable(const RE::GFxValue& a_root)
	{
		if (!IsItemSelected(a_root)) {
			return false;
		}

		double charge{};
		if (!TryGetNestedNumber(a_root, { "itemCard", "itemInfo", "charge" }, charge)) {
			return false;
		}
		return charge < 100.0;
	}

	bool TryGetActiveSegment(const RE::GFxValue& a_root, std::int32_t& a_out)
	{
		double active{};
		if (!TryGetNestedNumber(a_root, { "inventoryLists", "categoryList", "activeSegment" }, active)) {
			return false;
		}
		a_out = static_cast<std::int32_t>(active);
		return true;
	}
}
