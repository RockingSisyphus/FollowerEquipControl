#pragma once

#include "ContainerMenuUtil.h"
#include "ActorScope.h"
#include "KnownFollowerState.h"
#include "PluginSettings.h"

#include "PCH.h"

#include <cstdint>
#include <initializer_list>
#include <string_view>

namespace FEC::EquipMode::SkyUiMenuUtil
{
	[[nodiscard]] inline bool TryGetMemberBool(const RE::GFxValue& obj, const char* name, bool& out)
	{
		if (!name) {
			return false;
		}
		RE::GFxValue v;
		if (!obj.GetMember(name, &v) || !v.IsBool()) {
			return false;
		}
		out = v.GetBool();
		return true;
	}

	[[nodiscard]] inline bool TryGetMemberNumber(const RE::GFxValue& obj, const char* name, double& out)
	{
		if (!name) {
			return false;
		}
		RE::GFxValue v;
		if (!obj.GetMember(name, &v) || !v.IsNumber()) {
			return false;
		}
		out = v.GetNumber();
		return true;
	}

	[[nodiscard]] inline bool TryGetMemberString(const RE::GFxValue& obj, const char* name, std::string_view& out)
	{
		RE::GFxValue v;
		if (!obj.GetMember(name, &v) || !v.IsString()) {
			return false;
		}
		const auto* s = v.GetString();
		if (s) {
			out = s;
		}
		return s != nullptr;
	}

	[[nodiscard]] inline bool TryGetNestedNumber(const RE::GFxValue& root, std::initializer_list<const char*> path, double& out)
	{
		RE::GFxValue cur = root;
		for (const auto* key : path) {
			RE::GFxValue next;
			if (!cur.GetMember(key, &next)) {
				return false;
			}
			cur = next;
		}
		if (!cur.IsNumber()) {
			return false;
		}
		out = cur.GetNumber();
		return true;
	}

	[[nodiscard]] inline bool IsSkyUiPresent(const RE::GFxValue& root)
	{
		double platform{};
		return TryGetMemberNumber(root, "_platform", platform);
	}

	[[nodiscard]] inline bool IsSkyUiPcPlatform(const RE::GFxValue& root)
	{
		double platform{};
		if (!TryGetMemberNumber(root, "_platform", platform)) {
			return false;
		}
		return static_cast<std::int32_t>(platform) == 0;
	}

	[[nodiscard]] inline std::int32_t GetSkyUiPlatform(const RE::GFxValue& root)
	{
		double platform{};
		if (!TryGetMemberNumber(root, "_platform", platform)) {
			return -1;
		}
		return static_cast<std::int32_t>(platform);
	}

	[[nodiscard]] inline bool IsPlayerEquipModeActive(const RE::GFxValue& root)
	{
		bool enabled{ false };
		if (TryGetMemberBool(root, "_bEquipMode", enabled)) {
			return enabled;
		}
		return false;
	}

	[[nodiscard]] inline bool IsItemSelected(const RE::GFxValue& root)
	{
		double selectedIndex{};
		if (!TryGetNestedNumber(root, { "inventoryLists", "itemList", "selectedIndex" }, selectedIndex)) {
			return false;
		}
		return static_cast<std::int32_t>(selectedIndex) != -1;
	}

	[[nodiscard]] inline bool ShouldAffectMenu(RE::ContainerMenu* menu)
	{
		if (!menu) {
			return false;
		}
		auto target = ContainerMenuUtil::GetAffectedTarget(menu);
		if (!target) {
			if (PluginSettings::Get().corpseEquipMode.enable) {
				auto corpse = ContainerMenuUtil::ResolveActorHandle(menu->GetTargetRefHandle());
				if (!corpse || corpse->IsPlayerRef() || !corpse->IsDead()) {
					return false;
				}
				// kNone corpses cannot hold items.
				if (!ActorScope::HandEquipAllowed(corpse.get())) {
					return false;
				}
			} else {
				return false;
			}
		}
		auto& root = menu->GetRuntimeData().root;
		if (!root.IsObject()) {
			return false;
		}
		if (!IsSkyUiPresent(root)) {
			return false;
		}
		if (IsSkyUiPcPlatform(root) && IsPlayerEquipModeActive(root)) {
			return false;
		}
		return true;
	}
}
