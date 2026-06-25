#include "Localization.h"

#include "Plugin.h"

#include "Logging.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

namespace FEC::Localization
{
	namespace
	{
		using Table = std::unordered_map<std::string, std::string>;

		Table g_table;
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

		[[nodiscard]] std::string UnescapeValue(std::string s)
		{
			std::string out;
			out.reserve(s.size());

			for (std::size_t i = 0; i < s.size(); ++i) {
				const char c = s[i];
				if (c != '\\' || i + 1 >= s.size()) {
					out.push_back(c);
					continue;
				}

				const char n = s[i + 1];
				switch (n) {
				case 'n':
					out.push_back('\n');
					++i;
					break;
				case 'r':
					out.push_back('\r');
					++i;
					break;
				case 't':
					out.push_back('\t');
					++i;
					break;
				case '\\':
					out.push_back('\\');
					++i;
					break;
				default:
					// Unknown escape sequence: keep as-is.
					out.push_back(c);
					break;
				}
			}

			return out;
		}

		[[nodiscard]] std::optional<std::filesystem::path> GetThisDllDirectory()
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&FEC::Localization::Load),
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

		struct LanguageFileSelection
		{
			std::optional<std::filesystem::path> primary;
			std::optional<std::filesystem::path> fallbackEn;
		};

		[[nodiscard]] LanguageFileSelection SelectLanguageFile()
		{
			LanguageFileSelection sel;

			auto dllDir = GetThisDllDirectory();
			if (!dllDir) {
				return sel;
			}

			const auto langDir = (*dllDir / std::string(Plugin::NAME) / "Localization");
			std::error_code ec;
			if (!std::filesystem::exists(langDir, ec) || ec) {
				return sel;
			}

			std::vector<std::filesystem::path> nonEn;
			std::optional<std::filesystem::path> en;

			for (std::filesystem::directory_iterator it(langDir, ec); !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
				if (ec) {
					break;
				}

				const auto& entry = *it;
				if (!entry.is_regular_file(ec) || ec) {
					ec.clear();
					continue;
				}

				const auto p = entry.path();
				if (ToLower(p.extension().string()) != ".ini") {
					continue;
				}

				const auto filename = ToLower(p.filename().string());
				if (filename == "en.ini") {
					en = p;
				} else {
					nonEn.push_back(p);
				}
			}

			// Priority rules:
			// 1) Exactly one non-en.ini -> use it. (If none or multiple, go to #2)
			// 2) en.ini if present.
			// 3) Built-in English defaults.
			if (nonEn.size() == 1) {
				sel.primary = nonEn.front();
				sel.fallbackEn = en;
				return sel;
			}
			if (en) {
				sel.primary = *en;
				return sel;
			}

			return sel;
		}

