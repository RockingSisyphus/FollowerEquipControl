// Shared icon X-position helper for ContainerMenu icon injectors.
//
// Scans the InventoryListEntry clip for visible icon-like display objects
// (vanilla SkyUI icons, other FEC injectors, and third-party icons such as DIII)
// and returns the X coordinate for the next icon.
//
// Any mod that creates visible display-object clips to the right of the item-name
// field can be detected without hard-coded clip names.

#pragma once

#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <initializer_list>

#include "core/settings/PluginSettings.h"

namespace FEC::IconPositioning {

	// Default icon appearance values from the member initializers.
	inline const PluginSettings::IconAppearance& DefaultIconAppearance()
	{
		static const PluginSettings::IconAppearance kDefaults{};
		return kDefaults;
	}

	// Gap after the name text field when no other icons are present.
	inline double GapAfterText()
	{
		const auto& a = PluginSettings::Get().iconAppearance;
		return a.enableCustomization ? a.gapAfterText : DefaultIconAppearance().gapAfterText;
	}

	// Gap after the rightmost non-FEC icon. The stored value is offset by 6.0 so
	// the default remains 2 px while allowing a wider slider range.
	inline double GapAfterIcon()
	{
		const auto& a = PluginSettings::Get().iconAppearance;
		const double stored = a.enableCustomization ? a.gapAfterIcon : DefaultIconAppearance().gapAfterIcon;
		return stored - 6.0;
	}

	// Gap between adjacent FEC icons.
	inline double FecIconSpacing()
	{
		const auto& a = PluginSettings::Get().iconAppearance;
		return a.enableCustomization ? a.fecIconSpacing : DefaultIconAppearance().fecIconSpacing;
	}

	// Standard icon size used by all FEC icon injectors.
	inline double IconSize()
	{
		const auto& a = PluginSettings::Get().iconAppearance;
		return a.enableCustomization ? a.iconSize : DefaultIconAppearance().iconSize;
	}

	// Hash the effective appearance settings so any change invalidates the item
	// list immediately.
	[[nodiscard]] inline std::size_t AppearanceHash()
	{
		const auto& a = PluginSettings::Get().iconAppearance;
		const auto& def = DefaultIconAppearance();
		const double effectiveIconSize = a.enableCustomization ? a.iconSize : def.iconSize;
		const double effectiveGapAfterText = a.enableCustomization ? a.gapAfterText : def.gapAfterText;
		const double effectiveGapAfterIcon = a.enableCustomization ? a.gapAfterIcon : def.gapAfterIcon;
		const double effectiveFecIconSpacing = a.enableCustomization ? a.fecIconSpacing : def.fecIconSpacing;
		std::size_t h = std::hash<double>{}(effectiveIconSize);
		h ^= std::hash<double>{}(effectiveGapAfterText) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<double>{}(effectiveGapAfterIcon) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<double>{}(effectiveFecIconSpacing) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<bool>{}(a.enableIconIndicator) + 0x9e3779b9 + (h << 6) + (h >> 2);
		h ^= std::hash<bool>{}(a.enableCustomization) + 0x9e3779b9 + (h << 6) + (h >> 2);
		return h;
	}

	// Slot width used by vanilla SkyUI icons.
	inline constexpr double kIconSpace = 16.0;

	// Third-party icons are measured by tight bounds, so add back a vanilla-style
	// trailing gap.
	inline constexpr double kThirdPartyIconTrailingFactor = 0.25;

	// Vanilla SkyUI icon instance names. Visibility is checked via _currentframe,
	// not _visible.
	inline constexpr std::array kVanillaIcons{
		"bestIcon", "favoriteIcon", "poisonIcon",
		"stolenIcon", "enchIcon", "readIcon"
	};

	// FEC wrapper clip instance names. They are skipped because spacing comes from
	// predecessor order, not position.
	inline constexpr std::array kFecWrapperNames{
		"fecCombatIconWrap",
		"fecEquipIconWrap",
		"fecOutfitSyncIconWrap"
	};

	// SkyUI InventoryListEntry children that are not injected icons.
	inline constexpr std::array kSkyUIEntryChildren{
		// Frame-based icons (scanned separately with _currentframe logic).
		"bestIcon", "favoriteIcon", "poisonIcon",
		"stolenIcon", "enchIcon", "readIcon",
		// Non-icon entry elements.
		"background", "selectIndicator", "itemIcon", "equipIcon"
	};

