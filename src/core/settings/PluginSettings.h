// Typed INI-backed plugin settings: the schema plus the load/save/config-access API.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <string>

namespace FEC::PluginSettings
{
	enum class PoisonMode : std::uint8_t
	{
		kDisable,
		kApplyToWeapon,
		kConsume
	};

	enum class ContainedSoulDisplayMode : std::uint8_t
	{
		kDisable,
		kOnlyIfLowerThanCapacity,
		kAlways
	};

	enum class SoulGemSourceDisplayMode : std::uint8_t
	{
		kDisable,
		kFollowerOnly,
		kPlayerOnly,
		kFollowerAndPlayer
	};

	enum class SoulGemSortMode : std::uint8_t
	{
		kName,
		kSoulAsc,
		kSoulDesc
	};

	enum class FollowerStatsInTradeMenuMode : std::uint8_t
	{
		kDisable,
		kFollowerSide,
		kAlways
	};

	struct Core
	{
		bool enableMod{ true };
		bool operator==(const Core&) const = default;
	};

	struct ActorScope
	{
		bool affectFormerFollowers{ true };
		bool includeInclusionActors{ true };
		bool includePlayerSummons{ true };
		bool operator==(const ActorScope&) const = default;
	};

	struct QuickTrade
	{
		bool enableQuickTrade{ true };
		bool enableQuickTradeForFollowers{ true };
		bool enableQuickTradeForFormerFollowers{ true };
		bool enableQuickTradeForInclusionActors{ true };
		bool enableQuickTradeForPlayerSummons{ true };
		bool enableQuickTradeForKNoneActors{ true };
		bool enableQuickTradeForMounts{ true };
		bool quickTradeAllWorldMounts{ false };
		bool operator==(const QuickTrade&) const = default;
	};

	struct UIFeedback
	{
		bool enableNotifications{ true };
		bool enableUISounds{ true };
		bool operator==(const UIFeedback&) const = default;
	};

	struct Logging
	{
		std::string logLevel{ "info" };
		bool operator==(const Logging&) const = default;
	};

	struct KeyboardControls
	{
		std::uint32_t modKeyDik{ 29 };
		std::uint32_t skyuiEquipModeKey{ 0 };
		std::uint32_t quickTradeKeyDik{ 0 };
		bool operator==(const KeyboardControls&) const = default;
	};

	struct GamepadControls
	{
		std::uint32_t gamepadModKey{ 280 };
		std::uint32_t gamepadRightHandKey{ 276 };
		std::uint32_t gamepadLeftHandKey{ 278 };
		std::uint32_t quickTradeGamepadKey{ 0 };
		bool operator==(const GamepadControls&) const = default;
	};

	struct ButtonIndicators
	{
		bool enableModKeyIndicator{ true };
		std::string modKeyIndicatorText{};
		std::string inclusionIndicatorText{};
		std::string summonIndicatorText{};
		std::string equipModeTextOverride{};
		bool operator==(const ButtonIndicators&) const = default;
	};

	struct IconAppearance
	{
		bool enableIconIndicator{ true };
		bool enableCombatEquipIcon{ true };
		bool enableHeadgearIcon{ true };
		bool enableOutfitSyncIcon{ true };
		bool enableHandItemIcon{ true };
		bool enableCustomization{ true };
		double iconSize{ 12.0 };
		double gapAfterText{ 8.0 };
		double gapAfterIcon{ 8.0 };
		double fecIconSpacing{ 8.0 };
		bool operator==(const IconAppearance&) const = default;
	};

	struct EquipModeItems
	{
		bool enable{ true };
		bool enableWeapons{ true };
		bool enableArmor{ true };
		bool enableAmmo{ true };
		bool enableTorches{ true };
		bool enableScrolls{ true };
		bool operator==(const EquipModeItems&) const = default;
	};

	struct EquipModeConsumableMode
	{
		bool enableConsumableMode{ true };
		bool enablePotions{ true };
		bool enableFoods{ true };
		bool enableDrinks{ true };
		bool enableIngredients{ true };
		bool enableIngredientDiscoveryForPlayer{ false };
		bool operator==(const EquipModeConsumableMode&) const = default;
	};

	struct EquipModePoisonMode
	{
		PoisonMode mode{ PoisonMode::kApplyToWeapon };
		bool enableStacking{ false };
		std::uint32_t maxCharges{ 100 };
		bool operator==(const EquipModePoisonMode&) const = default;
	};

	struct EquipModeSpellTomeMode
	{
		bool enableSpellTomeMode{ true };
		bool doNotConsumeSpellTomes{ false };
		bool operator==(const EquipModeSpellTomeMode&) const = default;
	};

	struct CombatEquipPreference
	{
		bool enableScoring{ true };
		bool enableInstanceAlign{ true };
		bool enableClearPreferencesOnUnequip{ false };
		bool enableClearLeftHandPreferenceOnUnequip{ false };
		bool operator==(const CombatEquipPreference&) const = default;
	};

	struct CombatEquipEnforcement
	{
		bool enableMeleeEnforcement{ true };
		bool enableMeleeEnforcementEmptyHand{ false };
		bool enableAmmoPreference{ true };
		bool operator==(const CombatEquipEnforcement&) const = default;
	};