		void LoadDefaultsEnglish(Table& out)
		{
			out.clear();
			out.reserve(128);

			// General
			out.emplace("ui.title", "Follower Equip Control");
			out.emplace("ui.section.actor_management", "ACTOR MANAGEMENT");
			out.emplace("ui.section.preferences_and_spells", "Preferences & Spell List");
			out.emplace("ui.section.settings", "SETTINGS");
			out.emplace("ui.section.system", "System");
			out.emplace("ui.section.scope_and_access", "Scope & Access");
			out.emplace("ui.section.controls_and_ui", "Controls & UI");
			out.emplace("ui.section.equip_mode", "Equip Mode");
			out.emplace("ui.section.equip_stability", "Equip Stability");
			out.emplace("ui.section.combat_equip", "Combat Equip");
			out.emplace("ui.section.extra_features", "Extra Features");
			out.emplace("ui.section.fixes", "Fixes");

			out.emplace("ui.toolbar.revert", "REVERT");
			out.emplace("ui.toolbar.save", "SAVE");
			out.emplace("ui.toolbar.reset", "RESET");

			out.emplace("ui.tooltip.default_action", "Click to reset this setting to its default value.");
			out.emplace("ui.tooltip.default_value_prefix", "- Default:");
			out.emplace("ui.tooltip.revert", "Discard unsaved changes and restore the last saved values.");
			out.emplace("ui.tooltip.reset", "Reset this page's settings to defaults (press SAVE to keep the change).");
			out.emplace("ui.tooltip.unsaved", "You have unsaved changes (press SAVE to apply).");
			out.emplace("ui.tooltip.mod_disabled", "Mod is currently disabled (activate it in the System tab, then press SAVE).");

			out.emplace("ui.value.enabled", "Enabled");
			out.emplace("ui.value.disabled", "Disabled");
			out.emplace("ui.common.cancel", "Cancel");

			// Actor Management
			out.emplace("ui.actor_management.exclude_confirm.confirm", "Exclude");
			out.emplace("ui.actor_management.exclude_confirm.tooltip", "Exclude this actor from all mod features");
			out.emplace("ui.actor_management.exclude_confirm.body", "Exclude this actor from all mod features?\n\nThe mod will stop tracking and managing this actor. All existing data (preferences, spell suppressions, etc.) is preserved. You can re-include the actor at any time.");
			out.emplace("ui.actor_management.excluded_placeholder", "This actor is excluded from mod features. Re-include them first to display this page.");
			out.emplace("ui.actor_management.include_confirm.confirm", "Re-include");
			out.emplace("ui.actor_management.include_confirm.tooltip", "Re-include this actor again");
			out.emplace("ui.actor_management.include_confirm.body", "Re-include this actor again?\n\nThe mod will resume tracking and managing this actor normally.");
			out.emplace("ui.actor_management.remove_confirm.confirm", "Remove");
			out.emplace("ui.actor_management.remove_confirm.tooltip", "Remove this actor and delete all saved data for them");
			out.emplace("ui.actor_management.remove_confirm.body", "Remove this actor from the mod?\n\nAll mod records for this actor will be permanently deleted. If you interact with this actor through the trade menu, the mod will begin managing them again.");
			out.emplace("ui.actor_management.remove_confirm.body_excluded", "Remove this actor from the mod?\n\nAll mod records for this actor will be permanently deleted.\n\nNote: After the removal, if you interact with this actor through the trade menu, the mod will begin managing them again. To keep them excluded, you will need to exclude them again.");
			out.emplace("ui.actor_management.filter.former_followers.show", "Show former followers");
			out.emplace("ui.actor_management.filter.former_followers.hide", "Hide former followers");
			out.emplace("ui.actor_management.filter.summons.show", "Show summoned actors");
			out.emplace("ui.actor_management.filter.summons.hide", "Hide summoned actors");
			out.emplace("ui.actor_management.filter.inclusion_actors.show", "Show included actors");
			out.emplace("ui.actor_management.filter.inclusion_actors.hide", "Hide included actors");
			out.emplace("ui.actor_management.filter.excluded_actors.show", "Show excluded actors");
			out.emplace("ui.actor_management.filter.excluded_actors.hide", "Hide excluded actors");
			out.emplace("ui.actor_management.view.preferences", "PREFERENCES");
			out.emplace("ui.actor_management.view.spell_list", "SPELL LIST");

			// Preference Capture (read-only)
			out.emplace("ui.preference_capture.empty", "No captured preferences.");
			out.emplace("ui.preference_capture.help", "This page shows all combat preferences the mod has recorded for each follower.\n\nWhile trading with a follower, a preference is recorded each time you equip an item using the Mod Key. The mod stores these selections, and the Combat Equip settings use them to keep followers fighting with the items you chose. Use this table as a reference to check what is currently saved for each follower.");
			out.emplace("ui.preference_capture.category.one_hand_right", "Right-Hand");
			out.emplace("ui.preference_capture.category.one_hand_left", "Left-Hand");
			out.emplace("ui.preference_capture.category.shield_left", "Shield");
			out.emplace("ui.preference_capture.category.two_hand", "Two-Handed");
			out.emplace("ui.preference_capture.category.bow", "Bow");
			out.emplace("ui.preference_capture.category.crossbow", "Crossbow");
			out.emplace("ui.preference_capture.category.arrow", "Arrow");
			out.emplace("ui.preference_capture.category.bolt", "Bolt");
			out.emplace("ui.preference_capture.category.staff_right", "Right-Hand Staff");
			out.emplace("ui.preference_capture.category.staff_left", "Left-Hand Staff");
			out.emplace("ui.preference_capture.category.scroll_right", "Right-Hand Scroll");
			out.emplace("ui.preference_capture.category.scroll_left", "Left-Hand Scroll");
			out.emplace("ui.preference_capture.category.scroll_both", "Two-Handed Scroll");
			out.emplace("ui.preference_capture.category.unknown", "?");
			out.emplace("ui.preference_capture.none", "-");
			out.emplace("ui.preference_capture.signature.custom_name", "Custom Name: {0}");
			out.emplace("ui.preference_capture.signature.temper_quality", "Temper Quality: {0}");
			out.emplace("ui.preference_capture.signature.enchantment", "Enchantment: {0}");
			out.emplace("ui.preference_capture.signature.charge_capacity", "Charge Capacity: {0}");
			out.emplace("ui.preference_capture.cross_clear.show", "Show cross-clear reference");
			out.emplace("ui.preference_capture.cross_clear.help", "This table is a reference showing which preferences are automatically cleared when a new preference is set.\n\nAn X in a cell means setting the row's preference will automatically clear the column's preference. Preferences within the same group can conflict, setting one clears incompatible others.\n\nChoosing a two-handed weapon clears one-handed and shield preferences. Choosing a one-handed left weapon or a shield clears the other left-hand slot and also clears two-handed. Choosing a one-handed right weapon only clears two-handed.\n\nBow and Crossbow clear each other. Scroll (Right) and Scroll (Left) clear Scroll (2H), and Scroll (2H) clears both.\n\nArrows, Bolts, and Staves never clear anything.");

			// Spell List
			out.emplace("ui.spell_list.empty", "No learned spells.");
			out.emplace("ui.spell_list.help", "This page lists every spell the selected follower knows and lets you control which ones they can use in combat.\n\nWhen a follower has spells you do not want them using in combat, select the spell and press FORGET to remove it from active use. The follower stops casting that spell, and you can press REMEMBER at any time to restore it.\n\nSpells marked BASE SPELL are built into the follower's original character and cannot be removed. FORGET only works on spells they gained during gameplay.");
			out.emplace("ui.spell_list.detail.select_hint", "Select a spell to view details.");
			out.emplace("ui.spell_list.school.other", "Other");
			out.emplace("ui.spell_list.school.alteration", "Alteration");
			out.emplace("ui.spell_list.school.conjuration", "Conjuration");
			out.emplace("ui.spell_list.school.destruction", "Destruction");
			out.emplace("ui.spell_list.school.illusion", "Illusion");
			out.emplace("ui.spell_list.school.restoration", "Restoration");
			out.emplace("ui.spell_list.tier.novice", "Novice");
			out.emplace("ui.spell_list.tier.apprentice", "Apprentice");
			out.emplace("ui.spell_list.tier.adept", "Adept");
			out.emplace("ui.spell_list.tier.expert", "Expert");
			out.emplace("ui.spell_list.tier.master", "Master");
			out.emplace("ui.spell_list.toggle.suppress", "FORGET");
			out.emplace("ui.spell_list.toggle.suppress_tooltip", "This follower will forget the selected spell. It can be restored later.");
			out.emplace("ui.spell_list.toggle.unsuppress", "REMEMBER");
			out.emplace("ui.spell_list.toggle.unsuppress_tooltip", "This follower will remember the selected spell again.");
			out.emplace("ui.spell_list.toggle.casting", "CASTING");
			out.emplace("ui.spell_list.toggle.casting_warning", "This spell is currently being cast, wait for it to finish before forgetting.");
			out.emplace("ui.spell_list.toggle.base_spell", "BASE SPELL");
			out.emplace("ui.spell_list.toggle.base_spell_tooltip", "This spell is part of the actor's base NPC record and cannot be removed. Removing it would affect all NPCs that share the same template. Only spells added during gameplay can be removed.");
			out.emplace("ui.spell_list.detail.stat.magnitude", "Magnitude");
			out.emplace("ui.spell_list.detail.stat.duration", "Duration");
			out.emplace("ui.spell_list.detail.stat.area", "Area");
			out.emplace("ui.spell_list.detail.stat.empty", "-");
			out.emplace("ui.spell_list.detail.label.cost", "cost");
			out.emplace("ui.spell_list.detail.label.per_sec", "per sec");
			out.emplace("ui.spell_list.detail.label.level", "level");

			// System > Core Settings
			out.emplace("ui.header.core", "Core Settings");
			out.emplace("ui.system.enable_mod.label", "Activate Mod");
			out.emplace("ui.system.enable_mod.help", "Enables all mod features.");

			// System > UI Feedback
			out.emplace("ui.header.ui_feedback", "UI Feedback");
			out.emplace("ui.notifications.enable.label", "Notifications");
			out.emplace("ui.notifications.enable.help", "Shows a brief on-screen message when the mod performs certain actions on a follower.");
			out.emplace("ui.uisounds.enable.label", "UI Sounds");
			out.emplace("ui.uisounds.enable.help", "Plays vanilla UI sounds when the mod performs certain actions on a follower.");

			// System > Logging
			out.emplace("ui.header.logging", "Logging");
			out.emplace("ui.logging.level.label", "Log Level");
			out.emplace("ui.logging.level.help", "Controls how much detail the mod writes to the SKSE log file.");

			// Scope & Access > Actor Scope
			out.emplace("ui.header.actor_scope", "Actor Scope");
			out.emplace("ui.actor_scope.affect_former_followers.label", "Former Followers");
			out.emplace("ui.actor_scope.affect_former_followers.help", "Extends all mod features to dismissed and former followers.\n\nThe mod continues managing equipment and preferences for these followers even after you dismiss them. Without this setting, only followers currently in your party are affected.");
			out.emplace("ui.actor_scope.include_inclusion_actors.label", "Included Actors");
			out.emplace("ui.actor_scope.include_inclusion_actors.help", "Includes actors matched by the configured inclusion system in the mod scope.\n\nNot all follower mods register their NPCs as active party members, which means the mod does not recognize them by default. The inclusion system lets you specify actors, keywords, or factions so the mod manages them like any other follower. This lets mod authors and users include any actor they want, not just those from follower mods.");
			out.emplace("ui.actor_scope.include_non_humanoid_inclusion_actors.label", "Non-Humanoid Included Actors");
			out.emplace("ui.actor_scope.include_non_humanoid_inclusion_actors.help", "Removes equipment restrictions on non-humanoid actors admitted via the inclusion system.\n\nWhen disabled:\n- Non-humanoid actors (atronachs, creatures, etc.) are completely excluded from the mod scope.\n- Actors that cannot normally equip armor (draugr, skeletons, falmer, etc.) remain limited to weapons and ammo, matching Skyrim's native behavior.\n\nWhen enabled, all non-humanoid inclusion actors receive the full mod feature set, including armor equip (which may not apply gameplay effects or render visually).");
			out.emplace("ui.actor_scope.include_player_summons.label", "Player Summons");
			out.emplace("ui.actor_scope.include_player_summons.help", "Includes all player-commanded actors in the mod scope, such as summoned creatures, reanimated corpses, and similar actors controlled through Skyrim's command systems.\n\nWhen disabled, these actors are ignored entirely by the mod.");
			out.emplace("ui.actor_scope.include_non_humanoid_summons.label", "Non-Humanoid Summons");
			out.emplace("ui.actor_scope.include_non_humanoid_summons.help", "Removes equipment restrictions on non-humanoid player-commanded actors.\n\nWhen disabled:\n- Non-humanoid actors (wolves, atronachs, spiders, etc.) are completely excluded from the mod scope.\n- Actors that cannot normally equip armor (draugr, skeletons, falmer, etc.) remain limited to weapons and ammo, matching Skyrim's native behavior.\n\nWhen enabled, all non-humanoid actors receive the full mod feature set, including armor equip (which may not apply gameplay effects or render visually).");

			// Scope & Access > Quick Trade
			out.emplace("ui.header.quick_trade", "Quick Trade");
			out.emplace("ui.quick_trade.enable.label", "Enable Quick Trade");
			out.emplace("ui.quick_trade.enable.help", "Enables opening the trade menu directly without going through a dialogue menu.\n\nHold the Quick Trade Key and press Activate on an eligible actor to open the trade menu directly, without going through any dialogue. This is especially useful for mods that add actors without a trade dialogue option, or when you want to trade with a follower without talking to them. When disabled, the Quick Trade hotkey does nothing and all interactions go through normal dialogue.");
			out.emplace("ui.quick_trade.followers.label", "Followers");
			out.emplace("ui.quick_trade.followers.help", "Enables Quick Trade for active followers.\n\nHold the Quick Trade Key and press Activate on a follower currently in your party to open the trade menu directly instead of normal dialogue. When disabled, Quick Trade ignores active followers.");
			out.emplace("ui.quick_trade.former_followers.label", "Former Followers");
			out.emplace("ui.quick_trade.former_followers.help", "Enables Quick Trade for former followers.\n\nHold the Quick Trade Key and press Activate on a dismissed follower to open the trade menu directly. This only works while the Actor Scope setting Former Followers is enabled; otherwise Quick Trade ignores former followers.");
			out.emplace("ui.quick_trade.inclusion_actors.label", "Included Actors");
			out.emplace("ui.quick_trade.inclusion_actors.help", "Enables Quick Trade for Included Actors.\n\nHold the Quick Trade Key and press Activate on an actor added through the inclusion system to open the trade menu directly. This only works while the Actor Scope setting Included Actors is enabled.");
			out.emplace("ui.quick_trade.player_summons.label", "Player Summons");
			out.emplace("ui.quick_trade.player_summons.help", "Enables Quick Trade for player-commanded actors.\n\nHold the Quick Trade Key and press Activate on a summon, reanimated corpse, or similar player-commanded actor to open the trade menu directly. This only works while the Actor Scope setting Player Summons is enabled.\n\nItems given to spectral or daedric summons may be permanently lost if the summon dies before you can retrieve them. These actors disappear along with their inventory, so avoid giving important items to these summons.");
			out.emplace("ui.quick_trade.non_humanoid_actors.label", "Non-Humanoid Actors");
			out.emplace("ui.quick_trade.non_humanoid_actors.help", "Enables Quick Trade for non-humanoid actors.\n\nHold the Quick Trade Key and press Activate on a non-humanoid actor to open the trade menu directly. This only opens trade for item transfer; the mod's follower-management systems do not apply to these actors.\n\nTypical examples are reanimated creatures such as wolves, trolls, and spiders, as well as summoned actors such as atronachs, familiars, and other creatures created through conjuration spells. Custom non-humanoid companions, creatures, and other actors added by mods can likewise be accessed through Quick Trade.");
			out.emplace("ui.quick_trade.mounts.label", "Mounts");
			out.emplace("ui.quick_trade.mounts.help", "Enables Quick Trade for your followers' mounts and your last ridden mount.\n\nHold the Quick Trade Key and press Activate on mounts to open the trade menu. This only opens trade for item transfer; the mod's features do not work on these mounts.\n\nThe player's last ridden mount stays tracked after dismounting, but follower mounts are only tracked while actively ridden, so you can only Quick Trade with a follower's mount while they are currently riding it. If you want to access a mount that is not covered by these conditions, you can use the following setting.");
			out.emplace("ui.quick_trade.all_world_mounts.label", "All Mounts");
			out.emplace("ui.quick_trade.all_world_mounts.help", "Expands the Quick Trade scope to cover all rideable creatures.\n\nHold the Quick Trade Key and press Activate on any rideable creature to open the trade menu. If Quick Trade does not work on your followers' mounts or your own mount due to a mount mod you use, enable this to expand coverage to all rideable creatures.\n\nIf enabled, Quick Trade can be used on all horses and rideable creatures. This includes mounts in stables or owned by other NPCs.");

			// Controls & UI > Keyboard Controls
			out.emplace("ui.header.keyboard_controls", "Keyboard Controls");
			out.emplace("ui.controls.mod_key.label", "Mod Key");
			out.emplace("ui.controls.mod_key.help", "Sets the key you hold while trading with a follower to activate mod features.");
			out.emplace("ui.controls.skyui_equip_mode_key.label", "Equip Mode Key Override");
			out.emplace("ui.controls.skyui_equip_mode_key.help", "Sets the key you hold while trading with a follower to activate Equip Mode for the player.\n\nOverrides SkyUI's own Equip Mode key setting.\n\nSet to 'Disabled' to use SkyUI's own key.");
			out.emplace("ui.controls.skyui_equip_mode_key.none", "Disabled");
			out.emplace("ui.controls.quick_trade_key.label", "Quick Trade Key");
			out.emplace("ui.controls.quick_trade_key.help", "Sets the modifier key you hold while pressing Activate to open the Quick Trade menu directly with eligible actors.\n\nSet to 'Sprint Key' to use the game's Sprint binding.");
			out.emplace("ui.controls.quick_trade_key.none", "Sprint Key");

			// Controls & UI > Gamepad Controls
			out.emplace("ui.header.gamepad_controls", "Gamepad Controls");
			out.emplace("ui.controls.gamepad_mod_key.label", "Mod Key");
			out.emplace("ui.controls.gamepad_mod_key.help", "Sets the gamepad button used as the Mod Key while trading with a follower.\n\nHold this button and press another button to trigger mod actions (e.g. equip right hand, left hand).");
			out.emplace("ui.controls.gamepad_right_hand_key.label", "Right Hand Key");
			out.emplace("ui.controls.gamepad_right_hand_key.help", "Sets the gamepad button for right-hand equip while the Gamepad Mod Key is held.");
			out.emplace("ui.controls.gamepad_left_hand_key.label", "Left Hand Key");
			out.emplace("ui.controls.gamepad_left_hand_key.help", "Sets the gamepad button for left-hand equip while the Gamepad Mod Key is held.");
			out.emplace("ui.controls.quick_trade_gamepad_key.label", "Quick Trade Key");
			out.emplace("ui.controls.quick_trade_gamepad_key.help", "Sets the gamepad modifier button you hold while pressing Activate to open the Quick Trade menu directly with eligible actors.\n\nSet to 'Sprint Key' to use the game's Sprint binding.");
			out.emplace("ui.controls.quick_trade_gamepad_key.none", "Sprint Key");

			// Controls & UI > UI Button Indicators
			out.emplace("ui.header.ui_button_indicators", "UI Button Indicators");
			out.emplace("ui.controls.mod_key_indicator.label", "Mod Key Indicator");
			out.emplace("ui.controls.mod_key_indicator.help", "Adds a Mod Key indicator in the SkyUI bottom bar while trading with a follower.");
			out.emplace("ui.controls.indicator_text.label", "Indicator Text");
			out.emplace("ui.controls.indicator_text.default", "Equip Follower");
			out.emplace("ui.controls.indicator_text.help", "Sets the text shown on the Mod Key indicator in the SkyUI bottom bar.");
			out.emplace("ui.controls.inclusion_indicator_text.label", "Included Indicator Text");
			out.emplace("ui.controls.inclusion_indicator_text.default", "Equip Target");
			out.emplace("ui.controls.inclusion_indicator_text.help", "Sets the text shown on the Mod Key indicator when the target is a list-included actor. Leave empty to use the follower indicator text.");
			out.emplace("ui.controls.summon_indicator_text.label", "Summon Indicator Text");
			out.emplace("ui.controls.summon_indicator_text.default", "Equip Summon");
			out.emplace("ui.controls.summon_indicator_text.help", "Sets the text shown on the Mod Key indicator when the target is a player-summoned actor. Leave empty to use the follower indicator text.");
			out.emplace("ui.controls.equip_mode_text_override.label", "Equip Mode Text Override");
			out.emplace("ui.controls.equip_mode_text_override.default", "Equip Player");
			out.emplace("ui.controls.equip_mode_text_override.help", "Overrides the Equip Mode label in the SkyUI bottom bar while trading with a follower.\n\nSkyUI shows its own Equip Mode text by default. This setting lets you replace it with custom text. Leave it empty to use the localized default.");

			// Controls & UI > UI Icon Indicators
			out.emplace("ui.header.ui_icon_indicators", "UI Icon Indicators");
			out.emplace("ui.icon_appearance.enable_indicators.label", "Icon Indicators");
			out.emplace("ui.icon_appearance.enable_indicators.help", "Enables the mod's icons next to item names in the trade menu.\n\nWhile trading with a follower, icons appear next to items that have a combat preference, headgear preference, or re-equip selection. The toggles below control which icon types are shown.");
			out.emplace("ui.combat_equip.combat_preference_icon.label", "Combat Preference Icons");
			out.emplace("ui.combat_equip.combat_preference_icon.help", "Displays an icon next to items in the trade list that are saved as combat preferences.\n\nWhile trading with a follower, weapons, shields, and ammo you have saved as combat preferences are marked with a small icon. This lets you see which items the follower will prioritize to equip in combat.");
			out.emplace("ui.combat_equip.headgear_preference_icon.label", "Headgear Preference Icons");
			out.emplace("ui.combat_equip.headgear_preference_icon.help", "Displays an icon next to the headgear item in the trade list that is saved as the headgear preference.\n\nWhile trading with a follower, the helmet, hood, or circlet saved as the headgear preference is marked with a small icon. This lets you see which headgear the follower will equip when combat starts.\n\nNote: To change the headgear preference, hold the Mod Key and press left-hand equip on the selected headgear item.");
			out.emplace("ui.outfit_sync.reequip_selection_icon.label", "Re-Equip Selection Icons");
			out.emplace("ui.outfit_sync.reequip_selection_icon.help", "Displays an icon next to items in the trade list that are saved for re-equip.\n\nWhile trading with a follower, weapons, armor, and ammo you have saved as preferred for re-equip are marked with a small icon. This lets you see which items the mod will re-equip after the game rebuilds the follower's inventory.");
			out.emplace("ui.icon_appearance.enable_customization.label", "Icon Customization");
			out.emplace("ui.icon_appearance.enable_customization.help", "Enables size and spacing customization for the mod icons. When disabled, icons are shown at default size and spacing.");
			out.emplace("ui.icon_appearance.icon_size.label", "Icon Size");
			out.emplace("ui.icon_appearance.icon_size.help", "Sets the size of the mod icons shown next to item names in the trade menu.");
			out.emplace("ui.icon_appearance.gap_after_text.label", "Gap After Text");
			out.emplace("ui.icon_appearance.gap_after_text.help", "Sets the horizontal gap between the item name and the first mod icon when no other icons are present.");
			out.emplace("ui.icon_appearance.gap_after_icon.label", "Gap After Icon");
			out.emplace("ui.icon_appearance.gap_after_icon.help", "Sets the horizontal gap between the rightmost icon placed by SkyUI and the first mod icon.");
			out.emplace("ui.icon_appearance.icon_spacing.label", "Icon Spacing");
			out.emplace("ui.icon_appearance.icon_spacing.help", "Sets the horizontal gap between mod icons when multiple are visible on the same item.");

			// Equip Mode > Equippable Items
			out.emplace("ui.header.equippable_items", "Equippable Items");
			out.emplace("ui.equip_mode.enable.label", "Enable Equipping");
			out.emplace("ui.equip_mode.enable.help", "Allows equipping items on followers while trading.\n\nWhile trading with a follower, hold the Mod Key and click an item to make them equip it. The toggles below control which item types can be equipped this way.");
			out.emplace("ui.equip_mode.weapons.label", "Weapons");
			out.emplace("ui.equip_mode.weapons.help", "Allows weapons to be equipped on followers.");
			out.emplace("ui.equip_mode.armor.label", "Armor");
			out.emplace("ui.equip_mode.armor.help", "Allows armor and shields to be equipped on followers.");
			out.emplace("ui.equip_mode.ammo.label", "Ammo");
			out.emplace("ui.equip_mode.ammo.help", "Allows ammo (arrows and bolts) to be equipped on followers.");
			out.emplace("ui.equip_mode.torches.label", "Torches");
			out.emplace("ui.equip_mode.torches.help", "Allows torches to be equipped on followers.");
			out.emplace("ui.equip_mode.scrolls.label", "Scrolls");
			out.emplace("ui.equip_mode.scrolls.help", "Allows scrolls to be equipped on followers.");

			// Runtime feedback: Equip Mode
			out.emplace("feedback.equip_mode.unsupported_item", "{0} cannot equip this item.");
			out.emplace("feedback.equip_mode.armor_blocked_hand_only", "{0} cannot equip armor.");

			// Equip Mode > Consumables
			out.emplace("ui.header.consumables", "Consumables");
			out.emplace("ui.consumable.enable.label", "Enable Consuming");
			out.emplace("ui.consumable.enable.help", "Allows consuming items on followers while trading.\n\nWhile trading with a follower, hold the Mod Key and click a supported item to make the follower consume it. The toggles below control which item types can be consumed this way.");
			out.emplace("ui.consumable.potions.label", "Potions");
			out.emplace("ui.consumable.potions.help", "Allows potions to be consumed by followers.");
			out.emplace("ui.consumable.foods.label", "Foods");
			out.emplace("ui.consumable.foods.help", "Allows food to be consumed by followers.");
			out.emplace("ui.consumable.drinks.label", "Drinks");
			out.emplace("ui.consumable.drinks.help", "Allows drinks to be consumed by followers.");
			out.emplace("ui.consumable.ingredients.label", "Ingredients");
			out.emplace("ui.consumable.ingredients.help", "Allows ingredients to be consumed by followers.");
			out.emplace("ui.consumable.discovery.label", "Player Alchemy Discovery");
			out.emplace("ui.consumable.discovery.help", "Lets the player learn alchemy effects when a follower consumes ingredients.\n\nWhen a follower consumes an ingredient, the player learns its first alchemy effect, just like eating it yourself. This way, your followers can help you discover new effects.");

			// Runtime feedback: Consumable
			out.emplace("feedback.consumable.potion", "{0} consumed {1}.");
			out.emplace("feedback.consumable.food", "{0} ate {1}.");
			out.emplace("feedback.consumable.drink", "{0} drank {1}.");
			out.emplace("feedback.consumable.ingredient", "{0} consumed {1}.");
			out.emplace("feedback.consumable.discovery_first_effect", "Discovered {0} from {1}.");

			// Equip Mode > Poisons
			out.emplace("ui.header.poisons", "Poisons");
			out.emplace("ui.poison.apply", "APPLY");
			out.emplace("ui.poison.consume", "CONSUME");
			out.emplace("ui.poison.disable", "DISABLE");
			out.emplace("ui.poison.help", "Controls what happens when you click a poison while holding the Mod Key during follower trade.\n\n- Apply: Applies the poison to the follower's equipped weapon\n- Consume: The follower consumes the poison\n- Disable: Transfers the poison normally");
			out.emplace("ui.poison.stacking.label", "Poison Stacking");
			out.emplace("ui.poison.stacking.help", "Stacks additional charges on an already-poisoned weapon when the same poison is applied again.\n\nBy default, a weapon that is already poisoned cannot be poisoned again. When this setting is enabled, each application of the same poison adds charges to the weapon. Perks like Concentrated Poison can affect the number of charges added per application.");
			out.emplace("ui.poison.max_charges.label", "Max Charges");
			out.emplace("ui.poison.max_charges.help", "Sets the maximum number of poison charges that can stack on a weapon.\n\nSpecifies how many charges can be applied. Once the cap is reached, further applications are blocked. Set to UNLIMITED to allow charges to stack without limit.");
			out.emplace("ui.poison.max_charges.unlimited", "UNLIMITED");
			
			// Runtime feedback: Poison
			out.emplace("feedback.poison.no_valid_weapon", "{0} does not have a valid {3}-Hand weapon.");
			out.emplace("feedback.poison.already_poisoned", "{2} is already poisoned.");
			out.emplace("feedback.poison.different_poison", "{2} is already poisoned with a different poison.");
			out.emplace("feedback.poison.max_charges", "{2} already has the maximum number of poison charges ({3}).");
			out.emplace("feedback.poison.applied", "Applied {1} to {2}.");
			out.emplace("feedback.poison.consumed", "{0} consumed {1}.");

			// Equip Mode > Spell Tomes
			out.emplace("ui.header.spell_tome_mode", "Spell Tomes");
			out.emplace("ui.spell_tome.enable.label", "Enable Learning");
			out.emplace("ui.spell_tome.enable.help", "Allows followers to learn spells from tomes while trading.\n\nWhile trading with a follower, hold the Mod Key and click a spell tome to make the follower learn its spell.");

			// Runtime feedback: Spell Tome
			out.emplace("feedback.spell_tome.learned", "{0} learned {1}.");
			out.emplace("feedback.spell_tome.already_known", "{0} already knows {1}.");

			// Combat Equip > Preference Match
			out.emplace("ui.header.preference_match", "Preference Match");
			out.emplace("ui.combat_equip.equip_priority.label", "Equip Priority");
			out.emplace("ui.combat_equip.equip_priority.help", "Gives saved combat preferences priority when the game decides what a follower should equip in combat.\n\nIn combat, the game periodically decides which weapon or shield a follower should use and may switch them to items you did not choose. When this setting is enabled, items saved as combat preferences are favored in that decision, so the follower is more likely to fight with the items you selected.\n\nWhen the game makes a weapon decision, it only considers a small number of items from the follower's inventory, so your preference may have no effect even if the item you picked is in the follower's inventory.\n\nThis setting can influence the game's weapon decisions but cannot control when they occur or which hand they affect. If the game never makes a decision for one hand during combat, the preference for that hand cannot apply, making dual-wield unreliable. For that, you must use the Melee Override setting under Equip Overrides.");
			out.emplace("ui.combat_equip.exact_item_match.label", "Exact Item Match");
			out.emplace("ui.combat_equip.exact_item_match.help", "Makes the follower use the exact saved copy of a preferred item, not just any copy of the same type.\n\nA follower may carry several copies of the same weapon or shield with different temper levels or enchantments, and the game may equip any of them regardless of which one you originally chose as the preference. With this setting enabled, the follower will use the specific copy you saved whenever they equip that item.\n\nThis also complements Equip Priority: Equip Priority only matches the base item type, so the game can still equip the wrong copy. This setting closes that gap by ensuring the follower uses the exact copy you saved.");
			out.emplace("ui.combat_equip.clear_on_unequip.label", "Clear Preferences on Unequip");
			out.emplace("ui.combat_equip.clear_on_unequip.help", "Clears the saved preference for an item when you unequip it from a follower during trade.\n\nWhile trading with a follower, unequipping an item does not automatically remove its saved preference, so the follower continues to have a combat preference for that slot. When this setting is enabled, unequipping an item also clears its preference, and the follower no longer has a combat preference for that slot.\n\nThis setting covers all slots except the left hand. To also clear left-hand preferences on unequip, enable Clear Left-Hand Preference on Unequip.");
			out.emplace("ui.combat_equip.clear_left_on_unequip.label", "Clear Left-Hand Preference on Unequip");
			out.emplace("ui.combat_equip.clear_left_on_unequip.help", "Clears the left-hand preference when you unequip a left-hand item from a follower during trade.\n\nWhile trading with a follower, unequipping a left-hand item does not automatically remove its saved preference, so the follower continues to have a combat preference for that slot. When this setting is enabled, removing a one-handed weapon, shield, staff, or scroll from the left hand also clears the preference for that slot.\n\nKeeping this separate from Clear Preferences on Unequip lets you control each hand independently. For example, you can clear left-hand preferences on unequip to leave the off-hand empty on purpose, while right-hand weapon preferences remain unaffected.");

			// Combat Equip > Equip Overrides
			out.emplace("ui.header.equip_overrides", "Equip Overrides");
			out.emplace("ui.combat_equip.melee_override.label", "Melee Override");
			out.emplace("ui.combat_equip.melee_override.help", "Forces followers to use the melee weapons and shields you selected for them.\n\nIn combat, the game's AI can swap a follower's melee weapon or shield at any time. This setting overrides those swaps and force-equips your selections instead, rather than just influencing the game's decision as Equip Priority does. Only melee weapons and shields are covered; force-equipping ranged weapons, staves, or spells would break the follower's combat AI and prevent them from attacking.\n\nThe exact hand combination you set is preserved: two-handed weapon, one-handed with shield, dual-wield, one-handed with empty off-hand, or even fully unarmed. If no preference is saved for a hand, or if the preferred item is no longer in the follower's inventory, the mod takes no action on that hand, allowing the game to switch to something you did not pick (to force it to unequip instead, enable Force Unarmed).");
			out.emplace("ui.combat_equip.force_unarmed.label", "Force Unarmed");
			out.emplace("ui.combat_equip.force_unarmed.help", "Force-unequips a hand that has no saved combat preference when the game equips it during combat.\n\nWhen Melee Override is active, the mod tries to equip the saved preference for each hand. If a hand has no preference set or its preferred item is no longer available, the mod normally takes no action on that hand. With this setting enabled, the mod force-unequips that hand instead when the game equips it. This is most useful when you want one hand equipped with a weapon while keeping the other empty, or with unarmed combat mods where you want one or both hands deliberately free to attack with fists.");
			out.emplace("ui.combat_equip.ammo_override.label", "Ammo Override");
			out.emplace("ui.combat_equip.ammo_override.help", "Forces followers to use the arrows or bolts you selected for them.\n\nIn combat, followers can switch to different ammo on their own, ignoring the type you chose. This setting overrides that and keeps them using your selection throughout the fight.\n\nIf the preferred ammo runs out, the follower falls back to whatever other ammo they have.");

			// Combat Equip > Auto-Equip & Restore
			out.emplace("ui.header.auto_equip_restore", "Auto-Equip & Restore");
			out.emplace("ui.combat_equip.pre_combat_restore.label", "Pre-Combat Equip Restore");
			out.emplace("ui.combat_equip.pre_combat_restore.help", "Restores the follower's pre-combat equipment when combat ends.\n\nAfter combat, followers remain wearing whichever equipment they last switched to during the fight. This setting restores the weapons, shield, and ammo they had before the fight, including clearing any hand that was empty before combat started, so you do not have to keep re-equipping the items you want them to use every time combat ends. The follower stays equipped with your choices outside of combat.");
			out.emplace("ui.combat_equip.headgear_auto_equip.label", "Headgear Auto-Equip");
			out.emplace("ui.combat_equip.headgear_auto_equip.help", "Automatically equips the headgear you selected for a follower when combat starts and restores their pre-combat state when combat ends.\n\nOutside of combat, the follower can go bareheaded or wear different headgear. When a fight begins, the mod equips the saved preference automatically. When combat ends, their headgear reverts to what they had on before, including bareheaded if that was their state. Helmets, hoods, and circlets are covered.\n\n- To set a headgear preference, open trade with the follower, select a headgear item, and press the Left-Hand Key while holding the Mod Key.\n- To clear or change the preference, press the same combination on the same item to clear it, or on a different item to replace it.");
			out.emplace("ui.combat_equip.infinite_ammo.label", "Infinite Ammo");
			out.emplace("ui.combat_equip.infinite_ammo.help", "Prevents followers from consuming arrows or bolts when firing ranged weapons.\n\nWhen a follower fires a ranged weapon, the game removes one piece of ammo from their inventory per shot. This setting prevents that removal, so followers never deplete their supply. They still need at least one piece of ammo in their inventory so the game knows which type to use.");

			// Equip Stability > Outfit Sync
			out.emplace("ui.header.outfit_sync", "Outfit Sync");
			out.emplace("ui.outfit_sync.block_outfit_reapplication.label", "Block Default Outfit Reapplication");
			out.emplace("ui.outfit_sync.block_outfit_reapplication.help", "Prevents the game from re-dressing the follower in their default outfit.\n\nWhen loading a save, fast traveling, or entering a new area, the game can replace a follower's equipped items with their original default outfit, discarding any custom equipment you gave them. This setting blocks that replacement, so the follower stays in whatever equipment you chose.");
			out.emplace("ui.outfit_sync.reequip_saved_items.label", "Re-Equip Saved Items");
			out.emplace("ui.outfit_sync.reequip_saved_items.help", "Re-equips the outfit items you chose for the follower after the game strips their equipment.\n\nWhen loading a save, fast traveling, or entering a new area, the game sometimes strips a follower's equipped items entirely. Because Block Default Outfit Reapplication prevents the game from restoring the default outfit in these moments, the follower can end up wearing nothing. This setting re-equips your saved outfit selection so the follower stays dressed as you intended.");
			out.emplace("ui.outfit_sync.allow_external_outfit_changes.label", "Allow External Outfit Changes");
			out.emplace("ui.outfit_sync.allow_external_outfit_changes.help", "Updates the saved outfit selection when another mod changes the follower's outfit.\n\nWhen Re-Equip Saved Items is enabled, this mod tracks what outfit you chose for the follower and re-equips it whenever the game strips their equipment. With this setting enabled, if a mod changes the follower's outfit, that change is accepted as the new saved selection and used for all future restores.\n\nWhen this setting is disabled, the mod always re-equips your manually saved outfit, ignoring changes made by other mods.");

			// Equip Stability > Hidden Items
			out.emplace("ui.header.hidden_items", "Hidden Items");
			out.emplace("ui.hidden_items.non_playable.label", "Non-Playable Items");
			out.emplace("ui.hidden_items.non_playable.help", "Removes non-playable armor, weapons, and ammo that are hidden from the trade menu when you open trade with a follower.\n\nSome items in a follower's inventory are flagged as non-playable. These items do not appear in the trade menu or inventory menu. A follower can carry, equip, and use these items, but from the trade menu there is no way to know they exist and no way to take, remove, or swap them. With this setting enabled, those hidden items are found and removed from the follower's inventory each time you open trade with a follower. This ensures the trade list accurately reflects everything the follower is carrying, and these hidden items can no longer cause unwanted equips or weapon swaps.\n\nSome mods add non-playable items for internal use, and removing them could break those mods. Items without a visible model, items generated by mods during gameplay, and quest items are never removed to reduce this risk.");
			out.emplace("ui.hidden_items.remove_hidden_armor.label", "Remove Hidden Armor");
			out.emplace("ui.hidden_items.remove_hidden_armor.help", "Removes non-playable armor pieces from the follower's inventory when you open trade with a follower.\n\nWhen disabled, hidden armor can remain in the follower's inventory but never appears in the trade list.\n\nSome mods use non-playable items for internal purposes, and these items are most commonly added as armor pieces, which is why this setting carries a higher mod compatibility risk than the weapon or ammo settings. If a mod stops working correctly for a follower after enabling this, a required item was likely removed. Removed items are not restored if this setting is later disabled. To recover removed items, load a save from before this setting was enabled.");
			out.emplace("ui.hidden_items.remove_hidden_weapons.label", "Remove Hidden Weapons");
			out.emplace("ui.hidden_items.remove_hidden_weapons.help", "Removes non-playable weapons and shields from the follower's inventory when you open trade with a follower.\n\nWhen disabled, hidden weapons and shields can remain in the follower's inventory but never appear in the trade list.\n\nRemoved items are not restored if this setting is later disabled. To recover removed items, load a save from before this setting was enabled.");
			out.emplace("ui.hidden_items.remove_hidden_ammo.label", "Remove Hidden Ammo");
			out.emplace("ui.hidden_items.remove_hidden_ammo.help", "Removes non-playable ammo from the follower's inventory when you open trade with a follower.\n\nWhen disabled, hidden ammo can remain in the follower's inventory but never appears in the trade list.\n\nRemoved items are not restored if this setting is later disabled. To recover removed items, load a save from before this setting was enabled.");
			out.emplace("ui.hidden_items.outfit_items.label", "Outfit Items");
			out.emplace("ui.hidden_items.outfit_items.help", "Makes outfit items that are hidden from the trade list visible when you trade with a follower.\n\nSome armor and clothing are assigned to a follower as their default outfit. These items are hidden from the trade list even while the follower is wearing them, so you have no way to see, take, or swap those pieces. Additionally, mods like SPID that distribute outfit items to followers use the same system the game uses to hide default outfit items, so those distributed items are also hidden from the trade list even though they are in the follower's inventory. When disabled, those items remain hidden from the trade list and you cannot see, take, or manage them.");
			out.emplace("ui.hidden_items.reveal_default_outfit_items.label", "Reveal Default Outfit Items");
			out.emplace("ui.hidden_items.reveal_default_outfit_items.help", "Makes armor and clothing from the follower's default outfit visible in the trade list when you trade with a follower.\n\nWhen you open trade with a follower, armor and clothing from their default outfit are hidden from the trade list even while the follower is wearing them. You have no way to see, take, or swap those worn pieces because they never appear in the list. This setting makes them visible so you can manage them like any other item. Copies of those outfit pieces that the follower is not wearing are also removed, since they are hidden duplicates that serve no purpose and occupy carry weight.");
			out.emplace("ui.hidden_items.reveal_external_outfit_items.label", "Reveal External Outfit Items");
			out.emplace("ui.hidden_items.reveal_external_outfit_items.help", "Makes outfit items distributed by mods to the follower visible in the trade list when you trade with a follower.\n\nWhen you open trade with a follower, outfit items distributed by mods like SPID are hidden from the trade list even though they are in the follower's inventory. These mods use the same system the game uses to hide default outfit items when distributing armor and clothing to followers, so those items are hidden from the trade list in the same way. This setting makes those items visible in the trade list when you open trade so you can see, take, or manage them.");

			// Equip Stability > Item Injection Blocking
			out.emplace("ui.header.item_injection_blocking", "Item Injection Blocking");
			out.emplace("ui.equip_suppression.block_outfit_injection.label", "Block Outfit-Item Injection");
			out.emplace("ui.equip_suppression.block_outfit_injection.help", "Prevents the game from adding armor pieces from a follower's default outfit to their inventory.\n\nWhen the game applies a new outfit to a follower, it also adds the armor pieces from that outfit directly to the follower's inventory. Block Default Outfit Reapplication under Outfit Sync already prevents this in most situations, but when that setting is disabled, or when another mod assigns the follower a different outfit, those armor additions can still occur. This setting blocks those additions and the follower's inventory contains only what you have given them.\n\nIf Allow External Outfit Changes is enabled, disable this setting so the armor from outfits assigned by other mods is added to the follower's inventory as intended.");
			out.emplace("ui.equip_suppression.block_leveled_injection.label", "Block Leveled-Item Injection");
			out.emplace("ui.equip_suppression.block_leveled_injection.help", "Prevents the game from adding items to a follower's inventory from their leveled lists.\n\nWhen a follower's inventory is first set up, the game selects weapons, ammo, potions, and other items from lists that scale with the player's level and adds them to the follower's inventory. These items appear without you ever placing them there. The same process can also be triggered when the follower's level changes. This setting blocks that process so followers only carry items you have given them.\n\nIf Allow External Outfit Changes is enabled, disable this setting. Some mods assign outfits by triggering a full inventory reset that also runs leveled item injection, and blocking it can prevent those outfit items from being added to the follower's inventory.");

			// Equip Stability > Loot Blocking
			out.emplace("ui.header.loot_blocking", "Loot Blocking");
			out.emplace("ui.equip_suppression.block_weapon_acquisition.label", "Block Weapon Acquisition");
			out.emplace("ui.equip_suppression.block_weapon_acquisition.help", "Stops followers from seeking out and grabbing weapons during combat.\n\nIn combat, the game can decide a follower needs a weapon and direct them to find one. The follower stops engaging enemies, walks to a nearby weapon on the ground, in a container, or on a corpse, and picks it up. This setting prevents that from happening, so the follower stays in the fight instead of walking away to get a weapon.\n\nWhen this setting is disabled, Block Looting and Block Pickup can still intercept the final pickup or transfer during combat, but only after the follower has already stopped fighting and walked to the item.");
			out.emplace("ui.equip_suppression.block_looting.label", "Block Looting");
			out.emplace("ui.equip_suppression.block_looting.help", "Stops followers from looting items from containers and corpses.\n\nThe game can transfer items from a nearby container or corpse directly into a follower's inventory without any visible action. This setting blocks those transfers. All item types are covered, and the block applies at all times, not just during combat. Only looting the follower does on their own is blocked. Looting you order them to do is not affected.\n\nItems lying loose on the ground are not covered by this setting. Use Block Pickup to stop followers from taking those.");
			out.emplace("ui.equip_suppression.block_pickup.label", "Block Pickup");
			out.emplace("ui.equip_suppression.block_pickup.help", "Stops followers from picking up loose items lying on the ground.\n\nThe game can make followers walk over to nearby items on the ground and pick them up. This happens while they are following you or idling in an area, and can also happen during combat when Block Weapon Acquisition is disabled. All item types are covered. Only pickups the follower does on their own are blocked. Pickups you order them to do are not affected.\n\nItems in containers and on corpses are not covered by this setting. Use Block Looting to stop followers from taking those.");

			// Equip Stability > Auto-Equip Blocking
			out.emplace("ui.header.auto_equip_blocking", "Auto-Equip Blocking");
			out.emplace("ui.equip_suppression.block_non_combat_auto_equip.label", "Block Non-Combat Auto-Equip");
			out.emplace("ui.equip_suppression.block_non_combat_auto_equip.help", "Prevents followers from automatically equipping weapons, shields, or ammo outside combat.\n\nWhile changing location or fast traveling, the game can swap a follower's weapon, shield, or ammo to something you did not choose. This setting blocks those automatic equips so the follower keeps the items you selected until the next fight.");
			out.emplace("ui.equip_suppression.block_best_weapon_auto_equip.label", "Block Best Weapon Auto-Equip");
			out.emplace("ui.equip_suppression.block_best_weapon_auto_equip.help", "Stops the game from automatically equipping what it considers the best weapon on the follower during trade.\n\nWhile trading with a follower, the game can automatically equip a weapon on the follower whenever any item is transferred, whether you are giving or taking items. This can override your loadout choices within the same trade session. This setting blocks that automatic weapon equip for the rest of the trade session, letting you rearrange items without interference.");

			// Equip Stability > Equip Gate
			out.emplace("ui.header.equip_gate", "Equip Gate");
			out.emplace("ui.equip_gate.block_equip.label", "Block Equip");
			out.emplace("ui.equip_gate.block_equip.help", "Blocks all item equips on followers that are not performed by this mod.\n\nThe mod's dedicated settings cover most known scenarios that cause unwanted equipment changes. However, there may be game behaviors or changes from other mods that still trigger unwanted equips. This setting acts as a broad safety net, blocking every automatic equip that does not come from this mod and happens outside of combat. Equips during combat are always allowed.\n\nMay conflict with other mods that manage follower equipment. Enable this only if you are not using such mods, or if you accept the risk of inconsistent follower equipment behavior.");
			out.emplace("ui.equip_gate.block_unequip.label", "Block Unequip");
			out.emplace("ui.equip_gate.block_unequip.help", "Blocks all item unequips on followers that are not performed by this mod.\n\nThe mod's dedicated settings cover most known scenarios that cause unwanted equipment changes. However, there may be game behaviors or changes from other mods that still trigger unwanted unequips. This setting acts as a broad safety net, blocking every automatic unequip that does not come from this mod and happens outside of combat. Unequips during combat are always allowed.\n\nMay conflict with other mods that manage follower equipment. Enable this only if you are not using such mods, or if you accept the risk of inconsistent follower equipment behavior.");
			out.emplace("ui.equip_gate.never_block_torch.label", "Never Block Torch");
			out.emplace("ui.equip_gate.never_block_torch.help", "Allows followers to equip and unequip torches when Block Equip or Block Unequip is enabled.\n\nWhen Block Equip or Block Unequip is enabled, all automatic equips and unequips outside combat are blocked, including torches. This setting creates an exception for torches so followers can still light and put them away as needed, while the rest of their loadout stays protected.");

			// Extra Features > Enchantment Recharge
			out.emplace("ui.header.enchantment_recharge", "Enchantment Recharge");
			out.emplace("ui.weapon_recharge.enable_recharge.label", "Enable Recharging");
			out.emplace("ui.weapon_recharge.enable_recharge.help", "Enables recharging enchanted weapons while trading with a follower.\n\nWhile trading with a follower, select an enchanted weapon and press the Recharge Key to restore its charge. Soul gems from both your inventory and the follower's inventory are available for recharging.");
			out.emplace("ui.weapon_recharge.recharge_key.label", "Recharge Key");
			out.emplace("ui.weapon_recharge.recharge_key.help", "Sets the key used to recharge enchanted weapons while trading with a follower.");
			out.emplace("ui.weapon_recharge.gamepad_recharge_key.label", "Gamepad Recharge Key");
			out.emplace("ui.weapon_recharge.gamepad_recharge_key.help", "Sets the gamepad button used to recharge enchanted weapons while trading with a follower.\n\nOn controller, the Gamepad Mod Key must be held while pressing the Gamepad Recharge Key.");
			out.emplace("ui.weapon_recharge.enable_indicator.label", "Recharge Key Indicator");
			out.emplace("ui.weapon_recharge.enable_indicator.help", "Adds a Recharge Key indicator in the SkyUI bottom bar when an enchanted weapon is selected while trading with a follower.");
			out.emplace("ui.weapon_recharge.advanced_display.label", "Advanced Soul Gem Display");
			out.emplace("ui.weapon_recharge.advanced_display.help", "Enables additional information for each soul gem in the picker when recharging enchanted weapons.\n\nWhen recharging a weapon, the soul gem picker lists available gems. With this setting enabled, each gem can show its contained soul, how many are in the inventory, and which inventory the gem comes from. The toggles below control each type of detail separately.");
			out.emplace("ui.weapon_recharge.show_stack_count.label", "Stack Count");
			out.emplace("ui.weapon_recharge.show_stack_count.help", "Shows how many of each soul gem are in the inventory next to each gem in the picker.");
			out.emplace("ui.weapon_recharge.contained_soul_mode.label", "Contained Soul");
			out.emplace("ui.weapon_recharge.contained_soul_mode.help", "Controls when the contained soul of each gem is shown in the soul gem picker.\n\nWhen recharging a weapon, the soul gem picker lists available gems. This setting controls whether the contained soul each gem holds is displayed alongside the gem.\n\n- DISABLE: Never show the contained soul.\n- LOWER ONLY: Show only when the contained soul is lower than the gem's capacity.\n- ALWAYS: Always show the contained soul.");
			out.emplace("ui.weapon_recharge.contained_soul_mode.disable", "DISABLE");
			out.emplace("ui.weapon_recharge.contained_soul_mode.lower_only", "LOWER ONLY");
			out.emplace("ui.weapon_recharge.contained_soul_mode.always", "ALWAYS");
			out.emplace("ui.weapon_recharge.source_tag.label", "Source Tag");
			out.emplace("ui.weapon_recharge.source_tag.help", "Controls whether soul gems in the picker are labeled by which inventory they come from.\n\nWhen recharging a weapon, the soul gem picker can show gems from both your inventory and the follower's. This setting controls whether each gem is labeled with its source.\n\n- DISABLE: Do not show source labels.\n- FOLLOWER: Label only gems from the follower's inventory.\n- PLAYER: Label only gems from your inventory.\n- BOTH: Label gems from both inventories.");
			out.emplace("ui.weapon_recharge.source_tag.disable", "DISABLE");
			out.emplace("ui.weapon_recharge.source_tag.follower", "FOLLOWER");
			out.emplace("ui.weapon_recharge.source_tag.player", "PLAYER");
			out.emplace("ui.weapon_recharge.source_tag.both", "BOTH");
			out.emplace("ui.weapon_recharge.soul_gem_sorting.label", "Soul Gem Sorting");
			out.emplace("ui.weapon_recharge.soul_gem_sorting.help", "Controls how soul gems are ordered in the picker when recharging a weapon.\n\n- BY NAME: Sorts soul gems alphabetically by name.\n- SOUL ASC: Sorts by contained soul level, lowest first.\n- SOUL DESC: Sorts by contained soul level, highest first.");
			out.emplace("ui.weapon_recharge.soul_gem_sorting.name", "BY NAME");
			out.emplace("ui.weapon_recharge.soul_gem_sorting.soul_asc", "SOUL ASC");
			out.emplace("ui.weapon_recharge.soul_gem_sorting.soul_desc", "SOUL DESC");

			// Runtime feedback: Weapon recharge
			out.emplace("feedback.weapon_recharge.no_items_to_charge_enchantment", "No items to charge enchantment");

			// Extra Features > Follower Stats
			out.emplace("ui.header.follower_stats", "Follower Stats");
			out.emplace("ui.stats_display.always", "ALWAYS");
			out.emplace("ui.stats_display.follower_side", "FOLLOWER SIDE");
			out.emplace("ui.stats_display.disable", "DISABLE");
			out.emplace("ui.stats_display.help", "Controls where follower stats are shown while trading with a follower.\n\nWhile trading, SkyUI shows the player's stats in the bottom panel. This setting replaces that panel with the follower's carry weight, gold, and health/magicka/stamina bars when trading with a follower.\n\n- ALWAYS: Displays follower stats on both the Take and Give tabs.\n- FOLLOWER SIDE: Displays follower stats only on the Take tab.\n- DISABLE: Displays only the player's stats as usual.");

			// Extra Features > Corpse Equip Mode
			out.emplace("ui.header.corpse_equip", "Corpse Equip Mode");
			out.emplace("ui.corpse_equip.enable.label", "Enable Corpse Equip");
			out.emplace("ui.corpse_equip.enable.help", "Allows equipping and unequipping armor, weapons, and ammo on corpses while looting.\n\nWhile looting a corpse, hold the Mod Key and click an armor, weapon, or ammo item to equip or unequip it on the corpse.");
			out.emplace("ui.corpse_equip.indicator_text.label", "Indicator Text");
			out.emplace("ui.corpse_equip.indicator_text.default", "Equip Corpse");
			out.emplace("ui.corpse_equip.indicator_text.help", "Sets the text shown on the bottom bar indicator when looting a corpse.");

			// Fixes > Engine Fixes
			out.emplace("ui.header.engine_fixes", "Engine Fixes");
			out.emplace("ui.stale_weight_cache.enable.label", "Stuck Weight Fix");
			out.emplace("ui.stale_weight_cache.enable.help", "Fixes a bug where all weighted items on the Give tab appear grayed out and cannot be transferred to a follower.\n\nWhile trading with a follower, if an item from the follower's default equipment was removed without a replacement, the follower's carry weight can become stuck at an impossibly high value. The game then treats them as unable to carry any more, graying out all weighted items on the Give tab. This setting corrects the stuck carry weight when you open trade with that follower, restoring normal transfer behavior.");
			
			// Fixes > SkyUI Fixes
			out.emplace("ui.header.skyui_fixes", "SkyUI Fixes");
			out.emplace("ui.quantity_menu_blocker.enable.label", "Skip Quantity Prompt");
			out.emplace("ui.quantity_menu_blocker.enable.help", "Hides the quantity prompt when using the Mod Key or SkyUI equip mode while trading with a follower.\n\nWhile trading with a follower, clicking a stacked item with the Mod Key or in SkyUI equip mode normally opens a quantity prompt. These actions always apply to a single item, so the prompt serves no purpose. This setting skips it so the action happens immediately.");
			out.emplace("ui.zero_weight.enable.label", "Weightless Item Prompt");
			out.emplace("ui.zero_weight.enable.help", "Forces the quantity prompt to appear when taking weightless items from a follower on the Take tab.\n\nWhile trading, SkyUI transfers the entire stack of weightless items (such as gold or lockpicks) on the Take tab without showing a quantity prompt. This setting adds the prompt while trading with a follower so you can choose the exact amount to take instead of receiving the entire stack.\n\nTo transfer a stack (e.g., arrows) and equip it on the follower in one step, start the transfer without the Mod Key, then hold the Mod Key while confirming the quantity prompt.");
		}