	// Determines the X coordinate for the next icon on an InventoryListEntry.
	//
	// 1. Scan vanilla SkyUI icons by name and frame visibility.
	// 2. Scan other visible display objects to the right of the text field.
	// 3. Fall back to the text field edge if no icons are present.
	// 4. Add any visible FEC predecessor offsets.
	//
	// entryClip: the InventoryListEntry MovieClip.
	// entryField: the item-name TextField.
	// predecessors: visible FEC wrapper names that ran earlier in the chain.
	[[nodiscard]] inline double FindIconInsertX(
		RE::GFxValue& entryClip,
		const RE::GFxValue& entryField,
		std::initializer_list<const char*> predecessors = {})
	{
		double maxRight = 0.0;
		bool anyFound = false;

		// Text field right edge and fallback anchor.
		double textRight = 0.0;
		{
			RE::GFxValue tx, tw;
			entryField.GetMember("_x", &tx);
			entryField.GetMember("_width", &tw);
			if (tx.IsNumber() && tw.IsNumber()) {
				textRight = tx.GetNumber() + tw.GetNumber();
			}
		}

		// 1. Vanilla SkyUI icons.
		for (const auto* name : kVanillaIcons) {
			RE::GFxValue icon;
			entryClip.GetMember(name, &icon);
			if (!icon.IsDisplayObject()) {
				continue;
			}
			RE::GFxValue frame;
			icon.GetMember("_currentframe", &frame);
			if (!frame.IsNumber() || frame.GetNumber() < 2.0) {
				continue;
			}
			RE::GFxValue x;
			icon.GetMember("_x", &x);
			if (x.IsNumber()) {
				const double right = x.GetNumber() + kIconSpace;
				if (right > maxRight) {
					maxRight = right;
				}
				anyFound = true;
			}
		}

		// 2. Scan other visible display objects, including icons injected by other mods.
		entryClip.VisitMembers([&](const char* name, const RE::GFxValue& val) {
			if (!val.IsDisplayObject()) {
				return;
			}

			// Skip built-in SkyUI children to avoid false positives.
			for (const auto* s : kSkyUIEntryChildren) {
				if (std::strcmp(name, s) == 0) {
					return;
				}
			}

			// Skip SkyUI stat-column TextFields (textField0 through textField9).
			if (std::strncmp(name, "textField", 9) == 0) {
				return;
			}

			// Skip FEC wrapper clips; their spacing comes from predecessor order, not position.
			bool isFecWrapper = false;
			for (const auto* w : kFecWrapperNames) {
				if (std::strcmp(name, w) == 0) {
					isFecWrapper = true;
					break;
				}
			}
			if (isFecWrapper) {
				return;
			}

			RE::GFxValue vis;
			val.GetMember("_visible", &vis);
			if (!vis.IsBool() || !vis.GetBool()) {
				return;
			}

			RE::GFxValue x, w, h;
			val.GetMember("_x", &x);
			val.GetMember("_width", &w);
			val.GetMember("_height", &h);
			if (!x.IsNumber() || !w.IsNumber()) {
				return;
			}
			if (x.GetNumber() < textRight || w.GetNumber() <= 0.0) {
				return;
			}

			// Third-party icons use tight bounds, so add a vanilla-style trailing gap.
			// Use height when available; otherwise fall back to width.
			const double iconExtent =
				(h.IsNumber() && h.GetNumber() > 0.0) ? h.GetNumber() : w.GetNumber();
			const double right =
				x.GetNumber() + w.GetNumber() + iconExtent * kThirdPartyIconTrailingFactor;
			if (right > maxRight) {
				maxRight = right;
				anyFound = true;
			}
		});

		// 3. Fallback: place after the text field.
		double baseX;
		if (!anyFound) {
			baseX = std::floor(textRight + GapAfterText());
		} else {
			baseX = std::floor(maxRight + GapAfterIcon());
		}

		// 4. Add one slot per visible FEC predecessor in the chain.
		//
		// The chain runs inner-first, so predecessor visibility is already settled
		// for this frame.
		int visiblePredecessors = 0;
		for (const auto* pred : predecessors) {
			RE::GFxValue wrap;
			entryClip.GetMember(pred, &wrap);
			if (!wrap.IsDisplayObject()) {
				continue;
			}
			RE::GFxValue vis;
			wrap.GetMember("_visible", &vis);
			if (vis.IsBool() && vis.GetBool()) {
				++visiblePredecessors;
			}
		}

		return baseX + visiblePredecessors * (IconSize() + FecIconSpacing());
	}

	inline void RepositionVisibleFecIcon(
		RE::GFxValue& a_entryClip,
		const RE::GFxValue& a_entryField,
		const char* a_wrapperName,
		std::initializer_list<const char*> a_predecessors = {})
	{
		RE::GFxValue wrapper;
		a_entryClip.GetMember(a_wrapperName, std::addressof(wrapper));
		if (!wrapper.IsDisplayObject()) {
			return;
		}

		RE::GFxValue visible;
		wrapper.GetMember("_visible", std::addressof(visible));
		if (!visible.IsBool() || !visible.GetBool()) {
			return;
		}

		wrapper.SetMember("_x", RE::GFxValue(FindIconInsertX(
			a_entryClip,
			a_entryField,
			a_predecessors)));
	}

	inline void RepositionFecIcons(RE::GFxValue& a_entryClip, const RE::GFxValue& a_entryField)
	{
		RepositionVisibleFecIcon(a_entryClip, a_entryField, "fecCombatIconWrap");
		RepositionVisibleFecIcon(a_entryClip, a_entryField, "fecEquipIconWrap", { "fecCombatIconWrap" });
		RepositionVisibleFecIcon(a_entryClip, a_entryField, "fecOutfitSyncIconWrap", { "fecCombatIconWrap", "fecEquipIconWrap" });
	}

}  // namespace FEC::IconPositioning
