#include "PluginSettings.h"

#include "Plugin.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

namespace FEC::PluginSettings
{
	namespace
	{
		inline constexpr std::string_view kRootSection = "";

		Settings g_settings{};
		bool g_loaded{ false };

		[[nodiscard]] std::string ToLower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		[[nodiscard]] std::string Trim(std::string s)
		{
			auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
			while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) {
				s.erase(s.begin());
			}
			while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) {
				s.pop_back();
			}
			return s;
		}

		[[nodiscard]] std::optional<std::filesystem::path> GetThisDllDirectory()
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&FEC::PluginSettings::Load),
				&module)) {
				return std::nullopt;
			}

			wchar_t buf[MAX_PATH]{};
			const auto len = GetModuleFileNameW(module, buf, static_cast<DWORD>(std::size(buf)));
			if (len == 0) {
				return std::nullopt;
			}

			std::filesystem::path dllPath{ buf };
			if (!dllPath.has_parent_path()) {
				return std::nullopt;
			}

			return dllPath.parent_path();
		}

		[[nodiscard]] std::optional<std::filesystem::path> GetIniPathNearDll()
		{
			auto dllDir = GetThisDllDirectory();
			if (!dllDir) {
				return std::nullopt;
			}
			return *dllDir / (std::string(Plugin::NAME) + ".ini");
		}

		using SectionMap = std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

		[[nodiscard]] SectionMap ParseIni(std::istream& in)
		{
			SectionMap out;
			std::string currentSection{ std::string(kRootSection) };

			std::string line;
			while (std::getline(in, line)) {
				line = Trim(std::move(line));
				if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
					continue;
				}

				if (line.front() == '[' && line.back() == ']') {
					auto section = Trim(line.substr(1, line.size() - 2));
					currentSection = ToLower(std::move(section));
					continue;
				}

				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}

				auto key = Trim(line.substr(0, eq));
				auto val = Trim(line.substr(eq + 1));
				key = ToLower(std::move(key));
				out[currentSection][std::move(key)] = std::move(val);
			}

			return out;
		}

		[[nodiscard]] std::optional<std::string> GetValue(const SectionMap& ini, std::string_view section, std::string_view key)
		{
			auto secIt = ini.find(ToLower(std::string(section)));
			if (secIt == ini.end()) {
				return std::nullopt;
			}
			auto keyIt = secIt->second.find(ToLower(std::string(key)));
			if (keyIt == secIt->second.end()) {
				return std::nullopt;
			}
			return keyIt->second;
		}

		[[nodiscard]] PoisonMode ParsePoisonMode(std::string s, PoisonMode fallback)
		{
			s = ToLower(Trim(std::move(s)));
			if (s == "off" || s == "disabled" || s == "disable" || s == "0" || s == "none") {
				return PoisonMode::kDisable;
			}
			if (s == "applytoweapon" || s == "apply_to_weapon" || s == "apply" || s == "weapon" || s == "applyweapon" || s == "apply_weapon" || s == "1") {
				return PoisonMode::kApplyToWeapon;
			}
			if (s == "consume" || s == "drink" || s == "2") {
				return PoisonMode::kConsume;
			}
			return fallback;
		}

		[[nodiscard]] ContainedSoulDisplayMode ParseContainedSoulDisplayMode(std::string s, ContainedSoulDisplayMode fallback)
		{
			s = ToLower(Trim(std::move(s)));
			if (s == "off" || s == "disabled" || s == "disable" || s == "0" || s == "none") {
				return ContainedSoulDisplayMode::kDisable;
			}
			if (s == "onlyiflowerthancapacity" || s == "only_if_lower_than_capacity" || s == "only_if_lower" || s == "lower_only" || s == "lower" || s == "auto" || s == "1") {
				return ContainedSoulDisplayMode::kOnlyIfLowerThanCapacity;
			}
			if (s == "always" || s == "2") {
				return ContainedSoulDisplayMode::kAlways;
			}
			return fallback;
		}

		[[nodiscard]] SoulGemSourceDisplayMode ParseSoulGemSourceDisplayMode(std::string s, SoulGemSourceDisplayMode fallback)
		{
			s = ToLower(Trim(std::move(s)));
			if (s == "off" || s == "disabled" || s == "disable" || s == "0" || s == "none") {
				return SoulGemSourceDisplayMode::kDisable;
			}
			if (s == "follower" || s == "followeronly" || s == "follower_only" || s == "1") {
				return SoulGemSourceDisplayMode::kFollowerOnly;
			}
			if (s == "player" || s == "playeronly" || s == "player_only" || s == "2") {
				return SoulGemSourceDisplayMode::kPlayerOnly;
			}
			if (s == "both" || s == "followerandplayer" || s == "follower_and_player" || s == "follower&player" || s == "follower+player" || s == "3") {
				return SoulGemSourceDisplayMode::kFollowerAndPlayer;
			}
			return fallback;
		}

		[[nodiscard]] SoulGemSortMode ParseSoulGemSortMode(std::string s, SoulGemSortMode fallback)
		{
			s = ToLower(Trim(std::move(s)));
			if (s == "name" || s == "by_name" || s == "sortbyname" || s == "vanilla" || s == "0") {
				return SoulGemSortMode::kName;
			}
			if (s == "soul_asc" || s == "soulasc" || s == "asc" || s == "1") {
				return SoulGemSortMode::kSoulAsc;
			}
			if (s == "soul_desc" || s == "souldesc" || s == "desc" || s == "2") {
				return SoulGemSortMode::kSoulDesc;
			}
			return fallback;
		}

		[[nodiscard]] FollowerStatsInTradeMenuMode ParseFollowerStatsInTradeMenuMode(std::string s, FollowerStatsInTradeMenuMode fallback)
		{
			s = ToLower(Trim(std::move(s)));

			if (s == "always" || s == "both" || s == "all" || s == "2") {
				return FollowerStatsInTradeMenuMode::kAlways;
			}
			if (s == "follower" || s == "follower_side" || s == "followerside" || s == "followeronly" ||
				s == "take" || s == "take_only" || s == "takeonly" || s == "1") {
				return FollowerStatsInTradeMenuMode::kFollowerSide;
			}
			if (s == "off" || s == "disabled" || s == "disable" || s == "0" || s == "none") {
				return FollowerStatsInTradeMenuMode::kDisable;
			}

			return fallback;
		}

		[[nodiscard]] std::uint32_t ParseU32(std::string s, std::uint32_t fallback)
		{
			s = Trim(std::move(s));
			try {
				std::size_t idx = 0;
				const auto v = std::stoul(s, &idx, 10);
				if (idx != s.size()) {
					return fallback;
				}
				if (v > std::numeric_limits<std::uint32_t>::max()) {
					return fallback;
				}
				return static_cast<std::uint32_t>(v);
			} catch (...) {
				return fallback;
			}
		}

		[[nodiscard]] double ParseDouble(std::string s, double fallback)
		{
			s = Trim(std::move(s));
			try {
				std::size_t idx = 0;
				const auto v = std::stod(s, &idx);
				if (idx != s.size()) {
					return fallback;
				}
				if (!std::isfinite(v)) {
					return fallback;
				}
				return v;
			} catch (...) {
				return fallback;
			}
		}

		[[nodiscard]] std::uint32_t ParseHexColor(std::string s, std::uint32_t fallback)
		{
			s = Trim(std::move(s));
			if (!s.empty() && s.front() == '#') {
				s.erase(s.begin());
			} else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
				s.erase(0, 2);
			}
			if (s.empty() || s.size() > 6) {
				return fallback;
			}
			try {
				std::size_t idx = 0;
				const auto v = std::stoul(s, &idx, 16);
				if (idx != s.size()) {
					return fallback;
				}
				if (v > 0xFFFFFF) {
					return fallback;
				}
				return static_cast<std::uint32_t>(v);
			} catch (...) {
				return fallback;
			}
		}

		[[nodiscard]] bool ParseBool(std::string s, bool fallback)
		{
			s = ToLower(Trim(std::move(s)));
			if (s == "1" || s == "true" || s == "yes" || s == "y" || s == "on" || s == "enable" || s == "enabled") {
				return true;
			}
			if (s == "0" || s == "false" || s == "no" || s == "n" || s == "off" || s == "disable" || s == "disabled") {
				return false;
			}
			return fallback;
		}

		void LoadCore(const SectionMap& ini, Core& out)
		{
			if (auto v = GetValue(ini, "core", "enablemod"); v) {
				out.enableMod = ParseBool(*v, out.enableMod);
			}
		}

		void LoadUIFeedback(const SectionMap& ini, UIFeedback& out)
		{
			if (auto v = GetValue(ini, "uifeedback", "enablenotifications"); v) {
				out.enableNotifications = ParseBool(*v, out.enableNotifications);
			}
			if (auto v = GetValue(ini, "uifeedback", "enableuisounds"); v) {
				out.enableUISounds = ParseBool(*v, out.enableUISounds);
			}
		}

		void LoadLogging(const SectionMap& ini, Logging& out)
		{
			if (auto v = GetValue(ini, "logging", "loglevel"); v) {
				auto s = ToLower(Trim(*v));
				if (!s.empty()) {
					out.logLevel = std::move(s);
				}
			}
		}

		void LoadActorScope(const SectionMap& ini, ActorScope& out)
		{
			if (auto v = GetValue(ini, "actorscope", "affectformerfollowers"); v) {
				out.affectFormerFollowers = ParseBool(*v, out.affectFormerFollowers);
			}
			if (auto v = GetValue(ini, "actorscope", "includeinclusionactors"); v) {
				out.includeInclusionActors = ParseBool(*v, out.includeInclusionActors);
			}
			if (auto v = GetValue(ini, "actorscope", "includenonhumanoidinclusionactors"); v) {
				out.includeNonHumanoidInclusionActors = ParseBool(*v, out.includeNonHumanoidInclusionActors);
			}
			if (auto v = GetValue(ini, "actorscope", "includeplayersummons"); v) {
				out.includePlayerSummons = ParseBool(*v, out.includePlayerSummons);
			}
			if (auto v = GetValue(ini, "actorscope", "includenonhumanoidsummons"); v) {
				out.includeNonHumanoidSummons = ParseBool(*v, out.includeNonHumanoidSummons);
			}
		}

		void LoadQuickTrade(const SectionMap& ini, QuickTrade& out)
		{
			if (auto v = GetValue(ini, "quicktrade", "enablequicktrade"); v) {
				out.enableQuickTrade = ParseBool(*v, out.enableQuickTrade);
			}
			if (auto v = GetValue(ini, "quicktrade", "enablequicktradeforfollowers"); v) {
				out.enableQuickTradeForFollowers = ParseBool(*v, out.enableQuickTradeForFollowers);
			}
			if (auto v = GetValue(ini, "quicktrade", "enablequicktradeforformerfollowers"); v) {
				out.enableQuickTradeForFormerFollowers = ParseBool(*v, out.enableQuickTradeForFormerFollowers);
			}
			if (auto v = GetValue(ini, "quicktrade", "enablequicktradeforinclusionactors"); v) {
				out.enableQuickTradeForInclusionActors = ParseBool(*v, out.enableQuickTradeForInclusionActors);
			}
			if (auto v = GetValue(ini, "quicktrade", "enablequicktradeforplayersummons"); v) {
				out.enableQuickTradeForPlayerSummons = ParseBool(*v, out.enableQuickTradeForPlayerSummons);
			}
			if (auto v = GetValue(ini, "quicktrade", "enablequicktradeforknoneactors"); v) {
				out.enableQuickTradeForKNoneActors = ParseBool(*v, out.enableQuickTradeForKNoneActors);
			}
			if (auto v = GetValue(ini, "quicktrade", "enablequicktradeformounts"); v) {
				out.enableQuickTradeForMounts = ParseBool(*v, out.enableQuickTradeForMounts);
			}
			if (auto v = GetValue(ini, "quicktrade", "quicktradeallworldmounts"); v) {
				out.quickTradeAllWorldMounts = ParseBool(*v, out.quickTradeAllWorldMounts);
			}
		}

		void LoadKeyboardControls(const SectionMap& ini, KeyboardControls& out)
		{
			if (auto v = GetValue(ini, "keyboardcontrols", "modkeydik"); v) {
				out.modKeyDik = ParseU32(*v, out.modKeyDik);
			}
			if (auto v = GetValue(ini, "keyboardcontrols", "skyuiequipmodekey"); v) {
				out.skyuiEquipModeKey = ParseU32(*v, out.skyuiEquipModeKey);
			}
			if (auto v = GetValue(ini, "keyboardcontrols", "quicktradekeydik"); v) {
				out.quickTradeKeyDik = ParseU32(*v, out.quickTradeKeyDik);
			}
		}

		void LoadGamepadControls(const SectionMap& ini, GamepadControls& out)
		{
			if (auto v = GetValue(ini, "gamepadcontrols", "gamepadmodkey"); v) {
				out.gamepadModKey = ParseU32(*v, out.gamepadModKey);
			}
			if (auto v = GetValue(ini, "gamepadcontrols", "gamepadrighthandkey"); v) {
				out.gamepadRightHandKey = ParseU32(*v, out.gamepadRightHandKey);
			}
			if (auto v = GetValue(ini, "gamepadcontrols", "gamepadlefthandkey"); v) {
				out.gamepadLeftHandKey = ParseU32(*v, out.gamepadLeftHandKey);
			}
			if (auto v = GetValue(ini, "gamepadcontrols", "quicktradegamepadkey"); v) {
				out.quickTradeGamepadKey = ParseU32(*v, out.quickTradeGamepadKey);
			}
		}

		void LoadButtonIndicators(const SectionMap& ini, ButtonIndicators& out)
		{
			if (auto v = GetValue(ini, "buttonindicators", "enablemodkeyindicator"); v) {
				out.enableModKeyIndicator = ParseBool(*v, out.enableModKeyIndicator);
			}

			if (auto v = GetValue(ini, "buttonindicators", "modkeyindicatortext"); v) {
				auto s = Trim(*v);
				if (!s.empty()) {
					out.modKeyIndicatorText = std::move(s);
				}
			}

			if (auto v = GetValue(ini, "buttonindicators", "inclusionindicatortext"); v) {
				auto s = Trim(*v);
				if (!s.empty()) {
					out.inclusionIndicatorText = std::move(s);
				}
			}

			if (auto v = GetValue(ini, "buttonindicators", "summonindicatortext"); v) {
				auto s = Trim(*v);
				if (!s.empty()) {
					out.summonIndicatorText = std::move(s);
				}
			}

			if (auto v = GetValue(ini, "buttonindicators", "equipmodetextoverride"); v) {
				auto s = Trim(*v);
				if (!s.empty()) {
					out.equipModeTextOverride = std::move(s);
				}
			}
		}

		void LoadIconAppearance(const SectionMap& ini, IconAppearance& out)
		{
			if (auto v = GetValue(ini, "iconappearance", "enableiconindicator"); v) {
				out.enableIconIndicator = ParseBool(*v, out.enableIconIndicator);
			}
			if (auto v = GetValue(ini, "iconappearance", "enablecombatequipicon"); v) {
				out.enableCombatEquipIcon = ParseBool(*v, out.enableCombatEquipIcon);
			}
			if (auto v = GetValue(ini, "iconappearance", "enableheadgearicon"); v) {
				out.enableHeadgearIcon = ParseBool(*v, out.enableHeadgearIcon);
			}
			if (auto v = GetValue(ini, "iconappearance", "enableoutfitsyncicon"); v) {
				out.enableOutfitSyncIcon = ParseBool(*v, out.enableOutfitSyncIcon);
			}
			if (auto v = GetValue(ini, "iconappearance", "enablecustomization"); v) {
				out.enableCustomization = ParseBool(*v, out.enableCustomization);
			}
			if (auto v = GetValue(ini, "iconappearance", "iconsize"); v) {
				out.iconSize = ParseDouble(*v, out.iconSize);
			}
			if (auto v = GetValue(ini, "iconappearance", "gapaftertext"); v) {
				out.gapAfterText = ParseDouble(*v, out.gapAfterText);
			}
			if (auto v = GetValue(ini, "iconappearance", "gapaftericon"); v) {
				out.gapAfterIcon = ParseDouble(*v, out.gapAfterIcon);
			}
			if (auto v = GetValue(ini, "iconappearance", "feciconspacing"); v) {
				out.fecIconSpacing = ParseDouble(*v, out.fecIconSpacing);
			}
		}

		void LoadEquipModeItems(const SectionMap& ini, EquipModeItems& out)
		{
			auto get = [&](std::string_view a_key) -> std::optional<std::string> {
				return GetValue(ini, "equipmodeitems", a_key);
			};

			if (auto v = get("enable"); v) {
				out.enable = ParseBool(*v, out.enable);
			}

			if (auto v = get("enableweapons"); v) {
				out.enableWeapons = ParseBool(*v, out.enableWeapons);
			}
			if (auto v = get("enablearmor"); v) {
				out.enableArmor = ParseBool(*v, out.enableArmor);
			}
			if (auto v = get("enableammo"); v) {
				out.enableAmmo = ParseBool(*v, out.enableAmmo);
			}
			if (auto v = get("enabletorches"); v) {
				out.enableTorches = ParseBool(*v, out.enableTorches);
			}
			if (auto v = get("enablescrolls"); v) {
				out.enableScrolls = ParseBool(*v, out.enableScrolls);
			}
		}

		void LoadEquipModeConsumableMode(const SectionMap& ini, EquipModeConsumableMode& out)
		{
			if (auto v = GetValue(ini, "equipmodeconsumablemode", "enableconsumablemode"); v) {
				out.enableConsumableMode = ParseBool(*v, out.enableConsumableMode);
			}
			if (auto v = GetValue(ini, "equipmodeconsumablemode", "enablepotions"); v) {
				out.enablePotions = ParseBool(*v, out.enablePotions);
			}
			if (auto v = GetValue(ini, "equipmodeconsumablemode", "enablefoods"); v) {
				out.enableFoods = ParseBool(*v, out.enableFoods);
			}
			if (auto v = GetValue(ini, "equipmodeconsumablemode", "enabledrinks"); v) {
				out.enableDrinks = ParseBool(*v, out.enableDrinks);
			}
			if (auto v = GetValue(ini, "equipmodeconsumablemode", "enableingredients"); v) {
				out.enableIngredients = ParseBool(*v, out.enableIngredients);
			}
			if (auto v = GetValue(ini, "equipmodeconsumablemode", "enableingredientdiscoveryforplayer"); v) {
				out.enableIngredientDiscoveryForPlayer = ParseBool(*v, out.enableIngredientDiscoveryForPlayer);
			}
		}

		void LoadEquipModePoisonMode(const SectionMap& ini, EquipModePoisonMode& out)
		{
			if (auto v = GetValue(ini, "equipmodepoisonmode", "mode"); v) {
				out.mode = ParsePoisonMode(*v, out.mode);
			}
			if (auto v = GetValue(ini, "equipmodepoisonmode", "enablepoisonstacking"); v) {
				out.enableStacking = ParseBool(*v, out.enableStacking);
			}
			if (auto v = GetValue(ini, "equipmodepoisonmode", "maxpoisoncharges"); v) {
				out.maxCharges = ParseU32(*v, out.maxCharges);
				if (out.maxCharges < 1u) { out.maxCharges = 1u; }
				if (out.maxCharges > 100u) { out.maxCharges = 100u; }
			}
		}

		void LoadEquipModeSpellTomeMode(const SectionMap& ini, EquipModeSpellTomeMode& out)
		{
			if (auto v = GetValue(ini, "equipmodespelltomemode", "enablespelltomemode"); v) {
				out.enableSpellTomeMode = ParseBool(*v, out.enableSpellTomeMode);
			}
		}

		void LoadCombatEquipPreference(const SectionMap& ini, CombatEquipPreference& out)
		{
			if (auto v = GetValue(ini, "combatequippreference", "enablescoring"); v) {
				out.enableScoring = ParseBool(*v, out.enableScoring);
			}
			if (auto v = GetValue(ini, "combatequippreference", "enableinstancealign"); v) {
				out.enableInstanceAlign = ParseBool(*v, out.enableInstanceAlign);
			}
			if (auto v = GetValue(ini, "combatequippreference", "enableclearpreferencesonunequip"); v) {
				out.enableClearPreferencesOnUnequip = ParseBool(*v, out.enableClearPreferencesOnUnequip);
			}
			if (auto v = GetValue(ini, "combatequippreference", "enableclearlefthandpreferenceonunequip"); v) {
				out.enableClearLeftHandPreferenceOnUnequip = ParseBool(*v, out.enableClearLeftHandPreferenceOnUnequip);
			}
		}

		void LoadCombatEquipEnforcement(const SectionMap& ini, CombatEquipEnforcement& out)
		{
			if (auto v = GetValue(ini, "combatequipenforcement", "enablemeleeenforcement"); v) {
				out.enableMeleeEnforcement = ParseBool(*v, out.enableMeleeEnforcement);
			}
			if (auto v = GetValue(ini, "combatequipenforcement", "enablemeleeenforcementemptyhand"); v) {
				out.enableMeleeEnforcementEmptyHand = ParseBool(*v, out.enableMeleeEnforcementEmptyHand);
			}
			if (auto v = GetValue(ini, "combatequipenforcement", "enableammopreference"); v) {
				out.enableAmmoPreference = ParseBool(*v, out.enableAmmoPreference);
			}
		}

		void LoadCombatEquipRestore(const SectionMap& ini, CombatEquipRestore& out)
		{
			if (auto v = GetValue(ini, "combatequiprestore", "enablerestoreprecombatonexit"); v) {
				out.enableRestorePreCombatOnExit = ParseBool(*v, out.enableRestorePreCombatOnExit);
			}
			if (auto v = GetValue(ini, "combatequiprestore", "enableheadgearautoequip"); v) {
				out.enableHeadgearAutoEquip = ParseBool(*v, out.enableHeadgearAutoEquip);
			}
			if (auto v = GetValue(ini, "combatequiprestore", "enableinfiniteammo"); v) {
				out.enableInfiniteAmmo = ParseBool(*v, out.enableInfiniteAmmo);
			}
		}

		void LoadOutfitSync(const SectionMap& ini, OutfitSync& out)
		{
			if (auto v = GetValue(ini, "outfitsync", "enableupdatenpcoutfitsuppression"); v) {
				out.enableUpdateNpcOutfitSuppression = ParseBool(*v, out.enableUpdateNpcOutfitSuppression);
			}
			if (auto v = GetValue(ini, "outfitsync", "enableoutfitsnapshotrestore"); v) {
				out.enableOutfitSnapshotRestore = ParseBool(*v, out.enableOutfitSnapshotRestore);
			}
			if (auto v = GetValue(ini, "outfitsync", "allowoutfitchanges"); v) {
				out.allowOutfitChanges = ParseBool(*v, out.allowOutfitChanges);
			}
		}

		void LoadHiddenItems(const SectionMap& ini, HiddenItems& out)
		{
			if (auto v = GetValue(ini, "hiddenitems", "enablenonplayableitems"); v) {
				out.enableNonPlayableItems = ParseBool(*v, out.enableNonPlayableItems);
			}
			if (auto v = GetValue(ini, "hiddenitems", "removehiddenarmor"); v) {
				out.removeHiddenArmor = ParseBool(*v, out.removeHiddenArmor);
			}
			if (auto v = GetValue(ini, "hiddenitems", "removehiddenweapon"); v) {
				out.removeHiddenWeapon = ParseBool(*v, out.removeHiddenWeapon);
			}
			if (auto v = GetValue(ini, "hiddenitems", "removehiddenammo"); v) {
				out.removeHiddenAmmo = ParseBool(*v, out.removeHiddenAmmo);
			}
			if (auto v = GetValue(ini, "hiddenitems", "enableoutfititems"); v) {
				out.enableOutfitItems = ParseBool(*v, out.enableOutfitItems);
			}
			if (auto v = GetValue(ini, "hiddenitems", "enablerevealdefaultoutfititems"); v) {
				out.enableRevealDefaultOutfitItems = ParseBool(*v, out.enableRevealDefaultOutfitItems);
			}
			if (auto v = GetValue(ini, "hiddenitems", "enablerevealexternaloutfititems"); v) {
				out.enableRevealExternalOutfitItems = ParseBool(*v, out.enableRevealExternalOutfitItems);
			}
		}

		void LoadItemInjectionBlocking(const SectionMap& ini, ItemInjectionBlocking& out)
		{
			if (auto v = GetValue(ini, "iteminjectionblocking", "enableoutfititemblocker"); v) {
				out.enableOutfitItemBlocker = ParseBool(*v, out.enableOutfitItemBlocker);
			}
			if (auto v = GetValue(ini, "iteminjectionblocking", "enableleveleditemblocker"); v) {
				out.enableLeveledItemBlocker = ParseBool(*v, out.enableLeveledItemBlocker);
			}
		}

		void LoadLootBlocking(const SectionMap& ini, LootBlocking& out)
		{
			if (auto v = GetValue(ini, "lootblocking", "enablepreventcombatloot"); v) {
				out.enablePreventCombatLoot = ParseBool(*v, out.enablePreventCombatLoot);
			}
			if (auto v = GetValue(ini, "lootblocking", "enablepreventcontainerloot"); v) {
				out.enablePreventContainerLoot = ParseBool(*v, out.enablePreventContainerLoot);
			}
			if (auto v = GetValue(ini, "lootblocking", "enablepreventpickupobject"); v) {
				out.enablePreventPickupObject = ParseBool(*v, out.enablePreventPickupObject);
			}
		}

		void LoadAutoEquipBlocking(const SectionMap& ini, AutoEquipBlocking& out)
		{
			if (auto v = GetValue(ini, "autoequipblocking", "enablenoncombatequipblocker"); v) {
				out.enableNonCombatEquipBlocker = ParseBool(*v, out.enableNonCombatEquipBlocker);
			}
			if (auto v = GetValue(ini, "autoequipblocking", "enablebestweaponautoequipsuppressor"); v) {
				out.enableBestWeaponAutoEquipSuppressor = ParseBool(*v, out.enableBestWeaponAutoEquipSuppressor);
			}
		}

		void LoadEquipGate(const SectionMap& ini, EquipGate& out)
		{
			if (auto v = GetValue(ini, "equipgate", "enableequipblocking"); v) {
				out.enableEquipBlocking = ParseBool(*v, out.enableEquipBlocking);
			}
			if (auto v = GetValue(ini, "equipgate", "enableunequipblocking"); v) {
				out.enableUnequipBlocking = ParseBool(*v, out.enableUnequipBlocking);
			}
			if (auto v = GetValue(ini, "equipgate", "neverblocktorchequip"); v) {
				out.enableNeverBlockTorchEquip = ParseBool(*v, out.enableNeverBlockTorchEquip);
			}
		}

		void LoadWeaponEnchantmentRecharge(const SectionMap& ini, WeaponEnchantmentRecharge& out)
		{
			if (auto v = GetValue(ini, "weaponenchantmentrecharge", "enablerecharge"); v) {
				out.enableRecharge = ParseBool(*v, out.enableRecharge);
			}
			if (auto v = GetValue(ini, "weaponenchantmentrecharge", "rechargekeydik"); v) {
				out.rechargeKeyDik = ParseU32(*v, out.rechargeKeyDik);
			}
			if (auto v = GetValue(ini, "weaponenchantmentrecharge", "gamepadrechargekey"); v) {
				out.gamepadRechargeKey = ParseU32(*v, out.gamepadRechargeKey);
			}
			if (auto v = GetValue(ini, "weaponenchantmentrecharge", "enableuiindicator"); v) {
				out.enableUIIndicator = ParseBool(*v, out.enableUIIndicator);
			}
			if (auto v = GetValue(ini, "weaponenchantmentrecharge", "advancedsoulgemdisplay"); v) {
				out.advancedSoulGemDisplay = ParseBool(*v, out.advancedSoulGemDisplay);
			}
			if (auto v = GetValue(ini, "weaponenchantmentrecharge", "showstackcount"); v) {
				out.showStackCount = ParseBool(*v, out.showStackCount);
			}

			if (auto v = GetValue(ini, "weaponenchantmentrecharge", "containedsouldisplaymode"); v) {
				out.containedSoulDisplayMode = ParseContainedSoulDisplayMode(*v, out.containedSoulDisplayMode);
			}

			if (auto v = GetValue(ini, "weaponenchantmentrecharge", "sourcedisplaymode"); v) {
				out.sourceDisplayMode = ParseSoulGemSourceDisplayMode(*v, out.sourceDisplayMode);
			}

			if (auto v = GetValue(ini, "weaponenchantmentrecharge", "soulgemsortmode"); v) {
				out.soulGemSortMode = ParseSoulGemSortMode(*v, out.soulGemSortMode);
			}
		}

		void LoadStatsDisplay(const SectionMap& ini, StatsDisplay& out)
		{
			if (auto v = GetValue(ini, "statsdisplay", "enablefollowerstatsintrademenu"); v) {
				out.followerStatsInTradeMenuMode = ParseFollowerStatsInTradeMenuMode(*v, out.followerStatsInTradeMenuMode);
			}
		}

		void LoadCorpseEquipMode(const SectionMap& ini, CorpseEquipMode& out)
		{
			if (auto v = GetValue(ini, "corpseequipmode", "enable"); v) {
				out.enable = ParseBool(*v, out.enable);
			}
			if (auto v = GetValue(ini, "corpseequipmode", "indicatortext"); v) {
				auto s = Trim(*v);
				if (!s.empty()) {
					out.indicatorText = std::move(s);
				}
			}
		}

		void LoadEngineFixes(const SectionMap& ini, EngineFixes& out)
		{
			if (auto v = GetValue(ini, "enginefixes", "enablestaleweightcachefix"); v) {
				out.enableStaleWeightCacheFix = ParseBool(*v, out.enableStaleWeightCacheFix);
			}
		}

		void LoadSkyUIFixes(const SectionMap& ini, SkyUIFixes& out)
		{
			if (auto v = GetValue(ini, "skyuifixes", "enablequantitymenublocker"); v) {
				out.enableQuantityMenuBlocker = ParseBool(*v, out.enableQuantityMenuBlocker);
			}
			if (auto v = GetValue(ini, "skyuifixes", "enablezeroweightfix"); v) {
				out.enableZeroWeightFix = ParseBool(*v, out.enableZeroWeightFix);
			}
		}

		[[nodiscard]] std::string DefaultIniContents(const Settings& defaults)
		{
			auto poisonMode = [](PoisonMode m) {
				switch (m) {
				case PoisonMode::kDisable:
					return "disable";
				case PoisonMode::kConsume:
					return "consume";
				case PoisonMode::kApplyToWeapon:
				default:
					return "applyToWeapon";
				}
			};
			auto boolStr = [](bool v) { return v ? "true" : "false"; };
			auto followerStatsInTradeMenuMode = [](FollowerStatsInTradeMenuMode m) {
				switch (m) {
				case FollowerStatsInTradeMenuMode::kDisable:
					return "disable";
				case FollowerStatsInTradeMenuMode::kAlways:
					return "always";
				case FollowerStatsInTradeMenuMode::kFollowerSide:
				default:
					return "follower_side";
				}
			};
			auto containedSoulMode = [](ContainedSoulDisplayMode m) {
				switch (m) {
				case ContainedSoulDisplayMode::kDisable:
					return "disable";
				case ContainedSoulDisplayMode::kAlways:
					return "always";
				case ContainedSoulDisplayMode::kOnlyIfLowerThanCapacity:
				default:
					return "lower_only";
			}
		};
			auto sourceMode = [](SoulGemSourceDisplayMode m) {
				switch (m) {
				case SoulGemSourceDisplayMode::kFollowerAndPlayer:
					return "both";
				case SoulGemSourceDisplayMode::kFollowerOnly:
					return "follower";
				case SoulGemSourceDisplayMode::kPlayerOnly:
					return "player";
				case SoulGemSourceDisplayMode::kDisable:
				default:
					return "disable";
			}
		};
			auto soulGemSortMode = [](SoulGemSortMode m) {
				switch (m) {
				case SoulGemSortMode::kName:
					return "name";
				case SoulGemSortMode::kSoulDesc:
					return "soul_desc";
				case SoulGemSortMode::kSoulAsc:
				default:
					return "soul_asc";
			}
		};
			return std::format(
				"; ============================================================\n"
				"; Follower Equip Control - Settings\n"
				"; ============================================================\n"
				"; Boolean values: true / false\n"
				"; Changes take effect after restarting the game.\n"
				"; If you have SKSE Menu Framework, you can change these in-game\n"
				"; via the Mod Control Panel (changes apply immediately there).\n"
				"; ============================================================\n"
				"\n"
				"\n"
				"[Core]\n"
				"\n"
				"; Enables or disables all mod features.\n"
				"; Default: true\n"
				"EnableMod={}\n"
				"\n"
				"\n"
				"[UIFeedback]\n"
				"\n"
				"; Shows short HUD notifications when the mod performs an action on a follower.\n"
				"; Default: true\n"
				"EnableNotifications={}\n"
				"\n"
				"; Plays vanilla UI sounds when the mod performs an action on a follower.\n"
				"; Default: true\n"
				"EnableUISounds={}\n"
				"\n"
				"\n"
				"[Logging]\n"
				"\n"
				"; Controls how much detail the mod writes to the SKSE log file.\n"
				"; Values: trace, debug, info, warn, error, critical, off\n"
				";   trace    - most verbose, logs all internal events\n"
				";   debug    - verbose diagnostic output\n"
				";   info     - standard operation messages\n"
				";   warn     - warnings only\n"
				";   error    - recoverable errors only\n"
				";   critical - unrecoverable errors only\n"
				";   off      - no logging\n"
				"; Default: info\n"
				"LogLevel={}\n"
				"\n"
				"\n"
				"[ActorScope]\n"
				"\n"
				"; Extends all mod features to dismissed and former followers.\n"
				"; When disabled, only followers currently in your party are affected.\n"
				"; Default: true\n"
				"AffectFormerFollowers={}\n"
				"\n"
				"; Includes actors matched by the FollowerEquipControl/Actors/*.ini inclusion rules\n"
				"; (keyword / NPC / faction) in the mod scope. When disabled, all inclusion rules\n"
				"; are ignored (third-party follower bridges and scripted-actor admissions are off).\n"
				"; Default: true\n"
				"IncludeInclusionActors={}\n"
				"\n"
				"; Allows non-humanoid inclusion actors to receive the full equip feature set.\n"
				"; When disabled, hand-only races (draugr, skeletons, falmer) are limited to\n"
				"; weapons and shields; full creatures are blocked from body-slot armor equip.\n"
				"; Default: false\n"
				"IncludeNonHumanoidInclusionActors={}\n"
				"\n"
				"; Includes player-commanded actors (conjured summons, dead thralls, reanimated\n"
				"; corpses, mounts) in the mod scope. When disabled, these actors are ignored.\n"
				"; Default: true\n"
				"IncludePlayerSummons={}\n"
				"\n"
				"; Allows non-humanoid player-commanded actors to receive the full equip feature set.\n"
				"; When disabled, full creatures are excluded from the mod entirely; hand-only\n"
				"; races (draugr, skeletons, falmer) are limited to weapons and shields.\n"
				"; Default: false\n"
				"IncludeNonHumanoidSummons={}\n"
				"\n"
				"\n"
				"[QuickTrade]\n"
				"\n"
				"; Master toggle for Quick Trade (Followers, FormerFollowers, InclusionActors,\n"
				"; PlayerSummons, KNoneActors, Mounts, QuickTradeAllWorldMounts).\n"
				"; When disabled, the hotkey is inactive for all actor categories.\n"
				"; Default: true\n"
				"EnableQuickTrade={}\n"
				"\n"
				"; Enables Quick Trade for active followers (IsPlayerTeammate).\n"
				"; Independent of the other EnableQuickTradeFor* keys.\n"
				"; Default: true\n"
				"EnableQuickTradeForFollowers={}\n"
				"\n"
				"; Enables Quick Trade for former (dismissed) followers no longer IsPlayerTeammate.\n"
				"; Independent of the other EnableQuickTradeFor* keys.\n"
				"; Ignored when AffectFormerFollowers=false.\n"
				"; Default: true\n"
				"EnableQuickTradeForFormerFollowers={}\n"
				"\n"
				"; Enables Quick Trade for inclusion actors flagged with the '+' prefix.\n"
				"; Independent of the other EnableQuickTradeFor* keys.\n"
				"; Default: true\n"
				"EnableQuickTradeForInclusionActors={}\n"
				"\n"
				"; Enables Quick Trade for player-commanded actors (summons, dead thralls, reanimated corpses).\n"
				"; Independent of the other EnableQuickTradeFor* keys.\n"
				"; Default: true\n"
				"EnableQuickTradeForPlayerSummons={}\n"
				"\n"
				"; Enables Quick Trade for kNone actors (wolves, atronachs, horses) within mod scope.\n"
				"; Opens trade menu only - no equip features apply to kNone actors.\n"
				"; The actor must still pass its own category gate (e.g. IncludePlayerSummons).\n"
				"; Default: true\n"
				"EnableQuickTradeForKNoneActors={}\n"
				"\n"
				"; Enables Quick Trade for mounts associated with followers or the player.\n"
				"; Covers Tier 1 (follower/summon/inclusion actor mounts) and Tier 2 (player's last mount).\n"
				"; No mod features apply to mounts -- this opens trade menu only.\n"
				"; Default: true\n"
				"EnableQuickTradeForMounts={}\n"
				"\n"
				"; Expands mount Quick Trade to all world mounts (any IsAMount() actor).\n"
				"; When enabled, every horse and rideable creature becomes eligible, not just\n"
				"; follower/player-associated mounts.\n"
				"; Warning: this opens Quick Trade for all rideable creatures in the world.\n"
				"; Ignored when EnableQuickTradeForMounts=false.\n"
				"; Default: false\n"
				"QuickTradeAllWorldMounts={}\n"
				"\n"
				"\n"
				"[KeyboardControls]\n"
				"\n"
				"; Controls which key activates mod features while trading with a follower.\n"
				"; Value is a DirectInput keyboard scan code (decimal).\n"
				"; https://wiki.nexusmods.com/index.php/DirectX_Scancodes_And_How_To_Use_Them\n"
				"; Default: 29\n"
				"ModKeyDik={}\n"
				"\n"
				"; Overrides SkyUI's Equip Mode key with a custom key.\n"
				"; SkyUI's config.txt setting for this key does not work for some users, so this is the only way to change it.\n"
				"; Value is a DirectInput keyboard scan code (decimal). Set to 0 to keep SkyUI's own Equip Mode key.\n"
				"; Default: 0\n"
				"SkyuiEquipModeKey={}\n"
				"\n"
				"; Controls the Quick Trade modifier key (DirectInput scan code).\n"
				"; Hold this key + press Activate on an eligible actor to open trade directly.\n"
				"; Set to 0 to use the engine-bound Sprint key as the modifier.\n"
				"; Default: 0 (use Sprint binding)\n"
				"QuickTradeKeyDik={}\n"
				"\n"
				"\n"
				"[GamepadControls]\n"
				"\n"
				"; Sets the gamepad Mod Key button (SKSE keycode, 266-281).\n"
				"; Default: 280 (LT / Left Trigger)\n"
				"GamepadModKey={}\n"
				"\n"
				"; Sets the gamepad button for right-hand equip while holding the Mod Key.\n"
				"; Default: 276 (A)\n"
				"GamepadRightHandKey={}\n"
				"\n"
				"; Sets the gamepad button for left-hand equip while holding the Mod Key.\n"
				"; Default: 278 (X)\n"
				"GamepadLeftHandKey={}\n"
				"\n"
				"; Controls the Quick Trade modifier button (SKSE gamepad keycode, 266-281).\n"
				"; Set to 0 to use the engine-bound Sprint gamepad button as the modifier.\n"
				"; Default: 0 (use Sprint binding)\n"
				"QuickTradeGamepadKey={}\n"
				"\n"
				"\n"
				"[ButtonIndicators]\n"
				"\n"
				"; Adds a Mod Key indicator in the SkyUI bottom bar while trading with a follower.\n"
				"; Default: true\n"
				"EnableModKeyIndicator={}\n"
				"\n"
				"; Sets the text shown on the Mod Key indicator in the SkyUI bottom bar.\n"
				"; Leave empty to use the localized default from your language file.\n"
				"; Default: (empty - uses localized default)\n"
				"ModKeyIndicatorText={}\n"
				"\n"
				"; Sets the text shown on the Mod Key indicator when the target is a list-included actor.\n"
				"; Leave empty to use the localized default from your language file.\n"
				"; Default: (empty - uses localized default)\n"
				"InclusionIndicatorText={}\n"
				"\n"
				"; Sets the text shown on the Mod Key indicator when the target is a player-summoned actor.\n"
				"; Leave empty to use the localized default from your language file.\n"
				"; Default: (empty - uses localized default)\n"
				"SummonIndicatorText={}\n"
				"\n"
				"; Overrides SkyUI's Equip Mode label in the bottom bar while trading with a follower.\n"
				"; Leave empty to use the localized default from your language file.\n"
				"; Default: (empty - uses localized default)\n"
				"EquipModeTextOverride={}\n"
				"\n"
				"\n"
				"[IconAppearance]\n"
				"\n"
				"; Master toggle for mod icons (CombatEquipIcon, HeadgearIcon, OutfitSyncIcon).\n"
				"; When disabled, no mod icons are shown next to item names in the trade menu.\n"
				"; Default: true\n"
				"EnableIconIndicator={}\n"
				"\n"
				"; Displays an icon next to items in the trade list that have a combat-equip preference set.\n"
				"; Default: true\n"
				"EnableCombatEquipIcon={}\n"
				"\n"
				"; Displays an icon next to the headgear item that is set as the combat headgear preference.\n"
				"; Default: true\n"
				"EnableHeadgearIcon={}\n"
				"\n"
				"; Displays an icon next to items in the trade list that are part of the saved outfit snapshot.\n"
				"; Default: true\n"
				"EnableOutfitSyncIcon={}\n"
				"\n"
				"; Master toggle for icon customization (IconSize, GapAfterText, GapAfterIcon, FecIconSpacing).\n"
				"; When disabled, icon sizes and spacing use built-in defaults.\n"
				"; Default: true\n"
				"EnableCustomization={}\n"
				"\n"
				"; Sets the width and height (in pixels) of mod icons shown next to item names.\n"
				"; Ignored when EnableCustomization=false.\n"
				"; Default: 12.0\n"
				"IconSize={}\n"
				"\n"
				"; Sets the horizontal gap (in pixels) after the item name text field when no other icons are present.\n"
				"; Ignored when EnableCustomization=false.\n"
				"; Default: 8.0\n"
				"GapAfterText={}\n"
				"\n"
				"; Sets the horizontal gap (in pixels) after the rightmost non-mod icon (SkyUI, DIII, etc.).\n"
				"; Effective gap = stored value - 6.0 (default 8.0 => 2px effective gap).\n"
				"; Ignored when EnableCustomization=false.\n"
				"; Default: 8.0\n"
				"GapAfterIcon={}\n"
				"\n"
				"; Sets the horizontal gap (in pixels) between adjacent mod icons.\n"
				"; Ignored when EnableCustomization=false.\n"
				"; Default: 8.0\n"
				"FecIconSpacing={}\n"
				"\n"
				"\n"
				"[EquipModeItems]\n"
				"\n"
				"; Master toggle for equip-mode items (Weapons, Armor, Ammo, Torches, Scrolls).\n"
				"; Hold the Mod Key and click an item to equip or unequip it.\n"
				"; When disabled, the Mod Key equip action is inactive for all item types.\n"
				"; Default: true\n"
				"Enable={}\n"
				"\n"
				"; Allows weapons to be equipped on followers.\n"
				"; Default: true\n"
				"EnableWeapons={}\n"
				"\n"
				"; Allows armor and shields to be equipped on followers.\n"
				"; Default: true\n"
				"EnableArmor={}\n"
				"\n"
				"; Allows ammo (arrows and bolts) to be equipped on followers.\n"
				"; Default: true\n"
				"EnableAmmo={}\n"
				"\n"
				"; Allows torches to be equipped on followers.\n"
				"; Default: true\n"
				"EnableTorches={}\n"
				"\n"
				"; Allows scrolls to be equipped on followers.\n"
				"; Default: true\n"
				"EnableScrolls={}\n"
				"\n"
				"\n"
				"[EquipModeConsumableMode]\n"
				"\n"
				"; Master toggle for consumable mode (Potions, Foods, Drinks, Ingredients).\n"
				"; Hold the Mod Key and click a consumable to make the follower consume it.\n"
				"; When disabled, the Mod Key consume action is inactive for all consumable types.\n"
				"; Default: true\n"
				"EnableConsumableMode={}\n"
				"\n"
				"; Allows potions to be consumed by followers.\n"
				"; Default: true\n"
				"EnablePotions={}\n"
				"\n"
				"; Allows food to be consumed by followers.\n"
				"; Default: true\n"
				"EnableFoods={}\n"
				"\n"
				"; Allows drinks to be consumed by followers.\n"
				"; Default: true\n"
				"EnableDrinks={}\n"
				"\n"
				"; Allows ingredients to be consumed by followers.\n"
				"; Default: true\n"
				"EnableIngredients={}\n"
				"\n"
				"; Lets the player learn the first alchemy effect when a follower consumes an ingredient.\n"
				"; Default: false\n"
				"EnableIngredientDiscoveryForPlayer={}\n"
				"\n"
				"\n"
				"[EquipModePoisonMode]\n"
				"\n"
				"; Controls what happens when you click a poison while holding the Mod Key.\n"
				"; Values: applyToWeapon, consume, disable\n"
				";   applyToWeapon - applies the poison to the follower's equipped weapon\n"
				";   consume       - the follower consumes the poison\n"
				";   disable       - transfers the poison normally\n"
				"; Default: applyToWeapon\n"
				"Mode={}\n"
				"\n"
				"; Enables poison charge stacking when Mode = applyToWeapon.\n"
				"; When disabled, clicking a poison on an already-poisoned weapon is blocked.\n"
				"; Default: false\n"
				"EnablePoisonStacking={}\n"
				"\n"
				"; Sets the maximum number of poison charges that can be stacked.\n"
				"; 1-99 = charge cap, 100 = unlimited.\n"
				"; Ignored when EnablePoisonStacking=false.\n"
				"; Default: 100\n"
				"MaxPoisonCharges={}\n"
				"\n"
				"\n"
				"[EquipModeSpellTomeMode]\n"
				"\n"
				"; Allows followers to learn spells from tomes while trading.\n"
				"; Hold the Mod Key and click a spell tome to teach the spell.\n"
				"; Default: true\n"
				"EnableSpellTomeMode={}\n"
				"\n"
				"\n"
				"[CombatEquipPreference]\n"
				"\n"
				"; Makes followers prefer the gear you selected when the game chooses what to equip in combat.\n"
				"; Does not force-equip; gives your selection a strong advantage in the game's evaluation.\n"
				"; Default: true\n"
				"EnableScoring={}\n"
				"\n"
				"; Tries to equip the exact version of a preferred item (matching temper, enchantment, etc.),\n"
				"; not just any copy of the same base item.\n"
				"; Default: true\n"
				"EnableInstanceAlign={}\n"
				"\n"
				"; Clears a saved preference when you unequip that item from a follower during trade.\n"
				"; Covers all slots except the left hand (see below).\n"
				"; Default: false\n"
				"EnableClearPreferencesOnUnequip={}\n"
				"\n"
				"; Clears the left-hand preference when you unequip a left-hand item during trade.\n"
				"; Separate from the above because shield/off-hand choices are often more deliberate.\n"
				"; Default: false\n"
				"EnableClearLeftHandPreferenceOnUnequip={}\n"
				"\n"
				"\n"
				"[CombatEquipEnforcement]\n"
				"\n"
				"; Forces followers to keep the melee weapons and shields you selected during combat.\n"
				"; Blocks AI swaps. Only melee weapons and shields are covered.\n"
				"; Default: true\n"
				"EnableMeleeEnforcement={}\n"
				"\n"
				"; Clears any hand-slot with no saved preference or whose preferred item is no longer in inventory.\n"
				"; Useful with unarmed combat mods when you want to leave a hand deliberately empty.\n"
				"; Ignored when EnableMeleeEnforcement=false.\n"
				"; Default: false\n"
				"EnableMeleeEnforcementEmptyHand={}\n"
				"\n"
				"; Makes followers use the arrows or bolts you selected, overriding automatic ammo switching.\n"
				"; Default: true\n"
				"EnableAmmoPreference={}\n"
				"\n"
				"\n"
				"[CombatEquipRestore]\n"
				"\n"
				"; Restores the follower's pre-combat equipment (weapons, shield, ammo, empty hands) when combat ends.\n"
				"; Default: true\n"
				"EnableRestorePreCombatOnExit={}\n"
				"\n"
				"; Equips the selected headgear when combat starts and restores pre-combat state when combat ends.\n"
				"; Covers helmets, hoods, and circlets.\n"
				"; Default: false\n"
				"EnableHeadgearAutoEquip={}\n"
				"\n"
				"; Prevents followers from consuming ammo when firing ranged weapons.\n"
				"; Followers will never run out of arrows or bolts.\n"
				"; Default: false\n"
				"EnableInfiniteAmmo={}\n"
				"\n"
				"\n"
				"[OutfitSync]\n"
				"\n"
				"; Prevents the game from re-dressing the follower in their default outfit\n"
				"; after inventory resets or area transitions.\n"
				"; Default: true\n"
				"EnableUpdateNpcOutfitSuppression={}\n"
				"\n"
				"; Re-equips the armor and clothing you last chose for the follower\n"
				"; after the game rebuilds their outfit (save/load, fast travel, area transitions).\n"
				"; Default: true\n"
				"EnableOutfitSnapshotRestore={}\n"
				"\n"
				"; Lets external mods (SPID, NFF, Simple Outfit Framework, Papyrus scripts,\n"
				"; AI packages, etc.) change the follower's outfit.\n"
				"; Default: true\n"
				"AllowOutfitChanges={}\n"
				"\n"
				"\n"
				"[HiddenItems]\n"
				"\n"
				"; Master toggle for all non-playable item handling (Armor, Weapon, Ammo).\n"
				"; When disabled, all three handling modes are skipped for followers.\n"
				"; Default: true\n"
				"NonPlayableItems={}\n"
				"\n"
				"; Removes non-playable armor from the follower's inventory when you open trade.\n"
				"; Warning: May conflict with mods that use non-playable armor pieces for internal purposes.\n"
				"; Default: false\n"
				"RemoveHiddenArmor={}\n"
				"\n"
				"; Removes non-playable weapons and shields from the follower's inventory when you open trade.\n"
				"; Warning: Removed items are not restored if this setting is later disabled.\n"
				"; Default: true\n"
				"RemoveHiddenWeapon={}\n"
				"\n"
				"; Removes non-playable ammo from the follower's inventory when you open trade.\n"
				"; Warning: Removed items are not restored if this setting is later disabled.\n"
				"; Default: true\n"
				"RemoveHiddenAmmo={}\n"
				"\n"
				"; Master toggle for outfit item visibility (EnableRevealDefaultOutfitItems, EnableRevealExternalOutfitItems).\n"
				"; When disabled, outfit items are not revealed in the trade list.\n"
				"; Default: true\n"
				"EnableOutfitItems={}\n"
				"\n"
				"; Makes default outfit items visible in the trade list when you open trade.\n"
				"; Worn items are revealed; surplus unworn copies are removed.\n"
				"; Default: true\n"
				"EnableRevealDefaultOutfitItems={}\n"
				"\n"
				"; Makes outfit items distributed by external mods (e.g. SPID) visible in the trade list.\n"
				"; These mods use the same outfit-hiding system, so their items appear hidden by default.\n"
				"; Default: true\n"
				"EnableRevealExternalOutfitItems={}\n"
				"\n"
				"\n"
				"[ItemInjectionBlocking]\n"
				"\n"
				"; Prevents the game from adding armor pieces from a follower's default outfit to their inventory.\n"
				"; Default: false\n"
				"EnableOutfitItemBlocker={}\n"
				"\n"
				"; Prevents the game from adding leveled list items (weapons, ammo, potions, etc.)\n"
				"; to follower inventories.\n"
				"; Default: false\n"
				"EnableLeveledItemBlocker={}\n"
				"\n"
				"\n"
				"[LootBlocking]\n"
				"\n"
				"; Stops followers from seeking out and grabbing weapons during combat.\n"
				"; Default: true\n"
				"EnablePreventCombatLoot={}\n"
				"\n"
				"; Stops followers from looting weapons and ammo from containers and corpses.\n"
				"; Default: true\n"
				"EnablePreventContainerLoot={}\n"
				"\n"
				"; Stops followers from picking up loose items from the ground.\n"
				"; Default: true\n"
				"EnablePreventPickupObject={}\n"
				"\n"
				"\n"
				"[AutoEquipBlocking]\n"
				"\n"
				"; Prevents followers from automatically equipping weapons, shields, or ammo outside combat\n"
				"; (e.g. after fast travel or location change).\n"
				"; Default: true\n"
				"EnableNonCombatEquipBlocker={}\n"
				"\n"
				"; Stops the game from automatically equipping what it considers the best weapon\n"
				"; on the follower during trade.\n"
				"; Default: true\n"
				"EnableBestWeaponAutoEquipSuppressor={}\n"
				"\n"
				"\n"
				"[EquipGate]\n"
				"\n"
				"; Blocks ALL item equips on followers that are not performed by this mod.\n"
				"; Warning: May conflict with other mods that manage follower equipment.\n"
				"; Default: false\n"
				"EnableEquipBlocking={}\n"
				"\n"
				"; Blocks ALL item unequips on followers that are not performed by this mod.\n"
				"; Warning: May conflict with other mods that manage follower equipment.\n"
				"; Default: false\n"
				"EnableUnequipBlocking={}\n"
				"\n"
				"; Allows followers to equip and unequip torches even when blocking is active.\n"
				"; Ignored when both EnableEquipBlocking=false and EnableUnequipBlocking=false.\n"
				"; Default: true\n"
				"NeverBlockTorchEquip={}\n"
				"\n"
				"\n"
				"[WeaponEnchantmentRecharge]\n"
				"\n"
				"; Enables recharging enchanted weapons while trading with a follower.\n"
				"; Select an enchanted weapon and press the Recharge Key.\n"
				"; Default: true\n"
				"EnableRecharge={}\n"
				"\n"
				"; Sets the key for recharging enchanted weapons (DirectInput scan code).\n"
				"; Default: 20\n"
				"RechargeKeyDik={}\n"
				"\n"
				"; Sets the gamepad button for recharging while holding the Mod Key (SKSE keycode, 266-281).\n"
				"; Default: 279 (Y)\n"
				"GamepadRechargeKey={}\n"
				"\n"
				"; Adds a Recharge Key indicator in the SkyUI bottom bar when an enchanted weapon is selected.\n"
				"; Default: true\n"
				"EnableUIIndicator={}\n"
				"\n"
				"; Master toggle for advanced soul gem display (ShowStackCount, ContainedSoulDisplayMode, SourceDisplayMode).\n"
				"; When disabled, the soul gem picker shows basic gem names only.\n"
				"; Default: true\n"
				"AdvancedSoulGemDisplay={}\n"
				"\n"
				"; Adds a (count) suffix for stacked soul gems in the picker.\n"
				"; Ignored when AdvancedSoulGemDisplay=false.\n"
				"; Default: true\n"
				"ShowStackCount={}\n"
				"\n"
				"; Controls when the contained-soul level is shown in the picker.\n"
				"; Values: disable, lower_only, always\n"
				";   disable    - never show the contained soul\n"
				";   lower_only - show only when the soul is lower than the gem's capacity\n"
				";   always     - always show the contained soul\n"
				"; Ignored when AdvancedSoulGemDisplay=false.\n"
				"; Default: lower_only\n"
				"ContainedSoulDisplayMode={}\n"
				"\n"
				"; Controls which inventory source tags are shown for soul gems.\n"
				"; Values: disable, follower, player, both\n"
				";   disable  - no source tags\n"
				";   follower - tag only the follower's gems\n"
				";   player   - tag only the player's gems\n"
				";   both     - tag both inventories\n"
				"; Ignored when AdvancedSoulGemDisplay=false.\n"
				"; Default: disable\n"
				"SourceDisplayMode={}\n"
				"\n"
				"; Controls how the soul gem picker is sorted.\n"
				"; Values: name, soul_asc, soul_desc\n"
				";   name      - alphabetical, similar to vanilla\n"
				";   soul_asc  - by contained soul level, lowest first\n"
				";   soul_desc - by contained soul level, highest first\n"
				"; Ignored when AdvancedSoulGemDisplay=false.\n"
				"; Default: soul_asc\n"
				"SoulGemSortMode={}\n"
				"\n"
				"\n"
				"[StatsDisplay]\n"
				"\n"
				"; Controls where follower stats (carry weight, gold, health/magicka/stamina) are shown.\n"
				"; Values: disable, follower_side, always\n"
				";   disable       - does not override the stats display\n"
				";   follower_side - shows follower stats only on the Take tab\n"
				";   always        - shows follower stats on both the Take and Give tabs\n"
				"; Default: follower_side\n"
				"EnableFollowerStatsInTradeMenu={}\n"
				"\n"
				"\n"
				"[CorpseEquipMode]\n"
				"\n"
				"; Allow equipping and unequipping items on dead NPCs (corpses) while looting.\n"
				"; Hold the Mod Key and click an item to equip/unequip it on the corpse.\n"
				"; Default: true\n"
				"Enable={}\n"
				"\n"
				"; Sets the text shown on the bottom bar indicator when looting a corpse.\n"
				"; Leave empty to use the localized default from your language file.\n"
				"; Default: (empty - uses localized default)\n"
				"IndicatorText={}\n"
				"\n"
				"\n"
				"[EngineFixes]\n"
				"\n"
				"; Fixes Give-tab items that cannot be transferred caused by a corrupt engine weight cache.\n"
				"; Corrects the stuck carry weight when the trade menu opens and after each item transfer.\n"
				"; Default: true\n"
				"EnableStaleWeightCacheFix={}\n"
				"\n"
				"\n"
				"[SkyUIFixes]\n"
				"\n"
				"; Skips the quantity prompt during Mod Key actions and SkyUI equip mode.\n"
				"; Since these actions always target a single item, the prompt is unnecessary.\n"
				"; Default: true\n"
				"EnableQuantityMenuBlocker={}\n"
				"\n"
				"; Forces a quantity prompt when transferring weightless items (gold, lockpicks, etc.)\n"
				"; from a follower, so you can choose the exact amount instead of taking the entire stack.\n"
				"; Default: true\n"
				"EnableZeroWeightFix={}\n"
				"\n",
				boolStr(defaults.core.enableMod),
				boolStr(defaults.uiFeedback.enableNotifications),
				boolStr(defaults.uiFeedback.enableUISounds),
				defaults.logging.logLevel,
				boolStr(defaults.actorScope.affectFormerFollowers),
				boolStr(defaults.actorScope.includeInclusionActors),
				boolStr(defaults.actorScope.includeNonHumanoidInclusionActors),
				boolStr(defaults.actorScope.includePlayerSummons),
				boolStr(defaults.actorScope.includeNonHumanoidSummons),
				boolStr(defaults.quickTrade.enableQuickTrade),
				boolStr(defaults.quickTrade.enableQuickTradeForFollowers),
				boolStr(defaults.quickTrade.enableQuickTradeForFormerFollowers),
				boolStr(defaults.quickTrade.enableQuickTradeForInclusionActors),
				boolStr(defaults.quickTrade.enableQuickTradeForPlayerSummons),
				boolStr(defaults.quickTrade.enableQuickTradeForKNoneActors),
				boolStr(defaults.quickTrade.enableQuickTradeForMounts),
				boolStr(defaults.quickTrade.quickTradeAllWorldMounts),
				defaults.keyboardControls.modKeyDik,
				defaults.keyboardControls.skyuiEquipModeKey,
				defaults.keyboardControls.quickTradeKeyDik,
				defaults.gamepadControls.gamepadModKey,
				defaults.gamepadControls.gamepadRightHandKey,
				defaults.gamepadControls.gamepadLeftHandKey,
				defaults.gamepadControls.quickTradeGamepadKey,
				boolStr(defaults.buttonIndicators.enableModKeyIndicator),
				defaults.buttonIndicators.modKeyIndicatorText,
				defaults.buttonIndicators.inclusionIndicatorText,
				defaults.buttonIndicators.summonIndicatorText,
				defaults.buttonIndicators.equipModeTextOverride,
				boolStr(defaults.iconAppearance.enableIconIndicator),
				boolStr(defaults.iconAppearance.enableCombatEquipIcon),
				boolStr(defaults.iconAppearance.enableHeadgearIcon),
				boolStr(defaults.iconAppearance.enableOutfitSyncIcon),
				boolStr(defaults.iconAppearance.enableCustomization),
				defaults.iconAppearance.iconSize,
				defaults.iconAppearance.gapAfterText,
				defaults.iconAppearance.gapAfterIcon,
				defaults.iconAppearance.fecIconSpacing,
				boolStr(defaults.equipModeItems.enable),
				boolStr(defaults.equipModeItems.enableWeapons),
				boolStr(defaults.equipModeItems.enableArmor),
				boolStr(defaults.equipModeItems.enableAmmo),
				boolStr(defaults.equipModeItems.enableTorches),
				boolStr(defaults.equipModeItems.enableScrolls),
				boolStr(defaults.equipModeConsumableMode.enableConsumableMode),
				boolStr(defaults.equipModeConsumableMode.enablePotions),
				boolStr(defaults.equipModeConsumableMode.enableFoods),
				boolStr(defaults.equipModeConsumableMode.enableDrinks),
				boolStr(defaults.equipModeConsumableMode.enableIngredients),
				boolStr(defaults.equipModeConsumableMode.enableIngredientDiscoveryForPlayer),
				poisonMode(defaults.equipModePoisonMode.mode),
				boolStr(defaults.equipModePoisonMode.enableStacking),
				defaults.equipModePoisonMode.maxCharges,
				boolStr(defaults.equipModeSpellTomeMode.enableSpellTomeMode),
				boolStr(defaults.combatEquipPreference.enableScoring),
				boolStr(defaults.combatEquipPreference.enableInstanceAlign),
				boolStr(defaults.combatEquipPreference.enableClearPreferencesOnUnequip),
				boolStr(defaults.combatEquipPreference.enableClearLeftHandPreferenceOnUnequip),
				boolStr(defaults.combatEquipEnforcement.enableMeleeEnforcement),
				boolStr(defaults.combatEquipEnforcement.enableMeleeEnforcementEmptyHand),
				boolStr(defaults.combatEquipEnforcement.enableAmmoPreference),
				boolStr(defaults.combatEquipRestore.enableRestorePreCombatOnExit),
				boolStr(defaults.combatEquipRestore.enableHeadgearAutoEquip),
				boolStr(defaults.combatEquipRestore.enableInfiniteAmmo),
				boolStr(defaults.outfitSync.enableUpdateNpcOutfitSuppression),
				boolStr(defaults.outfitSync.enableOutfitSnapshotRestore),
				boolStr(defaults.outfitSync.allowOutfitChanges),
				boolStr(defaults.hiddenItems.enableNonPlayableItems),
				boolStr(defaults.hiddenItems.removeHiddenArmor),
				boolStr(defaults.hiddenItems.removeHiddenWeapon),
				boolStr(defaults.hiddenItems.removeHiddenAmmo),
				boolStr(defaults.hiddenItems.enableOutfitItems),
				boolStr(defaults.hiddenItems.enableRevealDefaultOutfitItems),
				boolStr(defaults.hiddenItems.enableRevealExternalOutfitItems),
				boolStr(defaults.itemInjectionBlocking.enableOutfitItemBlocker),
				boolStr(defaults.itemInjectionBlocking.enableLeveledItemBlocker),
				boolStr(defaults.lootBlocking.enablePreventCombatLoot),
				boolStr(defaults.lootBlocking.enablePreventContainerLoot),
				boolStr(defaults.lootBlocking.enablePreventPickupObject),
				boolStr(defaults.autoEquipBlocking.enableNonCombatEquipBlocker),
				boolStr(defaults.autoEquipBlocking.enableBestWeaponAutoEquipSuppressor),
				boolStr(defaults.equipGate.enableEquipBlocking),
				boolStr(defaults.equipGate.enableUnequipBlocking),
				boolStr(defaults.equipGate.enableNeverBlockTorchEquip),
				boolStr(defaults.weaponEnchantmentRecharge.enableRecharge),
				defaults.weaponEnchantmentRecharge.rechargeKeyDik,
				defaults.weaponEnchantmentRecharge.gamepadRechargeKey,
				boolStr(defaults.weaponEnchantmentRecharge.enableUIIndicator),
				boolStr(defaults.weaponEnchantmentRecharge.advancedSoulGemDisplay),
				boolStr(defaults.weaponEnchantmentRecharge.showStackCount),
				containedSoulMode(defaults.weaponEnchantmentRecharge.containedSoulDisplayMode),
				sourceMode(defaults.weaponEnchantmentRecharge.sourceDisplayMode),
				soulGemSortMode(defaults.weaponEnchantmentRecharge.soulGemSortMode),
				followerStatsInTradeMenuMode(defaults.statsDisplay.followerStatsInTradeMenuMode),
				boolStr(defaults.corpseEquipMode.enable),
				defaults.corpseEquipMode.indicatorText,
				boolStr(defaults.engineFixes.enableStaleWeightCacheFix),
				boolStr(defaults.skyUIFixes.enableQuantityMenuBlocker),
				boolStr(defaults.skyUIFixes.enableZeroWeightFix));
		}

		void EnsureIniExistsWithDefaults(const std::filesystem::path& iniPath, const Settings& defaults)
		{
			std::error_code ec;
			if (std::filesystem::exists(iniPath, ec)) {
				return;
			}
			if (ec) {
				logger::warn("Config: failed to check INI existence '{}': {}", iniPath.string(), ec.message());
				return;
			}

			std::ofstream out(iniPath, std::ios::binary);
			if (!out.is_open()) {
				logger::warn("Config: failed to create default INI '{}'", iniPath.string());
				return;
			}

			out << DefaultIniContents(defaults);
			out.close();

			logger::info("Config: created default INI '{}'", iniPath.string());
		}
	}

	void Load()
	{
		Settings next{};

		auto iniPath = GetIniPathNearDll();
		if (!iniPath) {
			g_settings = next;
			g_loaded = true;
			logger::warn("Config: INI path unavailable; using defaults");
			return;
		}

		EnsureIniExistsWithDefaults(*iniPath, next);

		std::ifstream in(*iniPath);
		if (in.is_open()) {
			auto parsed = ParseIni(in);
			LoadCore(parsed, next.core);
			LoadUIFeedback(parsed, next.uiFeedback);
			LoadLogging(parsed, next.logging);
			LoadActorScope(parsed, next.actorScope);
			LoadQuickTrade(parsed, next.quickTrade);
			LoadKeyboardControls(parsed, next.keyboardControls);
			LoadGamepadControls(parsed, next.gamepadControls);
			LoadButtonIndicators(parsed, next.buttonIndicators);
			LoadIconAppearance(parsed, next.iconAppearance);
			LoadEquipModeItems(parsed, next.equipModeItems);
			LoadEquipModeConsumableMode(parsed, next.equipModeConsumableMode);
			LoadEquipModePoisonMode(parsed, next.equipModePoisonMode);
			LoadEquipModeSpellTomeMode(parsed, next.equipModeSpellTomeMode);
			LoadCombatEquipPreference(parsed, next.combatEquipPreference);
			LoadCombatEquipEnforcement(parsed, next.combatEquipEnforcement);
			LoadCombatEquipRestore(parsed, next.combatEquipRestore);
			LoadOutfitSync(parsed, next.outfitSync);
			LoadHiddenItems(parsed, next.hiddenItems);
			LoadItemInjectionBlocking(parsed, next.itemInjectionBlocking);
			LoadLootBlocking(parsed, next.lootBlocking);
			LoadAutoEquipBlocking(parsed, next.autoEquipBlocking);
			LoadEquipGate(parsed, next.equipGate);
			LoadWeaponEnchantmentRecharge(parsed, next.weaponEnchantmentRecharge);
			LoadStatsDisplay(parsed, next.statsDisplay);
			LoadCorpseEquipMode(parsed, next.corpseEquipMode);
			LoadEngineFixes(parsed, next.engineFixes);
			LoadSkyUIFixes(parsed, next.skyUIFixes);
		} else {
			logger::warn("Config: failed to open INI '{}'; using defaults", iniPath->string());
		}

		g_settings = next;
		g_loaded = true;

		logger::info(
			"Config loaded from '{}'",
			iniPath->string());
		logger::info(
			"Config: Core.EnableMod={}",
			g_settings.core.enableMod);
		logger::info(
			"Config: EnableNotifications={}",
			g_settings.uiFeedback.enableNotifications);
		logger::info(
			"Config: EnableUISounds={}",
			g_settings.uiFeedback.enableUISounds);
		logger::info(
			"Config: Logging.LogLevel={}",
			g_settings.logging.logLevel);
		logger::info(
			"Config: ActorScope.AffectFormerFollowers={}",
			g_settings.actorScope.affectFormerFollowers);
		logger::info(
			"Config: ActorScope.IncludeInclusionActors={}",
			g_settings.actorScope.includeInclusionActors);
		logger::info(
			"Config: ActorScope.IncludeNonHumanoidInclusionActors={}",
			g_settings.actorScope.includeNonHumanoidInclusionActors);
		logger::info(
			"Config: ActorScope.IncludePlayerSummons={}",
			g_settings.actorScope.includePlayerSummons);
		logger::info(
			"Config: ActorScope.IncludeNonHumanoidSummons={}",
			g_settings.actorScope.includeNonHumanoidSummons);
		logger::info(
			"Config: QuickTrade.EnableQuickTrade={}",
			g_settings.quickTrade.enableQuickTrade);
		logger::info(
			"Config: QuickTrade.EnableQuickTradeForFollowers={}",
			g_settings.quickTrade.enableQuickTradeForFollowers);
		logger::info(
			"Config: QuickTrade.EnableQuickTradeForFormerFollowers={}",
			g_settings.quickTrade.enableQuickTradeForFormerFollowers);
		logger::info(
			"Config: QuickTrade.EnableQuickTradeForPlayerSummons={}",
			g_settings.quickTrade.enableQuickTradeForPlayerSummons);
		logger::info(
			"Config: QuickTrade.EnableQuickTradeForInclusionActors={}",
			g_settings.quickTrade.enableQuickTradeForInclusionActors);
		logger::info(
			"Config: QuickTrade.EnableQuickTradeForKNoneActors={}",
			g_settings.quickTrade.enableQuickTradeForKNoneActors);
		logger::info(
			"Config: QuickTrade.EnableQuickTradeForMounts={}",
			g_settings.quickTrade.enableQuickTradeForMounts);
		logger::info(
			"Config: QuickTrade.QuickTradeAllWorldMounts={}",
			g_settings.quickTrade.quickTradeAllWorldMounts);
		logger::info(
			"Config: KeyboardControls.ModKeyDik={}",
			g_settings.keyboardControls.modKeyDik);
		logger::info(
			"Config: KeyboardControls.SkyuiEquipModeKey={}",
			g_settings.keyboardControls.skyuiEquipModeKey);
		logger::info(
			"Config: GamepadControls.GamepadModKey={}",
			g_settings.gamepadControls.gamepadModKey);
		logger::info(
			"Config: GamepadControls.GamepadRightHandKey={}",
			g_settings.gamepadControls.gamepadRightHandKey);
		logger::info(
			"Config: GamepadControls.GamepadLeftHandKey={}",
			g_settings.gamepadControls.gamepadLeftHandKey);
		logger::info(
			"Config: KeyboardControls.QuickTradeKeyDik={}",
			g_settings.keyboardControls.quickTradeKeyDik);
		logger::info(
			"Config: GamepadControls.QuickTradeGamepadKey={}",
			g_settings.gamepadControls.quickTradeGamepadKey);
		logger::info(
			"Config: ButtonIndicators.EnableModKeyIndicator={}",
			g_settings.buttonIndicators.enableModKeyIndicator);
		logger::info(
			"Config: ButtonIndicators.ModKeyIndicatorText={}",
			g_settings.buttonIndicators.modKeyIndicatorText);
		logger::info(
			"Config: ButtonIndicators.InclusionIndicatorText={}",
			g_settings.buttonIndicators.inclusionIndicatorText);
		logger::info(
			"Config: ButtonIndicators.SummonIndicatorText={}",
			g_settings.buttonIndicators.summonIndicatorText);
		logger::info(
			"Config: ButtonIndicators.EquipModeTextOverride={}",
			g_settings.buttonIndicators.equipModeTextOverride);
		logger::info(
			"Config: EquipModeItems.Enable={} EnableWeapons={} EnableArmor={} EnableAmmo={} EnableTorches={} EnableScrolls={} ",
			g_settings.equipModeItems.enable,
			g_settings.equipModeItems.enableWeapons,
			g_settings.equipModeItems.enableArmor,
			g_settings.equipModeItems.enableAmmo,
			g_settings.equipModeItems.enableTorches,
			g_settings.equipModeItems.enableScrolls);
		logger::info(
			"Config: EquipModeConsumableMode.EnableConsumableMode={} EnablePotions={} EnableFoods={} EnableDrinks={} EnableIngredients={} EnableIngredientDiscoveryForPlayer={} ",
			g_settings.equipModeConsumableMode.enableConsumableMode,
			g_settings.equipModeConsumableMode.enablePotions,
			g_settings.equipModeConsumableMode.enableFoods,
			g_settings.equipModeConsumableMode.enableDrinks,
			g_settings.equipModeConsumableMode.enableIngredients,
			g_settings.equipModeConsumableMode.enableIngredientDiscoveryForPlayer);
		logger::info(
			"Config: EquipModePoisonMode.Mode={} EnableStacking={} MaxCharges={} ",
			static_cast<std::uint32_t>(g_settings.equipModePoisonMode.mode),
			g_settings.equipModePoisonMode.enableStacking,
			g_settings.equipModePoisonMode.maxCharges);
		logger::info(
			"Config: EquipModeSpellTomeMode.EnableSpellTomeMode={} ",
			g_settings.equipModeSpellTomeMode.enableSpellTomeMode);
		logger::info(
			"Config: CombatEquipPreference.EnableScoring={} EnableInstanceAlign={} EnableClearPreferencesOnUnequip={} EnableClearLeftHandPreferenceOnUnequip={} ",
			g_settings.combatEquipPreference.enableScoring,
			g_settings.combatEquipPreference.enableInstanceAlign,
			g_settings.combatEquipPreference.enableClearPreferencesOnUnequip,
			g_settings.combatEquipPreference.enableClearLeftHandPreferenceOnUnequip);
		logger::info(
			"Config: CombatEquipEnforcement.EnableMeleeEnforcement={} EnableMeleeEnforcementEmptyHand={} EnableAmmoPreference={} ",
			g_settings.combatEquipEnforcement.enableMeleeEnforcement,
			g_settings.combatEquipEnforcement.enableMeleeEnforcementEmptyHand,
			g_settings.combatEquipEnforcement.enableAmmoPreference);
		logger::info(
			"Config: CombatEquipRestore.EnableRestorePreCombatOnExit={} EnableHeadgearAutoEquip={} EnableInfiniteAmmo={} ",
			g_settings.combatEquipRestore.enableRestorePreCombatOnExit,
			g_settings.combatEquipRestore.enableHeadgearAutoEquip,
			g_settings.combatEquipRestore.enableInfiniteAmmo);
		logger::info(
			"Config: IconAppearance.EnableCombatEquipIcon={} EnableHeadgearIcon={} EnableOutfitSyncIcon={} ",
			g_settings.iconAppearance.enableCombatEquipIcon,
			g_settings.iconAppearance.enableHeadgearIcon,
			g_settings.iconAppearance.enableOutfitSyncIcon);
		logger::info(
			"Config: OutfitSync.EnableUpdateNpcOutfitSuppression={} EnableOutfitSnapshotRestore={} AllowOutfitChanges={} ",
			g_settings.outfitSync.enableUpdateNpcOutfitSuppression,
			g_settings.outfitSync.enableOutfitSnapshotRestore,
			g_settings.outfitSync.allowOutfitChanges);
		logger::info(
			"Config: HiddenItems.EnableNonPlayableItems={} RemoveHiddenArmor={} RemoveHiddenWeapon={} RemoveHiddenAmmo={} EnableOutfitItems={} EnableRevealDefaultOutfitItems={} EnableRevealExternalOutfitItems={} ",
			g_settings.hiddenItems.enableNonPlayableItems,
			g_settings.hiddenItems.removeHiddenArmor,
			g_settings.hiddenItems.removeHiddenWeapon,
			g_settings.hiddenItems.removeHiddenAmmo,
			g_settings.hiddenItems.enableOutfitItems,
			g_settings.hiddenItems.enableRevealDefaultOutfitItems,
			g_settings.hiddenItems.enableRevealExternalOutfitItems);
		logger::info(
			"Config: ItemInjectionBlocking.EnableOutfitItemBlocker={} EnableLeveledItemBlocker={} ",
			g_settings.itemInjectionBlocking.enableOutfitItemBlocker,
			g_settings.itemInjectionBlocking.enableLeveledItemBlocker);
		logger::info(
			"Config: LootBlocking.EnablePreventCombatLoot={} EnablePreventContainerLoot={} EnablePreventPickupObject={} ",
			g_settings.lootBlocking.enablePreventCombatLoot,
			g_settings.lootBlocking.enablePreventContainerLoot,
			g_settings.lootBlocking.enablePreventPickupObject);
		logger::info(
			"Config: AutoEquipBlocking.EnableNonCombatEquipBlocker={} EnableBestWeaponAutoEquipSuppressor={}",
			g_settings.autoEquipBlocking.enableNonCombatEquipBlocker,
			g_settings.autoEquipBlocking.enableBestWeaponAutoEquipSuppressor);
		logger::info(
			"Config: EquipGate.EnableEquipBlocking={} EnableUnequipBlocking={} NeverBlockTorchEquip={}",
			g_settings.equipGate.enableEquipBlocking,
			g_settings.equipGate.enableUnequipBlocking,
			g_settings.equipGate.enableNeverBlockTorchEquip);
		logger::info(
			"Config: WeaponEnchantmentRecharge.EnableRecharge={} ",
			g_settings.weaponEnchantmentRecharge.enableRecharge);
		logger::info(
			"Config: WeaponEnchantmentRecharge.RechargeKeyDik={} ",
			g_settings.weaponEnchantmentRecharge.rechargeKeyDik);
		logger::info(
			"Config: WeaponEnchantmentRecharge.GamepadRechargeKey={} ",
			g_settings.weaponEnchantmentRecharge.gamepadRechargeKey);
		logger::info(
			"Config: WeaponEnchantmentRecharge.EnableUIIndicator={} ",
			g_settings.weaponEnchantmentRecharge.enableUIIndicator);
		logger::info(
			"Config: WeaponEnchantmentRecharge.AdvancedSoulGemDisplay={} ",
			g_settings.weaponEnchantmentRecharge.advancedSoulGemDisplay);
		logger::info(
			"Config: WeaponEnchantmentRecharge.ShowStackCount={} ContainedSoulDisplayMode={} SourceDisplayMode={} SoulGemSortMode={} ",
			g_settings.weaponEnchantmentRecharge.showStackCount,
			static_cast<std::uint32_t>(g_settings.weaponEnchantmentRecharge.containedSoulDisplayMode),
			static_cast<std::uint32_t>(g_settings.weaponEnchantmentRecharge.sourceDisplayMode),
			static_cast<std::uint32_t>(g_settings.weaponEnchantmentRecharge.soulGemSortMode));
		logger::info(
			"Config: StatsDisplay.EnableFollowerStatsInTradeMenu={} ",
			static_cast<std::uint32_t>(g_settings.statsDisplay.followerStatsInTradeMenuMode));
		logger::info(
			"Config: CorpseEquipMode.Enable={} IndicatorText={}",
			g_settings.corpseEquipMode.enable,
			g_settings.corpseEquipMode.indicatorText);
		logger::info(
			"Config: EngineFixes.EnableStaleWeightCacheFix={} ",
			g_settings.engineFixes.enableStaleWeightCacheFix);
		logger::info(
			"Config: SkyUIFixes.EnableQuantityMenuBlocker={} EnableZeroWeightFix={} ",
			g_settings.skyUIFixes.enableQuantityMenuBlocker,
			g_settings.skyUIFixes.enableZeroWeightFix);
	}

	const Settings& Get()
	{
		return g_settings;
	}

	void Set(const Settings& a_settings)
	{
		g_settings = a_settings;
		g_loaded = true;
	}

	bool Save(const Settings& a_settings)
	{
		auto iniPath = GetIniPathNearDll();
		if (!iniPath) {
			logger::warn("Config: INI path unavailable; cannot save");
			return false;
		}

		Settings normalized = a_settings;
		auto logLevel = ToLower(Trim(normalized.logging.logLevel));
		if (logLevel.empty()) {
			logLevel = "info";
		}
		normalized.logging.logLevel = std::move(logLevel);

		std::ofstream out(*iniPath, std::ios::binary | std::ios::trunc);
		if (!out.is_open()) {
			logger::warn("Config: failed to open INI for writing '{}'", iniPath->string());
			return false;
		}

		out << DefaultIniContents(normalized);
		out.close();

		logger::info("Config saved to '{}'", iniPath->string());
		return true;
	}

	bool Save()
	{
		return Save(g_settings);
	}
}