		[[nodiscard]] std::optional<std::unordered_map<std::string, std::string>> ParseKeyValueFile(const std::filesystem::path& path)
		{
			std::ifstream in(path);
			if (!in.is_open()) {
				return std::nullopt;
			}

			std::unordered_map<std::string, std::string> out;
			std::string line;
			std::string currentSection;

			while (std::getline(in, line)) {
				line = Trim(std::move(line));
				if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
					continue;
				}

				if (line.front() == '[' && line.back() == ']') {
					currentSection = ToLower(Trim(line.substr(1, line.size() - 2)));
					continue;
				}

				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}

				auto key = ToLower(Trim(line.substr(0, eq)));
				auto val = UnescapeValue(Trim(line.substr(eq + 1)));

				if (!currentSection.empty()) {
					key = currentSection + "." + key;
				}

				if (key.empty()) {
					continue;
				}

				out[std::move(key)] = std::move(val);
			}

			return out;
		}

		void ApplyOverrides(Table& base, const std::unordered_map<std::string, std::string>& overrides)
		{
			for (const auto& [k, v] : overrides) {
				if (!v.empty()) {
					base[k] = v;
				}
			}
		}

		void LoadImpl()
		{
			Table next;
			LoadDefaultsEnglish(next);

			auto tryLoad = [&next](const std::filesystem::path& p) {
				auto parsed = ParseKeyValueFile(p);
				if (!parsed) {
					return false;
				}
				ApplyOverrides(next, *parsed);
				logger::info("Localization: loaded '{}'", p.string());
				return true;
			};

			const auto sel = SelectLanguageFile();
			bool loadedFile = false;
			if (sel.primary) {
				loadedFile = tryLoad(*sel.primary);
				if (!loadedFile && sel.fallbackEn && *sel.fallbackEn != *sel.primary) {
					loadedFile = tryLoad(*sel.fallbackEn);
				}
			}

			if (!loadedFile) {
				logger::info("Localization: using built-in English");
			}

			g_table = std::move(next);
			g_loaded = true;
		}
	}

	void Load()
	{
		if (g_loaded) {
			return;
		}
		LoadImpl();
	}

	void Reload()
	{
		LoadImpl();
	}

	const std::string& Get(std::string_view key)
	{
		if (!g_loaded) {
			LoadImpl();
		}

		const std::string k = ToLower(std::string(key));
		const auto it = g_table.find(k);
		if (it != g_table.end()) {
			return it->second;
		}

		// Avoid allocating a new string each time for missing keys.
		static std::string missing;
		missing.assign(key.data(), key.size());
		return missing;
	}

	const char* CStr(std::string_view key)
	{
		return Get(key).c_str();
	}
}