	struct CombatEquipRestore
	{
		bool enableRestorePreCombatOnExit{ true };
		bool enableHeadgearAutoEquip{ false };
		bool enableInfiniteAmmo{ false };
		bool operator==(const CombatEquipRestore&) const = default;
	};

	struct OutfitSync
	{
		bool enableUpdateNpcOutfitSuppression{ true };
		bool enableOutfitSnapshotRestore{ true };
		bool allowOutfitChanges{ true };
		bool operator==(const OutfitSync&) const = default;
	};

	struct HiddenItems
	{
		bool enableNonPlayableItems{ true };
		bool removeHiddenArmor{ false };
		bool removeHiddenWeapon{ true };
		bool removeHiddenAmmo{ true };
		bool enableOutfitItems{ true };
		bool enableRevealDefaultOutfitItems{ true };
		bool enableRevealExternalOutfitItems{ true };
		bool operator==(const HiddenItems&) const = default;
	};

	struct ItemInjectionBlocking
	{
		bool enableOutfitItemBlocker{ false };
		bool enableLeveledItemBlocker{ false };
		bool operator==(const ItemInjectionBlocking&) const = default;
	};

	struct LootBlocking
	{
		bool enablePreventCombatLoot{ true };
		bool enablePreventContainerLoot{ false };
		bool enablePreventPickupObject{ false };
		bool operator==(const LootBlocking&) const = default;
	};

	struct AutoEquipBlocking
	{
		bool enableNonCombatEquipBlocker{ true };
		bool enableHandItemRestore{ true };
		bool enableBestWeaponAutoEquipSuppressor{ true };
		bool operator==(const AutoEquipBlocking&) const = default;
	};

	struct EquipGate
	{
		bool enableEquipBlocking{ false };
		bool enableUnequipBlocking{ false };
		bool enableNeverBlockTorchEquip{ true };
		bool operator==(const EquipGate&) const = default;
	};

	struct WeaponEnchantmentRecharge
	{
		bool enableRecharge{ true };
		std::uint32_t rechargeKeyDik{ 20 };
		std::uint32_t gamepadRechargeKey{ 279 };
		bool enableUIIndicator{ true };
		bool advancedSoulGemDisplay{ true };
		bool showStackCount{ true };
		ContainedSoulDisplayMode containedSoulDisplayMode{ ContainedSoulDisplayMode::kOnlyIfLowerThanCapacity };
		SoulGemSourceDisplayMode sourceDisplayMode{ SoulGemSourceDisplayMode::kDisable };
		SoulGemSortMode soulGemSortMode{ SoulGemSortMode::kSoulAsc };
		bool operator==(const WeaponEnchantmentRecharge&) const = default;
	};

	struct StatsDisplay
	{
		FollowerStatsInTradeMenuMode followerStatsInTradeMenuMode{ FollowerStatsInTradeMenuMode::kFollowerSide };
		bool operator==(const StatsDisplay&) const = default;
	};

	struct CorpseEquipMode
	{
		bool enable{ true };
		std::string indicatorText{};
		bool operator==(const CorpseEquipMode&) const = default;
	};

	struct EngineFixes
	{
		bool enableStaleWeightCacheFix{ true };
		bool operator==(const EngineFixes&) const = default;
	};

	struct SkyUIFixes
	{
		bool enableQuantityMenuBlocker{ true };
		bool enableZeroWeightFix{ true };
		bool operator==(const SkyUIFixes&) const = default;
	};

	struct Settings
	{
		Core core{};
		UIFeedback uiFeedback{};
		Logging logging{};
		ActorScope actorScope{};
		QuickTrade quickTrade{};
		KeyboardControls keyboardControls{};
		GamepadControls gamepadControls{};
		ButtonIndicators buttonIndicators{};
		IconAppearance iconAppearance{};
		EquipModeItems equipModeItems{};
		EquipModeConsumableMode equipModeConsumableMode{};
		EquipModePoisonMode equipModePoisonMode{};
		EquipModeSpellTomeMode equipModeSpellTomeMode{};
		CombatEquipPreference combatEquipPreference{};
		CombatEquipEnforcement combatEquipEnforcement{};
		CombatEquipRestore combatEquipRestore{};
		OutfitSync outfitSync{};
		HiddenItems hiddenItems{};
		ItemInjectionBlocking itemInjectionBlocking{};
		LootBlocking lootBlocking{};
		AutoEquipBlocking autoEquipBlocking{};
		EquipGate equipGate{};
		WeaponEnchantmentRecharge weaponEnchantmentRecharge{};
		StatsDisplay statsDisplay{};
		CorpseEquipMode corpseEquipMode{};
		EngineFixes engineFixes{};
		SkyUIFixes skyUIFixes{};
		bool operator==(const Settings&) const = default;
	};

	void Load();

	[[nodiscard]] const Settings& Get();

	void Set(const Settings& a_settings);

	[[nodiscard]] bool Save(const Settings& a_settings);
	[[nodiscard]] bool Save();
}
