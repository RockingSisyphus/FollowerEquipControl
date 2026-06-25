#include "ZeroWeightTakeAllFix.h"

#include "PluginSettings.h"
#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "KnownFollowerState.h"

#include <cstdint>
#include <initializer_list>

namespace FEC::EquipMode::Fixes
{
	namespace
	{
		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_postDisplayHandle{ 0 };

		// Small enough to display as 0.0, but non-zero to bypass SkyUI's == 0 branch.
		constexpr double kWeightEpsilon = 0.000001;

		[[nodiscard]] bool TryGetNestedNumber(const RE::GFxValue& root, std::initializer_list<const char*> path, double& out)
		{
			RE::GFxValue cur = root;
			for (const auto* key : path) {
				// GFxValue::GetMember only asserts in debug; guard null/non-object intermediates explicitly.
				if (!cur.IsObject() && !cur.IsDisplayObject()) {
					return false;
				}
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

		[[nodiscard]] bool IsItemSelected(const RE::GFxValue& root)
		{
			double selectedIndex{};
			if (!TryGetNestedNumber(root, { "inventoryLists", "itemList", "selectedIndex" }, selectedIndex)) {
				return false;
			}
			return static_cast<std::int32_t>(selectedIndex) != -1;
		}

		[[nodiscard]] bool IsViewingContainerSide(const RE::GFxValue& root)
		{
			// SkyUI ContainerMenu.as: isViewingContainer() uses inventoryLists.categoryList.activeSegment == 0.
			double activeSegment{};
			if (!TryGetNestedNumber(root, { "inventoryLists", "categoryList", "activeSegment" }, activeSegment)) {
				return false;
			}
			return static_cast<std::int32_t>(activeSegment) == 0;
		}

		[[nodiscard]] bool TrySetSelectedItemWeightEpsilon(RE::ContainerMenu* menu)
		{
			if (!menu) {
				return false;
			}

			auto& root = menu->GetRuntimeData().root;
			if (!root.IsObject()) {
				return false;
			}

			if (!IsViewingContainerSide(root)) {
				return false;
			}
			if (!IsItemSelected(root)) {
				return false;
			}

			double weight{};
			if (!TryGetNestedNumber(root, { "itemCard", "itemInfo", "weight" }, weight)) {
				return false;
			}

			// Only exact-zero weights trigger SkyUI's special case.
			if (weight != 0.0) {
				return false;
			}

			RE::GFxValue itemCard;
			if (!root.GetMember("itemCard", &itemCard) || !itemCard.IsObject()) {
				return false;
			}
			RE::GFxValue itemInfo;
			if (!itemCard.GetMember("itemInfo", &itemInfo) || !itemInfo.IsObject()) {
				return false;
			}

			return itemInfo.SetMember("weight", RE::GFxValue(kWeightEpsilon));
		}

		[[nodiscard]] bool ShouldApplyFix(RE::ContainerMenu* menu)
		{
			if (!menu) {
				return false;
			}

			if (ContainerMenuUtil::GetAffectedTarget(menu)) {
				return true;
			}

			// GetAffectedTarget rejects dead actors, but Corpse Equip Mode still needs this fix.
			if (PluginSettings::Get().corpseEquipMode.enable) {
				auto corpse = ContainerMenuUtil::ResolveActorHandle(menu->GetTargetRefHandle());
				if (corpse && !corpse->IsPlayerRef() && corpse->IsDead()) {
					return true;
				}
			}

			return false;
		}
	}

	void ZeroWeightTakeAllFix::Install()
	{
		if (g_installed) {
			return;
		}

		{
			const auto& cfg = PluginSettings::Get();
			if (!cfg.skyUIFixes.enableZeroWeightFix) {
				logger::info("ZeroWeightTakeAllFix: disabled by config");
				return;
			}
		}

		ContainerMenuDisplayHook::Install();
		g_postDisplayHandle = ContainerMenuDisplayHook::AddPostDisplayListener([](RE::ContainerMenu* menu) {
			if (!ShouldApplyFix(menu)) {
				return;
			}

			const auto ok = TrySetSelectedItemWeightEpsilon(menu);
			if (!ok) {
				// Failure is expected on non-SkyUI menus or before selection state is ready.
				static std::uint32_t s_failCount = 0;
				if ((s_failCount++ % 600) == 0) {
					logger::debug("ZeroWeightTakeAllFix: could not patch itemInfo.weight (menu state not ready)");
				}
			}
		});

		g_installed = true;
		logger::info("ZeroWeightTakeAllFix: installed (SkyUI weight==0 take-all bypass)");
	}

	void ZeroWeightTakeAllFix::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_postDisplayHandle) {
			ContainerMenuDisplayHook::UnregisterListener(g_postDisplayHandle);
			g_postDisplayHandle = 0;
		}

		g_installed = false;
		logger::info("ZeroWeightTakeAllFix: uninstalled");
	}
}
