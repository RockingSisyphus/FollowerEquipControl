#include "FeatureRegistry.h"

#include "AttachAshPileFunctorHook.h"
#include "FormDeleteEventSink.h"
#include "ContainerMenuOpenActions.h"
#include "EquipModeKeyOverride.h"
#include "FollowerEquipModeUIIndicator.h"
#include "DisplayFollowerStatsInTradeMenu.h"
#include "MenuFrameworkSettings.h"
#include "PluginSettings.h"
#include "Controls.h"
#include "Router.h"

#include "WeaponEnchantmentRecharge.h"
#include "WeaponEnchantmentRechargeUIIndicator.h"

#include "CombatEquipScore.h"
#include "PreCombatEquipRestore.h"
#include "HeadgearAutoEquip.h"
#include "CombatHeadgearIconInjector.h"
#include "CombatEquipIconInjector.h"
#include "HandItemIconInjector.h"
#include "OutfitSyncIconInjector.h"
#include "InfiniteAmmo.h"

#include "QuickTrade.h"

#include "ContainerLootBlocker.h"
#include "CorpseLootBlocker.h"
#include "PickUpObjectBlocker.h"
#include "CombatLootBlocker.h"
#include "EquipGate.h"

#include "BestWeaponAutoEquipSuppressor.h"
#include "HandItemRestore.h"
#include "NonPlayableItemSanitizer.h"
#include "OutfitItemSanitizer.h"
#include "LeveledItemBlocker.h"
#include "OutfitItemBlocker.h"
#include "NpcOutfitUpdateHook.h"
#include "OutfitSnapshotRestore.h"

namespace FEC
{
	namespace FeatureRegistry
	{
		namespace
		{
			bool g_installed{ false };
		}

		void InstallAllFeatures()
		{
			// Safe to call multiple times (also called from multiple SKSE messaging events).
			// Do NOT early-return on a global flag: some hooks/relocations may not be available
			// on the first call (depending on game state / init order). Each feature's Install()
			// is responsible for being idempotent and will retry if it previously failed.

			const bool enabled = PluginSettings::Get().core.enableMod;

			// Always keep the settings UI installed so the user can re-enable the mod in-game.
			MenuFrameworkSettings::Install();

			if (enabled) {
				Controls::Install();
				FormDeleteEventSink::Install();
				AttachAshPileFunctorHook::Install();
				NpcOutfitUpdateHook::Install();
				OutfitSnapshotRestore::Install();
				BestWeaponAutoEquipSuppressor::Install();
				HandItemRestore::Install();
				NonPlayableItemSanitizer::Install();
				OutfitItemSanitizer::Install();
				LeveledItemBlocker::Install();
				OutfitItemBlocker::Install();
				ContainerMenuOpenActions::Install();
				EquipModeKeyOverride::Install();
				DisplayFollowerStatsInTradeMenu::Install();

				EquipMode::Core::Router::Install();
				WeaponEnchantmentRecharge::Install();
				WeaponEnchantmentRechargeUIIndicator::Install();
				FollowerEquipModeUIIndicator::Install();
				CombatEquipScoreController::Install();
				PreCombatEquipRestore::Install();
				HeadgearAutoEquip::Install();
				CombatHeadgearIconInjector::Install();
				CombatEquipIconInjector::Install();
				OutfitSyncIconInjector::Install();
				HandItemIconInjector::Install();
				InfiniteAmmo::Install();
				ContainerLootBlocker::Install();
				CorpseLootBlocker::Install();
				PickUpObjectBlocker::Install();
				CombatLootBlocker::Install();
				EquipGate::Install();
				QuickTrade::Install();
			} else {
				// Uninstall in reverse install order to restore the vtable cleanly.
				QuickTrade::Uninstall();
				EquipGate::Uninstall();
				CombatLootBlocker::Uninstall();
				PickUpObjectBlocker::Uninstall();
				CorpseLootBlocker::Uninstall();
				ContainerLootBlocker::Uninstall();
				InfiniteAmmo::Uninstall();
				HandItemIconInjector::Uninstall();
				OutfitSyncIconInjector::Uninstall();
				CombatEquipIconInjector::Uninstall();
				CombatHeadgearIconInjector::Uninstall();
				HeadgearAutoEquip::Uninstall();
				PreCombatEquipRestore::Uninstall();
				CombatEquipScoreController::Uninstall();
				FollowerEquipModeUIIndicator::Uninstall();
				WeaponEnchantmentRechargeUIIndicator::Uninstall();
				WeaponEnchantmentRecharge::Uninstall();
				EquipMode::Core::Router::Uninstall();

				DisplayFollowerStatsInTradeMenu::Uninstall();
				EquipModeKeyOverride::Uninstall();
				ContainerMenuOpenActions::Uninstall();
				OutfitItemBlocker::Uninstall();
				LeveledItemBlocker::Uninstall();
				OutfitItemSanitizer::Uninstall();
				NonPlayableItemSanitizer::Uninstall();
				HandItemRestore::Uninstall();
				BestWeaponAutoEquipSuppressor::Uninstall();
				OutfitSnapshotRestore::Uninstall();
				NpcOutfitUpdateHook::Uninstall();

				AttachAshPileFunctorHook::Uninstall();
				FormDeleteEventSink::Uninstall();
				Controls::Uninstall();
			}

			g_installed = true;
		}

		void UninstallAllFeatures()
		{
			if (!g_installed) {
				return;
			}

			// Always attempt to uninstall: each feature's Uninstall() should be safe even if not installed.

			// Uninstall in reverse install order to restore the vtable cleanly.
			QuickTrade::Uninstall();
			EquipGate::Uninstall();
			CombatLootBlocker::Uninstall();
			PickUpObjectBlocker::Uninstall();
			CorpseLootBlocker::Uninstall();
			ContainerLootBlocker::Uninstall();
			InfiniteAmmo::Uninstall();
			HandItemIconInjector::Uninstall();
			OutfitSyncIconInjector::Uninstall();
			CombatEquipIconInjector::Uninstall();
			CombatHeadgearIconInjector::Uninstall();
			HeadgearAutoEquip::Uninstall();
			PreCombatEquipRestore::Uninstall();
			CombatEquipScoreController::Uninstall();
			FollowerEquipModeUIIndicator::Uninstall();
			WeaponEnchantmentRechargeUIIndicator::Uninstall();
			WeaponEnchantmentRecharge::Uninstall();
			EquipMode::Core::Router::Uninstall();

			DisplayFollowerStatsInTradeMenu::Uninstall();
			EquipModeKeyOverride::Uninstall();
			ContainerMenuOpenActions::Uninstall();
			OutfitItemBlocker::Uninstall();
			LeveledItemBlocker::Uninstall();
			OutfitItemSanitizer::Uninstall();
			NonPlayableItemSanitizer::Uninstall();
			HandItemRestore::Uninstall();
			BestWeaponAutoEquipSuppressor::Uninstall();
			OutfitSnapshotRestore::Uninstall();
			NpcOutfitUpdateHook::Uninstall();

			AttachAshPileFunctorHook::Uninstall();
			FormDeleteEventSink::Uninstall();
			Controls::Uninstall();

			MenuFrameworkSettings::Uninstall();
			
			g_installed = false;
		}
	}
}
