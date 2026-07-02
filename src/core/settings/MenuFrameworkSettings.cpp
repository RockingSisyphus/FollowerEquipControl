#include "MenuFrameworkSettings.h"

#include "PCH.h"

#include "FeatureRegistry.h"
#include "Localization.h"

#include "PluginSettings.h"

#include "Logging.h"

#include "Controls.h"

#include "FollowerEquipModeUIIndicator.h"
#include "ConsumableMode.h"
#include "PoisonMode.h"
#include "SpellTomeMode.h"
#include "WeaponEnchantmentRechargeUIIndicator.h"
#include "EquipGate.h"
#include "ZeroWeightTakeAllFix.h"
#include "StaleWeightCacheFix.h"
#include "QuantityMenuBlocker.h"
#include "CombatEquipPreference.h"
#include "KnownFollowerState.h"
#include "ActorScope.h"
#include "ActorInclusion.h"
#include "SpellSuppressionState.h"
#include "ActorScopeExcludeState.h"
#include "ActorStateCleanup.h"

#include "RE/E/Effect.h"
#include "RE/E/EffectSetting.h"
#include "RE/E/EnchantmentItem.h"
#include "RE/T/TESEnchantableForm.h"
#include "RE/A/ActorValueList.h"
#include "RE/A/ActorValueInfo.h"

#include "third_party/SKSEMenuFramework.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace FEC
{
	namespace
	{
		const PluginSettings::Settings& Defaults()
		{
			static const PluginSettings::Settings kDefaults{};
			return kDefaults;
		}

		struct UIState
		{
			bool initialized{ false };
			bool dirty{ false };

			PluginSettings::Settings draft{};

			int logLevelIndex{ 2 };  // 0..6 (trace..off)
			int modKeyDik{ 0 };
			int skyuiEquipModeKey{ 0 };
			int rechargeKeyDik{ 20 };
			int gamepadModKey{ 280 };
			int gamepadRightHandKey{ 276 };
			int gamepadLeftHandKey{ 278 };
			int gamepadRechargeKey{ 279 };
			int quickTradeKeyDik{ 0 };
			int quickTradeGamepadKey{ 0 };

			std::array<char, 96> indicatorText{};
			std::array<char, 96> summonIndicatorText{};
			std::array<char, 96> inclusionIndicatorText{};
			std::array<char, 96> equipModeTextOverride{};
			std::array<char, 96> corpseIndicatorText{};
		};

		UIState g_ui;
		bool g_registered{ false };

		[[nodiscard]] std::string TrimCopy(std::string s);

		constexpr const char* kLogLevels[] = { "trace", "debug", "info", "warn", "error", "critical", "off" };

		[[nodiscard]] int LogLevelIndexFromString(std::string s)
		{
			s = TrimCopy(std::move(s));
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			if (s == "trace") return 0;
			if (s == "debug") return 1;
			if (s == "info") return 2;
			if (s == "warn" || s == "warning") return 3;
			if (s == "error" || s == "err") return 4;
			if (s == "critical") return 5;
			if (s == "off") return 6;
			return 2;
		}

		[[nodiscard]] const char* LogLevelStringFromIndex(int idx)
		{
			if (idx < 0) idx = 0;
			if (idx > 6) idx = 6;
			return kLogLevels[idx];
		}

		[[nodiscard]] std::string Label(std::string_view key, std::string_view idSuffix)
		{
			std::string out = Localization::Get(key);
			out += idSuffix;
			return out;
		}

		//=============================================================================
		// [SECTION 1] Style Layer - style structs + constexpr instances
		//   Centralized style knobs - edit these to tune the entire menu UI.
		//=============================================================================

		// --- Global spacing ---------------------------------------------------
		struct SpacingStyle
		{
			float settingsItemGapPx      = 8.0f;   // Vertical gap between setting items.
			float toolbarGapPx           = 16.0f;  // Vertical gap between title and toolbar.
			float helpDefaultExtraGapPx  = 4.0f;   // Extra gap between help icon and default button.
		};

		constexpr SpacingStyle kSpacingStyle{};

		// --- Title line -------------------------------------------------------
		struct TitleLineStyle
		{
			float iconScale              = 1.25f;  // Title icon scale relative to FontAwesome font size.
		};

		constexpr TitleLineStyle kTitleLineStyle{};

		// --- Button themes (emphasis / ghost / neutral) -----------------------
		struct ButtonThemeColors
		{
			// Emphasis (selected / primary).
			ImGuiMCP::ImVec4 emphasisButton         { 0.80f, 0.80f, 0.80f, 1.0f };
			ImGuiMCP::ImVec4 emphasisHovered        { 1.00f, 1.00f, 1.00f, 1.0f };
			ImGuiMCP::ImVec4 emphasisActive         { 0.80f, 0.80f, 0.80f, 1.0f };
			ImGuiMCP::ImVec4 emphasisText           { 0.00f, 0.00f, 0.00f, 1.0f };

			// Ghost (transparent base).
			ImGuiMCP::ImVec4 ghostButton            { 0.00f, 0.00f, 0.00f, 0.00f };
			ImGuiMCP::ImVec4 ghostHovered           { 0.25f, 0.25f, 0.25f, 0.65f };
			ImGuiMCP::ImVec4 ghostActive            { 0.15f, 0.15f, 0.15f, 0.85f };
			ImGuiMCP::ImVec4 ghostText              { 1.00f, 1.00f, 1.00f, 0.40f };

			// Neutral (dark gray).
			ImGuiMCP::ImVec4 neutralButton          { 0.20f, 0.20f, 0.20f, 1.0f };
			ImGuiMCP::ImVec4 neutralHovered         { 0.28f, 0.28f, 0.28f, 1.0f };
			ImGuiMCP::ImVec4 neutralActive          { 0.14f, 0.14f, 0.14f, 1.0f };
			ImGuiMCP::ImVec4 neutralText            { 1.00f, 1.00f, 1.00f, 1.0f };
		};

		const ButtonThemeColors kButtonTheme{};

		// --- Control widths (narrow combos, segmented rows, sliders) ----------
		struct ControlWidthStyle
		{
			float narrowFactor           = 0.50f;
			float narrowMinPx            = 180.0f;
			float narrowMaxPx            = 360.0f;

			float segmentedFactor        = 0.75f;
			float segmentedMinPx         = 270.0f;
			float segmentedMaxPx         = 540.0f;

			float sliderFactor           = 0.50f;
			float sliderMinPx            = 180.0f;
			float sliderMaxPx            = 360.0f;
		};

		constexpr ControlWidthStyle kControlWidthStyle{};

		// --- DIK key combo popup ---------------------------------------------
		struct DikKeyComboStyle
		{
			int   visibleRows            = 14;      // Max visible rows in the popup.
			float separatorVertInsetPx   = 2.0f;    // Inset from top/bottom of each row for the vertical line.
			float separatorThickness     = 1.0f;    // Thickness of the column separator line.
		};

		constexpr DikKeyComboStyle kDikKeyComboStyle{};

		// --- Collapsing header with leading icon -----------------------------
		struct CollapsingHeaderIconStyle
		{
			float iconShiftMultiplier    = 4.5f;    // x = itemInnerSpacing.x * this value.
			float iconTextGapMultiplier  = -2.5f;   // x = itemInnerSpacing.x * this value.
		};

		constexpr CollapsingHeaderIconStyle kCollapsingHeaderIconStyle{};

		// --- Button with leading icon ----------------------------------------
		struct LeadingIconButtonStyle
		{
			float iconLabelGapPx         = 12.0f;   // Extra gap between icon and label text.
		};

		constexpr LeadingIconButtonStyle kLeadingIconButtonStyle{};

		// --- NavButton (full-width navigation) -------------------------------
		struct NavButtonStyle
		{
			float heightPx               = 48.0f;    // 0 = auto (text height + 2*framePadY).
			float marginTopPx            = 4.0f;
			float marginBottomPx         = 4.0f;
			float marginLeftPx           = 4.0f;
			float marginRightPx          = 4.0f;

			float accentWidthPx          = 8.0f;
			float rounding               = 0.0f;
			float borderSize             = 4.0f;

			// Button background (Neutral style).
			ImGuiMCP::ImVec4 bgColor             { 0.00f, 0.00f, 0.00f, 0.0f };
			ImGuiMCP::ImVec4 bgColorHovered      { 0.20f, 0.20f, 0.20f, 1.0f };
			ImGuiMCP::ImVec4 bgColorActive       { 0.14f, 0.14f, 0.14f, 1.0f };
			ImGuiMCP::ImVec4 borderColor         { 0.28f, 0.28f, 0.28f, 1.0f };
			ImGuiMCP::ImVec4 borderColorHovered  { 0.28f, 0.28f, 0.28f, 1.0f };

			// Text & accent drawn via DrawList.
			ImGuiMCP::ImVec4 textColor           { 0.80f, 0.80f, 0.80f, 1.0f };
			ImGuiMCP::ImVec4 textColorHovered    { 1.00f, 1.00f, 1.00f, 1.0f };
			ImGuiMCP::ImVec4 accentColor         { 0.28f, 0.28f, 0.28f, 1.0f };
			ImGuiMCP::ImVec4 accentColorHovered  { 0.28f, 0.28f, 0.28f, 1.0f };
		};

		const NavButtonStyle kNavStyle{};

		// --- Tooltip ---------------------------------------------------------
		struct TooltipStyle
		{
			float wrapWidthMultiplier    = 35.0f;   // Wrap at fontSize * this value.
		};

		constexpr TooltipStyle kTooltipStyle{};

		// --- Connector bracket (dependency lines) ----------------------------
		struct ConnectorBracketStyle
		{
			float thickness              = 1.0f;
			float yTopPadPx              = 2.0f;
			float minXOffsetPx           = 6.0f;    // Minimum horizontal offset from toggle left edge.
			float minHeightPx            = 2.0f;    // Vertical lines shorter than this are skipped.
		};

		constexpr ConnectorBracketStyle kConnectorStyle{};

		// --- Toolbar (RELOAD / SAVE / RESET ALL) ----------------------------
		struct ToolbarStyle
		{
			// Frame shape.
			float rounding               = 1.0f;    // Corner rounding for toolbar buttons.
			float borderSize             = 1.0f;    // Border thickness for toolbar buttons.
			float padX                   = 8.0f;    // Horizontal frame padding.
			float padY                   = 8.0f;    // Vertical frame padding (controls button height).

			// Status indicator.
			ImGuiMCP::ImVec4 disabledModColor { 0.60f, 0.25f, 0.25f, 0.60f };
		};

		const ToolbarStyle kToolbarStyle{};

		// --- Segmented button row --------------------------------------------
		struct SegmentedStyle
		{
			float minWidthPx             = 60.0f;
			float maxWidthPx             = 180.0f;
			float gapPx                  = 4.0f;
		};

		constexpr SegmentedStyle kSegmentedDefaultStyle{};

		// --- Preference Capture table ----------------------------------------
		struct PreferenceCaptureTableStyle
		{
			float rowExtraCellPaddingY   = 16.0f;
			float rowExtraMinHeightPx    = 0.0f;
			float setInnerPadX           = 16.0f;
			float setInnerPadY           = 0.0f;
			float setFrameRounding       = 1.0f;
			float setBorderThickness     = 1.0f;
			float categoryColWidthPx     = 320.0f;
		};

		constexpr PreferenceCaptureTableStyle kPrefCaptureTableStyle{};

		// --- Actor tab strip (Preference Capture) ----------------------------

		// Per-category button + strip colour theme.
		struct ActorTabColors
		{
			// Active (selected) tab state.
			ImGuiMCP::ImVec4 activeButton    {};
			ImGuiMCP::ImVec4 activeHovered   {};
			ImGuiMCP::ImVec4 activePressed   {};
			ImGuiMCP::ImVec4 activeBorder    {};
			ImGuiMCP::ImVec4 activeText      {};
			// Inactive (unselected) tab state.
			ImGuiMCP::ImVec4 inactiveButton  {};
			ImGuiMCP::ImVec4 inactiveHovered {};
			ImGuiMCP::ImVec4 inactivePressed {};
			ImGuiMCP::ImVec4 inactiveBorder  {};
			ImGuiMCP::ImVec4 inactiveText    {};
			// Bottom category strip.
			ImGuiMCP::ImVec4 activeStrip     {};
			ImGuiMCP::ImVec4 inactiveStrip   {};
		};

		struct ActorTabStyle
		{
			// Frame shape - match ToolbarStyle for consistent button height.
			float rounding               = 4.0f;    // Corner rounding.
			float borderSize             = 1.0f;    // Border thickness.
			float padX                   = 24.0f;   // Horizontal frame padding.
			float padY                   = 12.0f;   // Vertical frame padding (affects auto height when tabHeightPx == 0).

			// Tab sizing.
			float tabHeightPx            = 0.0f;    // Explicit button height. 0 = derive from padY via GetFrameHeight().
			float preferredMaxWidthPx    = 160.0f;  // Preferred max width per tab; long names may exceed.
			float minWidthPx             = 0.0f;    // Minimum width per tab (0 = content-sized).

			// Spacing.
			float itemSpacingOverridePx  = -1.0f;   // Horizontal gap between tabs (-1 = use ambient).
			float bottomSeparatorGapPx   = -1.0f;   // Gap between tab row and the separator line (-1 = use ambient item spacing).

			// Category strip at the bottom edge of each tab.
			float stripHeightPx          = 3.0f;    // Strip height for inactive tabs; active tabs use 2x.

			// Help icon area.
			float helpPadLeftPx          = 16.0f;   // Padding left of the help icon.
			float helpPadRightPx         = 16.0f;   // Padding right of the help icon.

			// Per-category colour themes.

			// Follower - silver / white theme.
			ActorTabColors follower {
				.activeButton    = { 0.28f, 0.28f, 0.30f, 0.50f },
				.activeHovered   = { 0.38f, 0.38f, 0.40f, 0.50f },
				.activePressed   = { 0.20f, 0.20f, 0.22f, 0.50f },
				.activeBorder    = { 0.88f, 0.88f, 0.92f, 0.90f },
				.activeText      = { 1.00f, 1.00f, 1.00f, 1.00f },
				.inactiveButton  = { 0.16f, 0.16f, 0.16f, 0.20f },
				.inactiveHovered = { 0.24f, 0.24f, 0.24f, 0.20f },
				.inactivePressed = { 0.11f, 0.11f, 0.11f, 0.20f },
				.inactiveBorder  = { 0.42f, 0.42f, 0.42f, 0.55f },
				.inactiveText    = { 0.65f, 0.65f, 0.65f, 1.00f },
				.activeStrip     = { 1.00f, 1.00f, 1.00f, 0.90f },
				.inactiveStrip   = { 0.80f, 0.80f, 0.80f, 0.25f },
			};

			// Summon - pale blue theme.
			ActorTabColors summon {
				.activeButton    = { 0.28f, 0.28f, 0.30f, 0.50f },
				.activeHovered   = { 0.38f, 0.38f, 0.40f, 0.50f },
				.activePressed   = { 0.20f, 0.20f, 0.22f, 0.50f },
				.activeBorder    = { 0.88f, 0.88f, 0.92f, 0.90f },
				.activeText      = { 0.85f, 0.95f, 1.00f, 1.00f },
				.inactiveButton  = { 0.16f, 0.16f, 0.16f, 0.20f },
				.inactiveHovered = { 0.24f, 0.24f, 0.24f, 0.20f },
				.inactivePressed = { 0.11f, 0.11f, 0.11f, 0.20f },
				.inactiveBorder  = { 0.42f, 0.42f, 0.42f, 0.55f },
				.inactiveText    = { 0.52f, 0.68f, 0.84f, 1.00f },
				.activeStrip     = { 0.45f, 0.72f, 1.00f, 0.95f },
				.inactiveStrip   = { 0.35f, 0.60f, 1.00f, 0.28f },
			};

			// Inclusion - pale green theme.
			ActorTabColors inclusion {
				.activeButton    = { 0.28f, 0.28f, 0.30f, 0.50f },
				.activeHovered   = { 0.38f, 0.38f, 0.40f, 0.50f },
				.activePressed   = { 0.20f, 0.20f, 0.22f, 0.50f },
				.activeBorder    = { 0.88f, 0.88f, 0.92f, 0.90f },
				.activeText      = { 0.88f, 1.00f, 0.92f, 1.00f },
				.inactiveButton  = { 0.16f, 0.16f, 0.16f, 0.20f },
				.inactiveHovered = { 0.24f, 0.24f, 0.24f, 0.20f },
				.inactivePressed = { 0.11f, 0.11f, 0.11f, 0.20f },
				.inactiveBorder  = { 0.42f, 0.42f, 0.42f, 0.55f },
				.inactiveText    = { 0.58f, 0.78f, 0.64f, 1.00f },
				.activeStrip     = { 0.52f, 0.90f, 0.66f, 0.95f },
				.inactiveStrip   = { 0.42f, 0.78f, 0.56f, 0.28f },
			};

			// Excluded - muted red / dark rose theme.
			ActorTabColors excluded {
				.activeButton    = { 0.28f, 0.28f, 0.30f, 0.50f },
				.activeHovered   = { 0.38f, 0.38f, 0.40f, 0.50f },
				.activePressed   = { 0.20f, 0.20f, 0.22f, 0.50f },
				.activeBorder    = { 0.88f, 0.88f, 0.92f, 0.90f },
				.activeText      = { 0.95f, 0.72f, 0.72f, 1.00f },
				.inactiveButton  = { 0.16f, 0.16f, 0.16f, 0.20f },
				.inactiveHovered = { 0.24f, 0.24f, 0.24f, 0.20f },
				.inactivePressed = { 0.11f, 0.11f, 0.11f, 0.20f },
				.inactiveBorder  = { 0.42f, 0.42f, 0.42f, 0.55f },
				.inactiveText    = { 0.65f, 0.40f, 0.40f, 1.00f },
				.activeStrip     = { 0.75f, 0.28f, 0.28f, 0.95f },
				.inactiveStrip   = { 0.75f, 0.22f, 0.22f, 0.28f },
			};
		};

		const ActorTabStyle kActorTabStyle{};

		// --- View toggle row (Preferences / Spell List) ----------------------
		struct ViewToggleButtonColors
		{
			// Active (selected) state.
			ImGuiMCP::ImVec4 activeButton    {};
			ImGuiMCP::ImVec4 activeHovered   {};
			ImGuiMCP::ImVec4 activePressed   {};
			ImGuiMCP::ImVec4 activeBorder    {};
			ImGuiMCP::ImVec4 activeText      {};
			ImGuiMCP::ImVec4 activeIcon      {};

			// Inactive (unselected) state.
			ImGuiMCP::ImVec4 inactiveButton  {};
			ImGuiMCP::ImVec4 inactiveHovered {};
			ImGuiMCP::ImVec4 inactivePressed {};
			ImGuiMCP::ImVec4 inactiveBorder  {};
			ImGuiMCP::ImVec4 inactiveText    {};
			ImGuiMCP::ImVec4 inactiveIcon    {};
		};

		struct ViewToggleStyle
		{
			// Layout.
			float buttonMinWidthPx = 200.0f;  // Minimum width per button; scales up to fill the available half.
			float buttonHeightPx   = 60.0f;   // Fixed height for each view button.
			float halfCenterGapPx  = 1.0f;    // Gap between the row centre-line and the nearest button edge.

			// Frame shape.
			float rounding   = 0.0f;   // Corner rounding.
			float borderSize = 1.0f;   // Border thickness.

			// Preferences button - pale red theme.
			ViewToggleButtonColors prefs {
				.activeButton    = { 0.55f, 0.18f, 0.18f, 0.20f },
				.activeHovered   = { 0.68f, 0.22f, 0.22f, 0.40f },
				.activePressed   = { 0.44f, 0.14f, 0.14f, 0.40f },
				.activeBorder    = { 1.00f, 0.45f, 0.45f, 0.80f },
				.activeText      = { 1.00f, 0.90f, 0.90f, 1.00f },
				.activeIcon      = { 1.00f, 0.65f, 0.65f, 1.00f },
				.inactiveButton  = { 0.20f, 0.20f, 0.20f, 0.20f },
				.inactiveHovered = { 0.28f, 0.28f, 0.28f, 0.40f },
				.inactivePressed = { 0.14f, 0.14f, 0.14f, 0.40f },
				.inactiveBorder  = { 0.35f, 0.35f, 0.35f, 1.00f },
				.inactiveText    = { 0.65f, 0.65f, 0.65f, 1.00f },
				.inactiveIcon    = { 0.65f, 0.65f, 0.65f, 0.60f },
			};

			// Spell List button - pale purple theme.
			ViewToggleButtonColors spells {
				.activeButton    = { 0.38f, 0.18f, 0.58f, 0.20f },
				.activeHovered   = { 0.48f, 0.24f, 0.72f, 0.40f },
				.activePressed   = { 0.30f, 0.14f, 0.46f, 0.40f },
				.activeBorder    = { 0.80f, 0.50f, 1.00f, 0.80f },
				.activeText      = { 0.95f, 0.88f, 1.00f, 1.00f },
				.activeIcon      = { 0.85f, 0.65f, 1.00f, 1.00f },
				.inactiveButton  = { 0.20f, 0.20f, 0.20f, 0.20f },
				.inactiveHovered = { 0.28f, 0.28f, 0.28f, 0.40f },
				.inactivePressed = { 0.14f, 0.14f, 0.14f, 0.40f },
				.inactiveBorder  = { 0.35f, 0.35f, 0.35f, 1.00f },
				.inactiveText    = { 0.65f, 0.65f, 0.65f, 1.00f },
				.inactiveIcon    = { 0.65f, 0.65f, 0.65f, 0.60f },
			};
		};

		const ViewToggleStyle kViewToggleStyle{};

		// --- Spell List view layout & colors ---------------------------------
		// Controls every visual and layout value used by SpellListView::Render()
		// and BuildDescSegments(). Edit here to tune the spell list without
		// touching any render code.
		struct SpellListStyle
		{
			// -- Panel layout ------------------------------------------------------
			float detailPanelWidthPx = 480.0f;  // Right detail panel fixed width (px).
			float panelGapPx         = 6.0f;    // Gap between left and right panels.
			float detailPadPx        = 12.0f;   // Inner horizontal padding of the right panel.
			float detailPadTopPx     = 0.0f;    // Extra vertical padding at the top of the right panel content.
			float detailPadBottomPx  = 0.0f;    // Extra vertical padding at the bottom of the right panel content.

			// -- School colors -----------------------------------------------------
			ImGuiMCP::ImVec4 colorAlteration  = { 0.48f, 0.62f, 0.75f, 1.0f };
			ImGuiMCP::ImVec4 colorConjuration = { 0.61f, 0.45f, 0.81f, 1.0f };
			ImGuiMCP::ImVec4 colorDestruction = { 0.75f, 0.31f, 0.31f, 1.0f };
			ImGuiMCP::ImVec4 colorIllusion    = { 0.75f, 0.48f, 0.25f, 1.0f };
			ImGuiMCP::ImVec4 colorRestoration = { 0.80f, 0.80f, 0.40f, 1.0f };

			// -- Tier colors (reserved) --------------------------------------------
			ImGuiMCP::ImVec4 colorTierNovice     = { 0.80f, 0.80f, 0.80f, 1.0f };  // skill  <25 - light grey
			ImGuiMCP::ImVec4 colorTierApprentice = { 0.45f, 0.80f, 0.50f, 1.0f };  // skill >=25 - soft green
			ImGuiMCP::ImVec4 colorTierAdept      = { 0.40f, 0.65f, 0.90f, 1.0f };  // skill >=50 - sky blue
			ImGuiMCP::ImVec4 colorTierExpert     = { 0.72f, 0.55f, 0.90f, 1.0f };  // skill >=75 - light purple
			ImGuiMCP::ImVec4 colorTierMaster     = { 0.95f, 0.80f, 0.35f, 1.0f };  // skill>=100 - warm gold

			// -- Left panel: school card -------------------------------------------
			ImGuiMCP::ImVec4 schoolCardNeutralBg = { 0.0f, 0.0f, 0.0f, 0.0f };  // card background (transparent)
			float schoolCardRounding              = 1.0f;   // card corner rounding
			float schoolCardBorderFactor          = 1.0f;   // school-color multiplier for card border RGB
			float schoolCardBorderAlpha           = 0.0f;   // card border alpha
			float cardPadY                        = 0.0f;   // vertical gap: card border -> first/last row
			float cardGapPx                       = 0.0f;   // vertical gap between consecutive school cards

			// -- Left panel: spell row ---------------------------------------------
			float rowExtraPadY     = 16.0f;   // added above/below font: row height = fontSize + 2 * this
			float spellNameLeftPad = 24.0f;  // indent of spell name text from card left border (px)
			ImGuiMCP::ImVec4 spellNameCol  = { 0.92f, 0.92f, 0.92f, 1.0f };  // normal spell name text
			ImGuiMCP::ImVec4 inactiveText  = { 0.40f, 0.40f, 0.40f, 0.80f };  // suppressed spell name text
			ImGuiMCP::ImVec4 inactiveBorder = { 0.40f, 0.40f, 0.40f, 0.80f };  // suppressed accent strip color

			// -- Left panel: accent strip ------------------------------------------
			float accentStripWidthPx = 6.0f;   // strip width (px)
			float accentStripPadY    = 4.0f;   // vertical inset within the row rect

			// -- Left panel: suppressed-spell dot indicator --------------------------
			ImGuiMCP::ImVec4 suppressedDotCol    = { 0.40f, 0.40f, 0.40f, 0.80f };  // dot color (defaults to inactiveText)
			float suppressedDotRadius  = 6.0f;   // circle radius in pixels
			float suppressedDotRightPad = 20.0f;  // extra inset from right edge (0 = centered on accent strip)

			// -- Right panel: container --------------------------------------------
			ImGuiMCP::ImVec4 detailPanelBg = { 0.05f, 0.05f, 0.05f, 0.40f };  // panel background
			ImGuiMCP::ImVec4 sepCol        = { 0.22f, 0.22f, 0.22f, 1.00f };   // panel border / separator
			float detailPanelRounding   = 16.0f;  // panel corner rounding
			float detailPanelBorderSize = 1.0f;  // panel border thickness

			// -- Right panel: title bar --------------------------------------------
			float titleNamePadY     = 4.0f;   // padding above and below spell name (within bg band)
			float titleFontScale    = 1.20f;  // multiplier on default font size for spell name
			ImGuiMCP::ImVec4 titleBarBgMidCol  = { 0.40f, 0.40f, 0.40f, 0.80f };  // gradient center color
			float titleBarBgEdgeAlpha = 0.0f;  // gradient edge alpha (0 = transparent at edges)
			ImGuiMCP::ImVec4 titleTextCol      = { 0.95f, 0.95f, 0.95f, 1.00f };  // spell name (white, ALL CAPS)
			ImGuiMCP::ImVec4 titleTopLineCol   = { 0.80f, 0.80f, 0.80f, 0.20f };  // top separator
			ImGuiMCP::ImVec4 titleBotLineCol   = { 0.80f, 0.80f, 0.80f, 0.20f };  // bottom separator
			float titleLineSize     = 2.0f;   // separator thickness (top & bottom)
			float titleSchoolGapPx  = 2.0f;   // gap between title bg bottom and school label
			float titleSchoolPadB   = 2.0f;   // padding below school label
			float titleSchoolAlpha  = 1.00f;  // school label opacity (color from school theme)

			// -- Right panel: description card -------------------------------------
			ImGuiMCP::ImVec4 descTextCol       = { 0.82f, 0.82f, 0.82f, 1.0f };
			ImGuiMCP::ImVec4 descCardBgCol     = { 0.00f, 0.00f, 0.00f, 0.20f };
			ImGuiMCP::ImVec4 descCardBorderCol = { 0.80f, 0.80f, 0.80f, 0.40f };
			float descCardPadPx       = 24.0f;    // inner text padding (all sides)
			float detailSideMarginPx  = 24.0f;    // extra horizontal margin applied to description+cost/level block (symmetrical)
			float descCardMinHeightPx = 160.0f;   // minimum card height (px)
			float descCardMaxHeightPx = 640.0f;   // maximum card height before clipping (px)
			float descCardRounding    = 1.0f;     // card corner rounding
			float descCardBorderSize  = 1.0f;     // card border thickness

			// -- Right panel: stats & metadata -------------------------------------
			ImGuiMCP::ImVec4 labelCol        = { 0.50f, 0.50f, 0.50f, 1.0f };  // stat/metadata label text
			ImGuiMCP::ImVec4 metaCol         = { 0.72f, 0.72f, 0.72f, 1.0f };  // metadata value text
			ImGuiMCP::ImVec4 tagNumericCol   = { 0.90f, 0.85f, 0.55f, 1.0f };  // numeric <tag> highlight
			ImGuiMCP::ImVec4 magnitudeColor  = { 0.80f, 0.42f, 0.42f, 1.0f };  // magnitude value
			ImGuiMCP::ImVec4 durationColor   = { 0.76f, 0.60f, 0.94f, 1.0f };  // duration value
			ImGuiMCP::ImVec4 areaColor       = { 0.62f, 0.90f, 0.66f, 1.0f };  // area value
			ImGuiMCP::ImVec4 costColor       = { 0.55f, 0.78f, 0.95f, 1.0f };  // cost value
			ImGuiMCP::ImVec4 statsLabelCol   = { 0.82f, 0.82f, 0.82f, 1.0f };  // stat row labels (Magnitude/Duration/Area)
			ImGuiMCP::ImVec4 perSecSuffixCol = { 0.45f, 0.62f, 0.78f, 0.85f }; // small "per sec" suffix for concentration spells
			ImGuiMCP::ImVec4 survivalColor   = { 0.60f, 0.85f, 0.70f, 1.0f };  // Survival Mode condition text ([SURV=...])

			// -- Right panel: casting badge --------------------------------------
			ImGuiMCP::ImVec4 warnBg       = { 0.40f, 0.40f, 0.40f, 0.20f };  // box background
			ImGuiMCP::ImVec4 warnBorder   = { 0.40f, 0.40f, 0.40f, 0.20f };  // box border
			ImGuiMCP::ImVec4 warnText     = { 0.72f, 0.72f, 0.72f, 0.65f };  // badge label text (amber)
			float warnHeightPx     = 50.0f;  // box height
			float warnWidthPx      = 150.0f; // box width
			float warnTextMarginPx = 4.0f;   // right inset for text wrap
			float warnRounding     = 16.0f;  // box corner rounding
			float warnBorderSize   = 0.0f;   // box border thickness

			// -- Right panel: base-spell badge ------------------------------------
			ImGuiMCP::ImVec4 baseBadgeBg     = { 0.40f, 0.40f, 0.40f, 0.20f };  // badge background
			ImGuiMCP::ImVec4 baseBadgeBorder = { 0.40f, 0.40f, 0.40f, 0.20f };  // badge border
			ImGuiMCP::ImVec4 baseBadgeText   = { 0.72f, 0.72f, 0.72f, 0.65f };  // badge label text
			float baseBadgeHeightPx  = 50.0f;  // badge height
			float baseBadgeWidthPx   = 150.0f; // badge width
			float baseBadgeRounding  = 16.0f;  // badge corner rounding
			float baseBadgeBorderSize = 0.0f;  // badge border thickness
			// -- Left panel: base-spell section separator -------------------------
			ImGuiMCP::ImVec4 baseSepCol      = { 0.72f, 0.72f, 0.72f, 0.65f };  // separator line color
			float baseSepPadTopPx    = 16.0f;  // gap above the separator (Dummy height)
			float baseSepPadBottomPx = 16.0f;  // gap below the separator (Dummy height)

			// -- Right panel: forget / remember button -------------------------
			ImGuiMCP::ImVec4 suppressBg     = { 0.38f, 0.38f, 0.38f, 0.20f };
			ImGuiMCP::ImVec4 suppressHov    = { 0.48f, 0.48f, 0.48f, 0.40f };
			ImGuiMCP::ImVec4 suppressPrs    = { 0.28f, 0.28f, 0.28f, 0.40f };
			ImGuiMCP::ImVec4 suppressBorder       = { 0.95f, 0.95f, 0.95f, 1.00f };
			ImGuiMCP::ImVec4 suppressBtnTextCol   = { 0.95f, 0.95f, 0.95f, 1.00f };
			ImGuiMCP::ImVec4 unsuppressBtnTextCol = { 0.95f, 0.95f, 0.95f, 1.00f };
			float unsuppressBgFactor     = 0.30f;  float unsuppressBgAlpha     = 0.45f;
			float unsuppressHovFactor    = 0.45f;  float unsuppressHovAlpha    = 0.45f;
			float unsuppressPrsFactor    = 0.20f;  float unsuppressPrsAlpha    = 0.95f;
			float unsuppressBorderFactor = 1.00f;  float unsuppressBorderAlpha = 0.95f;
			float actionBtnRounding      = 0.0f;
			float actionBtnBorderSize    = 2.0f;
			float actionBtnW             = 150.0f;
			float actionBtnH             = 50.0f;
			float statsSepGapPx          = 4.0f;    // gap (Dummy height) between stats+button block and separator
			float pluginInfoPadTopPx     = 0.0f;    // gap (Dummy height) between separator and plugin text
		};

		const SpellListStyle kSpellListStyle{};

		// --- Actor tab-strip icon colors ------------------------------------
		// Colors for every clickable icon in the left and right icon groups
		// of the Preference Capture actor tab strip.
		struct ActorTabStripIconColors
		{
			// Left group - actor-exclude icon (shown when actor is active).
			ImGuiMCP::ImVec4 excludeActive    { 0.70f, 0.35f, 0.35f, 1.0f };
			ImGuiMCP::ImVec4 excludeInactive  { 0.70f, 0.35f, 0.35f, 0.35f };

			// Left group - actor-include icon (shown when actor is already excluded).
			ImGuiMCP::ImVec4 includeActive    { 0.38f, 0.65f, 0.53f, 1.0f };
			ImGuiMCP::ImVec4 includeInactive  { 0.38f, 0.65f, 0.53f, 0.35f };

			// Left group - actor-remove icon.
			ImGuiMCP::ImVec4 removeActive     { 0.64f, 0.64f, 0.64f, 1.0f };
			ImGuiMCP::ImVec4 removeInactive   { 0.64f, 0.64f, 0.64f, 0.35f };

			// Right group - dismissed-follower toggle.
			ImGuiMCP::ImVec4 dismissedOn      { 0.64f, 0.64f, 0.64f, 1.0f };
			ImGuiMCP::ImVec4 dismissedOff     { 0.50f, 0.50f, 0.50f, 0.50f };

			// Right group - show-excluded-actors toggle.
			ImGuiMCP::ImVec4 showExcludedOn   { 0.70f, 0.35f, 0.35f, 1.0f };
			ImGuiMCP::ImVec4 showExcludedOff  { 0.50f, 0.50f, 0.50f, 0.50f };

			// Right group - inclusion-actor toggle.
			ImGuiMCP::ImVec4 inclusionOn      { 0.38f, 0.65f, 0.53f, 1.0f };
			ImGuiMCP::ImVec4 inclusionOff     { 0.50f, 0.50f, 0.50f, 0.50f };

			// Right group - summon-actor toggle.
			ImGuiMCP::ImVec4 summonOn         { 0.25f, 0.45f, 0.70f, 1.0f };
			ImGuiMCP::ImVec4 summonOff        { 0.50f, 0.50f, 0.50f, 0.50f };
		};

		const ActorTabStripIconColors kIconColors{};

		// --- Actor confirmation modal geometry ------------------------------
		// Controls the size and internal spacing of the Exclude/Include/Remove
		// confirmation pop-up modals.
		struct ActorConfirmModalStyle
		{
			float width           = 600.0f; // Modal width in pixels.
			float height          = 400.0f; // Minimum modal height in pixels; auto-expands if content requires more.
			float headerPadTopPx  = 6.0f;   // Vertical gap above the actor name.
			float headerPadBotPx  = 4.0f;   // Vertical gap below the actor name (before separator).
			float bodyPadTopPx    = 8.0f;   // Minimum vertical gap above body text (after separator).
			float bodyTextPadX    = 0.0f;   // Horizontal padding on each side of the body text block.
			float buttonPadBotPx  = 8.0f;   // Vertical gap below button row (to content bottom).
			float buttonWidthFrac = 0.42f;  // Each button width as a fraction of content width.
			float buttonHeightPx  = 0.0f;   // Button height; 0 = auto (ImGui default).
		};

		const ActorConfirmModalStyle kActorConfirmModalStyle{};

		//=============================================================================
		// [SECTION 2] Icon Constants - FontAwesome string literals
		//=============================================================================

		// Use SKSE Menu Framework's built-in FontAwesome font to render icons reliably.

		// Back to default icon.
		static const std::string kDefaultIcon = FontAwesome::UnicodeToUtf8(0xF0E2);					// undo

		// Toolbar icons.
		static const std::string kSaveIcon = FontAwesome::UnicodeToUtf8(0xF0C7);					// save
		static const std::string kReloadIcon = FontAwesome::UnicodeToUtf8(0xF021);					// sync/refresh
		static const std::string kResetAllIcon = FontAwesome::UnicodeToUtf8(0xF112);				// reply
		static const std::string kBackIcon = FontAwesome::UnicodeToUtf8(0xF090);					// arrow-right-to-bracket
		static const std::string kHelpIcon = FontAwesome::UnicodeToUtf8(0xF059);					// question-circle
		static const std::string kTitleIcon = FontAwesome::UnicodeToUtf8(0xF052);					// eject
		static const std::string kDirtyIcon = FontAwesome::UnicodeToUtf8(0xF06A);					// exclamation-circle

		// Title icon scale (relative to the FontAwesome font size).
		constexpr float kTitleIconScale = kTitleLineStyle.iconScale;

		// Section icons (keep in canonical feature order).
		static const std::string kIconCore = FontAwesome::UnicodeToUtf8(0xF085);					// gears
		static const std::string kIconUIFeedback = FontAwesome::UnicodeToUtf8(0xF27A);				// message
		static const std::string kIconLogging = FontAwesome::UnicodeToUtf8(0xF15C);					// file-text
		static const std::string kIconActorScope = FontAwesome::UnicodeToUtf8(0xF0C0);				// users
		static const std::string kIconQuickTrade = FontAwesome::UnicodeToUtf8(0xF0EC);				// exchange
		static const std::string kIconControls = FontAwesome::UnicodeToUtf8(0xF11C);				// keyboard
		static const std::string kIconGamepadControls = FontAwesome::UnicodeToUtf8(0xF11B);			// gamepad
		static const std::string kIconButtonIndicators = FontAwesome::UnicodeToUtf8(0xF2D0);		// window-maximize
		static const std::string kIconAppearance = FontAwesome::UnicodeToUtf8(0xF02E);				// bookmark
		static const std::string kIconEquipMode = FontAwesome::UnicodeToUtf8(0xF0E3);				// gavel
		static const std::string kIconConsumable = FontAwesome::UnicodeToUtf8(0xF0C3);				// flask
		static const std::string kIconPoison = FontAwesome::UnicodeToUtf8(0xF043);					// tint
		static const std::string kIconSpellTome = FontAwesome::UnicodeToUtf8(0xF02D);				// book
		static const std::string kIconWeaponRecharge = FontAwesome::UnicodeToUtf8(0xF0E7);			// bolt
		static const std::string kIconCombatEquip = FontAwesome::UnicodeToUtf8(0xF255);				// hand-grab
		static const std::string kIconCombatEquipEnforcement = FontAwesome::UnicodeToUtf8(0xF023);	// lock
		static const std::string kIconCombatEquipRestore = FontAwesome::UnicodeToUtf8(0xF1DA);		// history
		static const std::string kIconOutfitSync = FontAwesome::UnicodeToUtf8(0xE54D);				// person-rays
		static const std::string kIconHiddenItems = FontAwesome::UnicodeToUtf8(0xF2A8);				// eye-low-vision
		static const std::string kIconItemInjectionBlocking = FontAwesome::UnicodeToUtf8(0xF05E);	// ban
		static const std::string kIconLootBlocking = FontAwesome::UnicodeToUtf8(0xE56A);			// sack-xmark
		static const std::string kIconAutoEquipBlocking = FontAwesome::UnicodeToUtf8(0xF6DE);		// hand-fist
		static const std::string kIconEquipGate = FontAwesome::UnicodeToUtf8(0xF132);				// shield
		static const std::string kIconStatsDisplay = FontAwesome::UnicodeToUtf8(0xF828);			// bars-progress
		static const std::string kIconCorpseEquip = FontAwesome::UnicodeToUtf8(0xE546);				// person-falling
		static const std::string kIconFixes = FontAwesome::UnicodeToUtf8(0xF0AD);					// wrench
		static const std::string kIconFixesEngine = FontAwesome::UnicodeToUtf8(0xF013);				// cog
		static const std::string kIconFixesSkyUI  = FontAwesome::UnicodeToUtf8(0xF3FB);				// tablet

		// Dismissed follower toggle icons.
		static const std::string kIconActiveOnly = FontAwesome::UnicodeToUtf8(0xF0C0);				// users
		static const std::string kIconAllFollowers = FontAwesome::UnicodeToUtf8(0xF0C0);			// users

		// Summon and inclusion actor filter toggle icons.
		static const std::string kIconSummonsShow   = FontAwesome::UnicodeToUtf8(0xF470);			// per1son-dots-from-line
		static const std::string kIconSummonsHide   = FontAwesome::UnicodeToUtf8(0xF470);			// person-dots-from-line
		static const std::string kIconInclusionShow = FontAwesome::UnicodeToUtf8(0xE4B9);			// arrows-down-to-people
		static const std::string kIconInclusionHide = FontAwesome::UnicodeToUtf8(0xE4B9);			// arrows-down-to-people

		// Excluded-actors show/hide toggle (right icon group).
		static const std::string kIconExcludedShow = FontAwesome::UnicodeToUtf8(0xE073);			// users-slash

		// Actor action icons.
		static const std::string kIconActorExclude = FontAwesome::UnicodeToUtf8(0xE540);			// person-circle-minus
		static const std::string kIconActorInclude = FontAwesome::UnicodeToUtf8(0xE541);			// person-circle-plus
		static const std::string kIconActorRemove   = FontAwesome::UnicodeToUtf8(0xE543);			// person-circle-xmark

		// Cross-clear reference table icon (Preference Capture tab strip).
		static const std::string kIconCrossClear = FontAwesome::UnicodeToUtf8(0xF065);				// expand
		static const std::string kIconCrossClearClose = FontAwesome::UnicodeToUtf8(0xF066);			// compress

		// Actor Management view toggle icons.
		static const std::string kIconViewPreferences = FontAwesome::UnicodeToUtf8(0xF255);			// hand-grab
		static const std::string kIconViewSpellList   = FontAwesome::UnicodeToUtf8(0xE05D);			// hand-sparkles

		//=============================================================================
		// [SECTION 3] Widget Layer
		//   3a. ConnectorBracket + control width helpers
		//   3b. Key picker widgets (DikKey, Gamepad)
		//   3c. Button / header / nav renderers
		//   3d. Layout helpers (align, tooltip, icon widths)
		//=============================================================================

		struct ConnectorBracket
		{
			struct Join
			{
				float y{};
				float x{};
			};

			void CaptureToggleFromLastItem()
			{
				ImGuiMCP::ImGui::GetItemRectMin(&toggleMin);
				ImGuiMCP::ImGui::GetItemRectMax(&toggleMax);
				haveToggle = true;
			}

			void AddJoinFromLastItem(float xOffset = -1.0f)
			{
				ImGuiMCP::ImVec2 itemMin{};
				ImGuiMCP::ImVec2 itemMax{};
				ImGuiMCP::ImGui::GetItemRectMin(&itemMin);
				ImGuiMCP::ImGui::GetItemRectMax(&itemMax);

				const float y = itemMin.y + ((itemMax.y - itemMin.y) * 0.5f);
				const float x = itemMin.x + xOffset;
				if (joinCount < joins.size()) {
					joins[joinCount++] = Join{ y, x };
				}
				UpdateBottomFromLastItem();
			}

			void UpdateBottomFromLastItem()
			{
				ImGuiMCP::ImVec2 itemMin{};
				ImGuiMCP::ImVec2 itemMax{};
				ImGuiMCP::ImGui::GetItemRectMin(&itemMin);
				ImGuiMCP::ImGui::GetItemRectMax(&itemMax);

				const float y = itemMin.y + ((itemMax.y - itemMin.y) * 0.5f);
				if (!haveBottom || y > bottomY) {
					bottomY = y;
					haveBottom = true;
				}
			}

			void Draw(ImGuiMCP::ImGuiCol color = ImGuiMCP::ImGuiCol_Border, float thickness = kConnectorStyle.thickness, float yTopPad = kConnectorStyle.yTopPadPx) const
			{
				if (!haveToggle || !haveBottom || joinCount == 0u) {
					return;
				}

				auto* drawList = ImGuiMCP::ImGui::GetWindowDrawList();
				const auto col = ImGuiMCP::ImGui::GetColorU32(color);

				const auto* style = ImGuiMCP::ImGui::GetStyle();
				const float padX = style ? style->FramePadding.x : 0.0f;
				const float xLine = toggleMin.x + (std::max)(kConnectorStyle.minXOffsetPx, padX);
				const float yTop = toggleMax.y + yTopPad;
				const float yBottom = bottomY;

				if (yBottom <= yTop + kConnectorStyle.minHeightPx) {
					return;
				}

				ImGuiMCP::ImGui::ImDrawListManager::AddLine(drawList, ImGuiMCP::ImVec2(xLine, yTop), ImGuiMCP::ImVec2(xLine, yBottom), col, thickness);
				for (std::size_t i = 0; i < joinCount; ++i) {
					ImGuiMCP::ImGui::ImDrawListManager::AddLine(drawList, ImGuiMCP::ImVec2(xLine, joins[i].y), ImGuiMCP::ImVec2(joins[i].x, joins[i].y), col, thickness);
				}
			}

			ImGuiMCP::ImVec2 toggleMin{};
			ImGuiMCP::ImVec2 toggleMax{};
			bool haveToggle{ false };

			float bottomY{ 0.0f };
			bool haveBottom{ false };

			std::array<Join, 8> joins{};
			std::size_t joinCount{ 0u };
		};

		inline void SettingsItemGap()
		{
			ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2(0.0f, kSpacingStyle.settingsItemGapPx));
		}

		inline void SettingsSectionDivider()
		{
			ImGuiMCP::ImGui::Separator();
			SettingsItemGap();
		}

		inline void ToolbarGap()
		{
			ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2(0.0f, kSpacingStyle.toolbarGapPx));
		}

		enum class ButtonTheme
		{
			kNeutral,
			kEmphasis,
			kGhost,
		};

		struct ScopedStyleColors
		{
			int count{ 0 };

			ScopedStyleColors() = default;
			explicit ScopedStyleColors(int c) : count(c) {}
			ScopedStyleColors(const ScopedStyleColors&) = delete;
			ScopedStyleColors& operator=(const ScopedStyleColors&) = delete;
			ScopedStyleColors(ScopedStyleColors&& other) noexcept : count(other.count) { other.count = 0; }
			ScopedStyleColors& operator=(ScopedStyleColors&& other) noexcept
			{
				if (this != &other) {
					if (count > 0) {
						ImGuiMCP::ImGui::PopStyleColor(count);
					}
					count = other.count;
					other.count = 0;
				}
				return *this;
			}
			~ScopedStyleColors()
			{
				if (count > 0) {
					ImGuiMCP::ImGui::PopStyleColor(count);
				}
			}
		};

		[[nodiscard]] inline ScopedStyleColors PushButtonTheme(ButtonTheme theme)
		{
			const auto& t = kButtonTheme;
			switch (theme) {
			case ButtonTheme::kEmphasis:
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, t.emphasisButton);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, t.emphasisHovered);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, t.emphasisActive);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, t.emphasisText);
				return ScopedStyleColors{ 4 };
			case ButtonTheme::kGhost:
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, t.ghostButton);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, t.ghostHovered);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, t.ghostActive);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, t.ghostText);
				return ScopedStyleColors{ 4 };
			case ButtonTheme::kNeutral:
			default:
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, t.neutralButton);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, t.neutralHovered);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, t.neutralActive);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, t.neutralText);
				return ScopedStyleColors{ 4 };
			}
		}

		[[nodiscard]] inline ScopedStyleColors PushActorTabColors(const ActorTabColors& C, bool selected)
		{
			ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button,        selected ? C.activeButton   : C.inactiveButton);
			ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, selected ? C.activeHovered  : C.inactiveHovered);
			ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive,  selected ? C.activePressed  : C.inactivePressed);
			ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Border,        selected ? C.activeBorder   : C.inactiveBorder);
			ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text,          selected ? C.activeText     : C.inactiveText);
			return ScopedStyleColors{ 5 };
		}

		// Control width knobs (aliased from ControlWidthStyle).
		constexpr float kNarrowControlWidthFactor   = kControlWidthStyle.narrowFactor;
		constexpr float kNarrowControlMinWidthPx    = kControlWidthStyle.narrowMinPx;
		constexpr float kNarrowControlMaxWidthPx    = kControlWidthStyle.narrowMaxPx;

		constexpr float kSegmentedControlWidthFactor = kControlWidthStyle.segmentedFactor;
		constexpr float kSegmentedControlMinWidthPx  = kControlWidthStyle.segmentedMinPx;
		constexpr float kSegmentedControlMaxWidthPx  = kControlWidthStyle.segmentedMaxPx;

		constexpr float kSliderControlWidthFactor   = kControlWidthStyle.sliderFactor;
		constexpr float kSliderControlMinWidthPx    = kControlWidthStyle.sliderMinPx;
		constexpr float kSliderControlMaxWidthPx    = kControlWidthStyle.sliderMaxPx;

		[[nodiscard]] float CalcAnchoredControlWidthPx(float widthFactor, float minWidthPx, float maxWidthPx)
		{
			// Compute a "narrow" width anchored to the window content width, then shrink it
			// by the current X offset (e.g., Indent) so sub-items keep the same right edge.
			ImGuiMCP::ImVec2 contentMax{};
			ImGuiMCP::ImGui::GetWindowContentRegionMax(&contentMax);
			const float cursorX = ImGuiMCP::ImGui::GetCursorPosX();

			float baseW = contentMax.x * widthFactor;
			baseW = (std::max)(baseW, minWidthPx);
			baseW = (std::min)(baseW, maxWidthPx);

			float w = baseW - cursorX;
			const float remaining = contentMax.x - cursorX;
			w = (std::min)(w, remaining);
			w = (std::max)(w, 1.0f);
			return w;
		}

		[[nodiscard]] float CalcNarrowControlWidthPx()
		{
			return CalcAnchoredControlWidthPx(kNarrowControlWidthFactor, kNarrowControlMinWidthPx, kNarrowControlMaxWidthPx);
		}

		[[nodiscard]] float CalcSegmentedControlWidthPx()
		{
			return CalcAnchoredControlWidthPx(kSegmentedControlWidthFactor, kSegmentedControlMinWidthPx, kSegmentedControlMaxWidthPx);
		}

		[[nodiscard]] float CalcSliderControlWidthPx()
		{
			return CalcAnchoredControlWidthPx(kSliderControlWidthFactor, kSliderControlMinWidthPx, kSliderControlMaxWidthPx);
		}

		inline void SetNarrowControlWidth()
		{
			ImGuiMCP::ImGui::SetNextItemWidth(CalcNarrowControlWidthPx());
		}

		inline void SetSliderControlWidth()
		{
			ImGuiMCP::ImGui::SetNextItemWidth(CalcSliderControlWidthPx());
		}

		template <class DefaultFn>
		void HelpAndDefaultOnLine(const char* helpKey, const char* defaultButtonId, DefaultFn&& defaultFn);

		template <class DefaultFn>
		void HelpAndDefaultOnLine(const char* helpKey, const char* defaultButtonId, std::string_view defaultValueText, DefaultFn&& defaultFn);

		void MarkDirty();

		// -- Shared key-combo picker implementation ------------------------------
		// Preview strings (current selection and default) are precomputed by each
		// wrapper so per-picker range-check / fallback semantics are preserved.
		template <typename NameLookupFn>
		void RenderKeyComboImpl(
			std::string_view  a_labelKey,
			std::string_view  a_idSuffix,
			int&              a_ioKey,
			const char*       a_helpKey,
			const char*       a_defaultButtonId,
			std::uint32_t     a_defaultKey,
			std::string       a_preview,
			std::string       a_defaultPreview,
			std::uint32_t     a_keyRangeBegin,
			std::uint32_t     a_keyRangeEnd,
			const char*       a_maxCodePreview,
			const char*       a_rowIdPrefix,
			NameLookupFn      a_nameLookup,
			ConnectorBracket* a_connector = nullptr,
			const char*       a_noneLabel = nullptr)
		{
			const auto rawCurrent = a_ioKey < 0 ? 0u : static_cast<std::uint32_t>(a_ioKey);
			SetNarrowControlWidth();
			const auto label = a_noneLabel ? std::string(a_idSuffix) : Label(a_labelKey, a_idSuffix);
			{
				const int kVisibleRows = kDikKeyComboStyle.visibleRows;
				const float maxPopupH = ImGuiMCP::ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(kVisibleRows);
				ImGuiMCP::ImGui::SetNextWindowSizeConstraints(
					ImGuiMCP::ImVec2(0.0f, 0.0f),
					ImGuiMCP::ImVec2(1000000.0f, maxPopupH));
			}
			// Dim the combo button preview when None (0) is selected.
			// Pushed before BeginCombo, popped inside the open-popup path and in the
			// closed-combo fallthrough, so only the preview text is dimmed.
			bool dimPushed = false;
			if (a_noneLabel && rawCurrent == 0u) {
				const auto* style = ImGuiMCP::ImGui::GetStyle();
				const auto& baseCol = style ? style->Colors[static_cast<int>(ImGuiMCP::ImGuiCol_Text)] : ImGuiMCP::ImVec4(1, 1, 1, 1);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, ImGuiMCP::ImVec4(baseCol.x, baseCol.y, baseCol.z, 0.5f));
				dimPushed = true;
			}
			if (ImGuiMCP::ImGui::BeginCombo(label.c_str(), a_preview.c_str(), ImGuiMCP::ImGuiComboFlags_HeightRegular)) {
				// Pop before rendering rows so the popup uses normal text color.
				if (dimPushed) {
					ImGuiMCP::ImGui::PopStyleColor(1);
					dimPushed = false;
				}
				const auto* style = ImGuiMCP::ImGui::GetStyle();
				const float padX = style ? style->FramePadding.x : 0.0f;
				const float inner = style ? style->ItemInnerSpacing.x : 0.0f;
				// Horizontal padding on both sides of the keycode/name separator line.
				ImGuiMCP::ImVec2 spaceSize{};
				ImGuiMCP::ImGui::CalcTextSize(&spaceSize, " ", nullptr, false, 0.0f);
				const float spaceW = (std::max)(1.0f, spaceSize.x);
				const float sepPadX = (std::max)(12.0f, (std::max)(spaceW * 3.0f, inner * 1.5f));
				// Width of the numeric column (right-aligned), based on the widest value shown.
				ImGuiMCP::ImVec2 maxCodeSize{};
				ImGuiMCP::ImGui::CalcTextSize(&maxCodeSize, a_maxCodePreview, nullptr, false, 0.0f);
				const float codeColW = maxCodeSize.x;

				auto renderRow = [&](std::uint32_t code, const char* name, bool selected, const char* rowId) {
					const bool pressed = ImGuiMCP::ImGui::Selectable(rowId, selected);
					auto* drawList = ImGuiMCP::ImGui::GetWindowDrawList();
					const auto textColor = ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImGuiCol_Text);
					ImGuiMCP::ImVec2 rectMin{};
					ImGuiMCP::ImGui::GetItemRectMin(&rectMin);
					ImGuiMCP::ImVec2 rectMax{};
					ImGuiMCP::ImGui::GetItemRectMax(&rectMax);
					const float h = rectMax.y - rectMin.y;
					std::string codeStr = (code == 0u) ? "" : std::to_string(code);
					ImGuiMCP::ImVec2 codeSize{};
					ImGuiMCP::ImGui::CalcTextSize(&codeSize, codeStr.c_str(), nullptr, false, 0.0f);
					ImGuiMCP::ImVec2 nameSize{};
					ImGuiMCP::ImGui::CalcTextSize(&nameSize, name, nullptr, false, 0.0f);
					const float xStart = rectMin.x + padX;
					const float yCode = rectMin.y + (h - codeSize.y) * 0.5f;
					const float yName = rectMin.y + (h - nameSize.y) * 0.5f;
					const float xCode = xStart + codeColW - codeSize.x;  // right-aligned in numeric column
					const float xSep = xStart + codeColW + sepPadX;
					const float xName = xSep + sepPadX;
					ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2(xCode, yCode), textColor, codeStr.c_str());
					{
						const auto sepColor = ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImGuiCol_TextDisabled);
						const float y1 = rectMin.y + kDikKeyComboStyle.separatorVertInsetPx;
						const float y2 = rectMax.y - kDikKeyComboStyle.separatorVertInsetPx;
						ImGuiMCP::ImGui::ImDrawListManager::AddLine(drawList, ImGuiMCP::ImVec2(xSep, y1), ImGuiMCP::ImVec2(xSep, y2), sepColor, kDikKeyComboStyle.separatorThickness);
					}
					ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2(xName, yName), textColor, name);
					return pressed;
				};

				// Optional "None" row at the top (value 0 = no override).
				if (a_noneLabel) {
					std::string noneId = "##";
					noneId += a_rowIdPrefix;
					noneId += "None";
					if (renderRow(0u, a_noneLabel, rawCurrent == 0u, noneId.c_str())) {
						a_ioKey = 0;
						MarkDirty();
					}
				}
				for (std::uint32_t code = a_keyRangeBegin; code <= a_keyRangeEnd; ++code) {
					auto name = a_nameLookup(code);
					if (name.empty()) continue;
					const bool selected = (rawCurrent == code);
					std::string rowId = "##";
					rowId += a_rowIdPrefix;
					rowId += std::to_string(code);
					if (renderRow(code, name.c_str(), selected, rowId.c_str())) {
						a_ioKey = static_cast<int>(code);
						MarkDirty();
					}
				}
				ImGuiMCP::ImGui::EndCombo();
			}
			// Pop in the closed-combo path (when None is selected but popup is not open).
			if (dimPushed) {
				ImGuiMCP::ImGui::PopStyleColor(1);
			}
			// Render the visible label to the right of the combo (only when hidden label was used).
			if (a_noneLabel) {
				const auto* imStyle = ImGuiMCP::ImGui::GetStyle();
				const float spacing = imStyle ? imStyle->ItemInnerSpacing.x : 4.0f;
				ImGuiMCP::ImGui::SameLine(0.0f, spacing);
				ImGuiMCP::ImGui::TextUnformatted(Localization::CStr(a_labelKey));
			}
			if (a_connector) {
				a_connector->AddJoinFromLastItem();
			}
			HelpAndDefaultOnLine(a_helpKey, a_defaultButtonId, a_defaultPreview, [&]() {
				a_ioKey = static_cast<int>(a_defaultKey);
			});
		}

		void RenderGamepadKeyCombo(
			std::string_view a_labelKey,
			std::string_view a_idSuffix,
			int& a_ioKey,
			const char* a_helpKey,
			const char* a_defaultButtonId,
			std::uint32_t a_defaultKey,
			ConnectorBracket* a_connector = nullptr,
			const char* a_noneLabel = nullptr)
		{
			const auto rawCurrent = a_ioKey < 0 ? 0u : static_cast<std::uint32_t>(a_ioKey);
			std::string defaultPreview;
			if (a_defaultKey == 0u) {
				defaultPreview = a_noneLabel ? a_noneLabel : "0";
			} else {
				auto defaultName = Controls::GetGamepadButtonName(a_defaultKey);
				defaultPreview = defaultName.empty() ? std::to_string(a_defaultKey) : defaultName;
			}
			std::string preview;
			if (rawCurrent == 0u || rawCurrent < Controls::kGamepadOffset || rawCurrent >= Controls::kGamepadOffset + 16) {
				preview = a_noneLabel ? a_noneLabel : defaultPreview;
			} else {
				auto name = Controls::GetGamepadButtonName(rawCurrent);
				preview = name.empty() ? std::to_string(rawCurrent) : name;
			}
			RenderKeyComboImpl(
				a_labelKey, a_idSuffix, a_ioKey,
				a_helpKey, a_defaultButtonId, a_defaultKey,
				std::move(preview), std::move(defaultPreview),
				Controls::kGamepadOffset, Controls::kGamepadOffset + 15u,
				"281", "Gp",
				[](std::uint32_t code) { return Controls::GetGamepadButtonName(code); },
				a_connector, a_noneLabel);
		}

		void RenderDikKeyCombo(
			std::string_view a_labelKey,
			std::string_view a_idSuffix,
			int& a_ioDik,
			const char* a_helpKey,
			const char* a_defaultButtonId,
			std::uint32_t a_defaultDik,
			ConnectorBracket* a_connector = nullptr,
			const char* a_noneLabel = nullptr)
		{
			const auto rawCurrent = a_ioDik < 0 ? 0u : static_cast<std::uint32_t>(a_ioDik);
			std::string defaultPreview;
			if (a_defaultDik == 0u) {
				defaultPreview = a_noneLabel ? a_noneLabel : "0";
			} else {
				auto defaultName = Controls::GetDikKeyName(a_defaultDik);
				defaultPreview = defaultName.empty() ? std::to_string(a_defaultDik) : defaultName;
			}
			std::string preview;
			if (rawCurrent == 0u) {
				// 0 is treated as "unset"; preview the default keycode as a numeric fallback.
				preview = a_noneLabel ? a_noneLabel : std::to_string(a_defaultDik);
			} else {
				auto name = Controls::GetDikKeyName(rawCurrent);
				preview = name.empty() ? std::to_string(rawCurrent) : name;
			}
			RenderKeyComboImpl(
				a_labelKey, a_idSuffix, a_ioDik,
				a_helpKey, a_defaultButtonId, a_defaultDik,
				std::move(preview), std::move(defaultPreview),
				1u, 255u,
				"255", "Dik",
				[](std::uint32_t code) { return Controls::GetDikKeyName(code); },
				a_connector, a_noneLabel);
		}

		void RenderTitleLine(const char* title)
		{
			// Draw a rotated title icon (90 degrees to the right).
			// ImGui doesn't support rotated text directly, so we draw to the window draw list and rotate
			// the vertices that were just emitted.
			auto rotateJustAddedVertices = [](auto* drawList, int vtxStart, float angleRad, ImGuiMCP::ImVec2 pivot) {
				const float c = std::cos(angleRad);
				const float s = std::sin(angleRad);
				for (int i = vtxStart; i < drawList->VtxBuffer.Size; ++i) {
					auto& v = drawList->VtxBuffer.Data[i];
					const float x = v.pos.x - pivot.x;
					const float y = v.pos.y - pivot.y;
					v.pos.x = pivot.x + (x * c - y * s);
					v.pos.y = pivot.y + (x * s + y * c);
				}
			};

			ImGuiMCP::ImGui::AlignTextToFramePadding();
			auto* drawList = ImGuiMCP::ImGui::GetWindowDrawList();
			const auto textColor = ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImGuiCol_Text);
			const auto* style = ImGuiMCP::ImGui::GetStyle();
			const float spacingX = style ? style->ItemSpacing.x : 0.0f;

			ImGuiMCP::ImVec2 iconSize{};
			ImGuiMCP::ImFont* iconFont = nullptr;
			float iconFontSize = 0.0f;
			FontAwesome::PushSolid();
			iconFont = ImGuiMCP::ImGui::GetFont();
			iconFontSize = ImGuiMCP::ImGui::GetFontSize();
			ImGuiMCP::ImGui::CalcTextSize(&iconSize, kTitleIcon.c_str(), nullptr, false, 0.0f);
			FontAwesome::Pop();
			iconSize.x *= kTitleIconScale;
			iconSize.y *= kTitleIconScale;

			ImGuiMCP::ImVec2 titleSize{};
			ImGuiMCP::ImGui::CalcTextSize(&titleSize, title, nullptr, false, 0.0f);

			const float box = (std::max)((std::max)(iconSize.x, iconSize.y), titleSize.y);
			const auto boxSize = ImGuiMCP::ImVec2(box, box);
			ImGuiMCP::ImVec2 pos{};
			ImGuiMCP::ImGui::GetCursorScreenPos(&pos);
			const auto iconPos = ImGuiMCP::ImVec2(pos.x + (box - iconSize.x) * 0.5f, pos.y + (box - iconSize.y) * 0.5f);
			const auto pivot = ImGuiMCP::ImVec2(pos.x + box * 0.5f, pos.y + box * 0.5f);

			FontAwesome::PushSolid();
			const int vtxStart = drawList->VtxBuffer.Size;
			ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, iconFont, iconFontSize * kTitleIconScale, iconPos, textColor, kTitleIcon.c_str());
			// In ImGui screen coordinates (Y down), +90deg is a clockwise/right rotation.
			constexpr float kPi = 3.14159265358979323846f;
			rotateJustAddedVertices(drawList, vtxStart, kPi * 0.5f, pivot);
			FontAwesome::Pop();

			// Draw title text vertically centered with the icon block.
			const auto titlePos = ImGuiMCP::ImVec2(pos.x + box + spacingX, pos.y + (box - titleSize.y) * 0.5f);
			ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, titlePos, textColor, title);

			// Advance the layout cursor by the combined line height.
			ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2(box + spacingX + titleSize.x, box));
		}

		[[nodiscard]] bool CollapsingHeaderWithLeadingIcon(const char* id, const std::string& icon, const char* label)
		{
			// Draw the icon *inside* the header frame, to the left of the header text.
			// Reserve a fixed-width slot (based on the widest section icon) so all header texts align.
			// Also draw the icon starting at the built-in label start (after the triangle arrow).
			const auto* style = ImGuiMCP::ImGui::GetStyle();
			const float inner = style ? style->ItemInnerSpacing.x : 0.0f;
			const float padX = style ? style->FramePadding.x : 0.0f;
			const float fontSize = ImGuiMCP::ImGui::GetFontSize();

			auto sectionIconSlotW = []() -> float {
				static float cached = -1.0f;
				if (cached >= 0.0f) {
					return cached;
				}
				const std::string* icons[] = {
					&kIconCore,
					&kIconUIFeedback,
					&kIconLogging,
					&kIconControls,
					&kIconEquipMode,
					&kIconConsumable,
					&kIconPoison,
					&kIconSpellTome,
					&kIconWeaponRecharge,
					&kIconCombatEquip,
					&kIconAutoEquipBlocking,
					&kIconEquipGate,
					&kIconStatsDisplay,
					&kIconCorpseEquip,
					&kIconFixes
				};
				float maxW = 0.0f;
				FontAwesome::PushSolid();
				for (auto* s : icons) {
					ImGuiMCP::ImVec2 sz{};
					ImGuiMCP::ImGui::CalcTextSize(&sz, s->c_str(), nullptr, false, 0.0f);
					maxW = (std::max)(maxW, sz.x);
				}
				FontAwesome::Pop();
				cached = maxW;
				return cached;
			};

			ImGuiMCP::ImVec2 iconSize{};
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::CalcTextSize(&iconSize, icon.c_str(), nullptr, false, 0.0f);
			FontAwesome::Pop();

			ImGuiMCP::ImVec2 spaceSize{};
			ImGuiMCP::ImGui::CalcTextSize(&spaceSize, " ", nullptr, false, 0.0f);
			const float spaceW = (std::max)(spaceSize.x, 1.0f);

			// Nudge the icon block a bit to the right for visual balance.
			const float iconShift = inner * kCollapsingHeaderIconStyle.iconShiftMultiplier;

			const float slotW = sectionIconSlotW();
			const float iconTextGap = inner * kCollapsingHeaderIconStyle.iconTextGapMultiplier;
			const float reservedW = slotW + iconTextGap + iconShift;
			int spaces = static_cast<int>(std::ceil(reservedW / spaceW));
			if (spaces < 1) {
				spaces = 1;
			} else if (spaces > 10) {
				spaces = 10;
			}

			std::string headerLabel;
			headerLabel.assign(static_cast<std::size_t>(spaces), ' ');
			headerLabel += label;
			headerLabel += "##";
			headerLabel += id;

			const bool open = ImGuiMCP::ImGui::CollapsingHeader(headerLabel.c_str());

			auto* drawList = ImGuiMCP::ImGui::GetWindowDrawList();
			const auto textColor = ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImGuiCol_Text);
			ImGuiMCP::ImVec2 rectMin{};
			ImGuiMCP::ImVec2 rectMax{};
			ImGuiMCP::ImGui::GetItemRectMin(&rectMin);
			ImGuiMCP::ImGui::GetItemRectMax(&rectMax);
			const float h = rectMax.y - rectMin.y;

			// Label start inside a framed tree node is after the arrow triangle.
			const float xLabelStart = rectMin.x + padX + fontSize + inner;
			const float yIcon = rectMin.y + (h - iconSize.y) * 0.5f;
			float iconCenterOffset = 0.0f;
			if (slotW > iconSize.x) {
				iconCenterOffset = (slotW - iconSize.x) * 0.5f;
			}
			const float xIcon = xLabelStart + iconShift + iconCenterOffset;
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2(xIcon, yIcon), textColor, icon.c_str());
			FontAwesome::Pop();

			return open;
		}

		[[nodiscard]] float ButtonWidthWithTrailingIcon(const char* label, const std::string& icon)
		{
			const auto* style = ImGuiMCP::ImGui::GetStyle();
			const float padX = style ? (style->FramePadding.x * 2.0f) : 0.0f;
			const float inner = style ? style->ItemInnerSpacing.x : 0.0f;

			ImGuiMCP::ImVec2 labelSize{};
			ImGuiMCP::ImGui::CalcTextSize(&labelSize, label, nullptr, false, 0.0f);

			ImGuiMCP::ImVec2 iconSize{};
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::CalcTextSize(&iconSize, icon.c_str(), nullptr, false, 0.0f);
			FontAwesome::Pop();

			return labelSize.x + inner + iconSize.x + padX;
		}

		[[nodiscard]] bool ButtonWithTrailingIcon(const char* id, const char* label, const std::string& icon, ButtonTheme theme = ButtonTheme::kNeutral)
		{
			const auto* style = ImGuiMCP::ImGui::GetStyle();
			const float padX = style ? style->FramePadding.x : 0.0f;
			const float padY = style ? style->FramePadding.y : 0.0f;
			const float inner = style ? style->ItemInnerSpacing.x : 0.0f;

			ImGuiMCP::ImVec2 labelSize{};
			ImGuiMCP::ImGui::CalcTextSize(&labelSize, label, nullptr, false, 0.0f);

			ImGuiMCP::ImVec2 iconSize{};
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::CalcTextSize(&iconSize, icon.c_str(), nullptr, false, 0.0f);
			FontAwesome::Pop();

			const float w = labelSize.x + inner + iconSize.x + (padX * 2.0f);
			const float h = (std::max)(labelSize.y, iconSize.y) + (padY * 2.0f);

			std::string hiddenLabel = "##";
			hiddenLabel += id;
			auto themeScope = PushButtonTheme(theme);
			const bool pressed = ImGuiMCP::ImGui::Button(hiddenLabel.c_str(), ImGuiMCP::ImVec2(w, h));

			auto* drawList = ImGuiMCP::ImGui::GetWindowDrawList();
			const auto textColor = ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImGuiCol_Text);

			ImGuiMCP::ImVec2 rectMin{};
			ImGuiMCP::ImGui::GetItemRectMin(&rectMin);
			// Vertically center both label and icon.
			const float yLabel = rectMin.y + (h - labelSize.y) * 0.5f;
			const float xLabel = rectMin.x + padX;
			ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2(xLabel, yLabel), textColor, label);

			const float yIcon = rectMin.y + (h - iconSize.y) * 0.5f;
			const float xIcon = xLabel + labelSize.x + inner;
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2(xIcon, yIcon), textColor, icon.c_str());
			FontAwesome::Pop();

			return pressed;
		}

		[[nodiscard]] bool ButtonWithLeadingIcon(const char* id, const char* label, const std::string& icon, ButtonTheme theme = ButtonTheme::kNeutral, float widthOverridePx = 0.0f, bool rotateIcon180 = false, bool centerContent = false)
		{
			const auto* style = ImGuiMCP::ImGui::GetStyle();
			const float padX = style ? style->FramePadding.x : 0.0f;
			const float padY = style ? style->FramePadding.y : 0.0f;
			const float inner = style ? style->ItemInnerSpacing.x : 0.0f;
			const float gap = kLeadingIconButtonStyle.iconLabelGapPx;  // Extra gap between icon and label for visual separation.

			ImGuiMCP::ImVec2 labelSize{};
			ImGuiMCP::ImGui::CalcTextSize(&labelSize, label, nullptr, false, 0.0f);

			ImGuiMCP::ImVec2 iconSize{};
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::CalcTextSize(&iconSize, icon.c_str(), nullptr, false, 0.0f);
			FontAwesome::Pop();

			const float contentW = iconSize.x + gap + inner + labelSize.x;
			const float w = (widthOverridePx > 0.0f) ? widthOverridePx : (contentW + (padX * 2.0f));
			const float h = (std::max)(labelSize.y, iconSize.y) + (padY * 2.0f);

			std::string hiddenLabel = "##";
			hiddenLabel += id;
			auto themeScope = PushButtonTheme(theme);
			const bool pressed = ImGuiMCP::ImGui::Button(hiddenLabel.c_str(), ImGuiMCP::ImVec2(w, h));

			auto* drawList = ImGuiMCP::ImGui::GetWindowDrawList();
			const auto textColor = ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImGuiCol_Text);

			ImGuiMCP::ImVec2 rectMin{};
			ImGuiMCP::ImGui::GetItemRectMin(&rectMin);
			// Vertically center both icon and label.
			const float yIcon = rectMin.y + (h - iconSize.y) * 0.5f;
			const float xIcon = centerContent ? rectMin.x + (w - contentW) * 0.5f : rectMin.x + padX;
			FontAwesome::PushSolid();
			const int vtxStart = drawList->VtxBuffer.Size;
			ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2(xIcon, yIcon), textColor, icon.c_str());
			if (rotateIcon180) {
				const float pivotX = xIcon + iconSize.x * 0.5f;
				const float pivotY = yIcon + iconSize.y * 0.5f;
				for (int i = vtxStart; i < drawList->VtxBuffer.Size; ++i) {
					auto& v = drawList->VtxBuffer.Data[i];
					v.pos.x = pivotX + (pivotX - v.pos.x);
					v.pos.y = pivotY + (pivotY - v.pos.y);
				}
			}
			FontAwesome::Pop();

			const float yLabel = rectMin.y + (h - labelSize.y) * 0.5f;
			const float xLabel = xIcon + iconSize.x + gap;
			ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2(xLabel, yLabel), textColor, label);

			return pressed;
		}

		/// Full-width navigation button with a vertical accent bar and side-aligned icon.
		/// @param forward  true = accent bar on right + icon on right; false = accent bar on left + icon on left.
		[[nodiscard]] bool NavButton(const char* id, const char* label, const std::string& icon, bool forward, bool rotateIcon = false, const NavButtonStyle& ns = kNavStyle)
		{
			const auto* style = ImGuiMCP::ImGui::GetStyle();
			const float padX = style ? style->FramePadding.x : 0.0f;
			const float padY = style ? style->FramePadding.y : 0.0f;

			ImGuiMCP::ImVec2 labelSize{};
			ImGuiMCP::ImGui::CalcTextSize(&labelSize, label, nullptr, false, 0.0f);

			ImGuiMCP::ImVec2 iconSize{};
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::CalcTextSize(&iconSize, icon.c_str(), nullptr, false, 0.0f);
			FontAwesome::Pop();

			ImGuiMCP::ImVec2 avail{};
			ImGuiMCP::ImGui::GetContentRegionAvail(&avail);
			const float w = avail.x - ns.marginLeftPx - ns.marginRightPx;
			const float autoH = (std::max)(labelSize.y, iconSize.y) + (padY * 2.0f);
			const float h = (ns.heightPx > 0.0f) ? ns.heightPx : autoH;

			const float accentW = ns.accentWidthPx;
			const float rounding = ns.rounding;

			// Apply top/left margins via cursor offset.
			if (ns.marginTopPx > 0.0f) {
				ImGuiMCP::ImVec2 cursor{};
				ImGuiMCP::ImGui::GetCursorScreenPos(&cursor);
				ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(cursor.x, cursor.y + ns.marginTopPx));
			}

			if (ns.marginLeftPx > 0.0f) {
				ImGuiMCP::ImVec2 cursor{};
				ImGuiMCP::ImGui::GetCursorScreenPos(&cursor);
				ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(cursor.x + ns.marginLeftPx, cursor.y));
			}

			ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, ns.bgColor);
			ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, ns.bgColorHovered);
			ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, ns.bgColorActive);
			ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameRounding, rounding);
			ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, ns.borderSize);
			ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Border, ns.borderColor);

			std::string hiddenLabel = "##";
			hiddenLabel += id;
			const bool pressed = ImGuiMCP::ImGui::Button(hiddenLabel.c_str(), ImGuiMCP::ImVec2(w, h));
			const bool hovered = ImGuiMCP::ImGui::IsItemHovered();

			ImGuiMCP::ImGui::PopStyleColor(4);
			ImGuiMCP::ImGui::PopStyleVar(2);

			auto* drawList = ImGuiMCP::ImGui::GetWindowDrawList();

			ImGuiMCP::ImVec2 rectMin{}, rectMax{};
			ImGuiMCP::ImGui::GetItemRectMin(&rectMin);
			ImGuiMCP::ImGui::GetItemRectMax(&rectMax);

			// Hover-responsive text and accent bar colors.
			const auto textColor = ImGuiMCP::ImGui::GetColorU32(hovered ? ns.textColorHovered : ns.textColor);
			const auto accentColor = ImGuiMCP::ImGui::GetColorU32(hovered ? ns.accentColorHovered : ns.accentColor);
			const auto borderColor = ImGuiMCP::ImGui::GetColorU32(hovered ? ns.borderColorHovered : ns.borderColor);

			// Draw explicit border so hovered state can have its own border color.
			if (ns.borderSize > 0.0f) {
				ImGuiMCP::ImGui::ImDrawListManager::AddRect(
					drawList,
					rectMin,
					rectMax,
					borderColor,
					rounding,
					ImGuiMCP::ImDrawFlags_RoundCornersAll,
					ns.borderSize);
			}

			// Accent bar.
			if (forward) {
				ImGuiMCP::ImGui::ImDrawListManager::AddRectFilled(drawList,
					ImGuiMCP::ImVec2(rectMax.x - accentW, rectMin.y),
					ImGuiMCP::ImVec2(rectMax.x, rectMax.y),
					accentColor, rounding, ImGuiMCP::ImDrawFlags_RoundCornersRight);
			} else {
				ImGuiMCP::ImGui::ImDrawListManager::AddRectFilled(drawList,
					ImGuiMCP::ImVec2(rectMin.x, rectMin.y),
					ImGuiMCP::ImVec2(rectMin.x + accentW, rectMax.y),
					accentColor, rounding, ImGuiMCP::ImDrawFlags_RoundCornersLeft);
			}

			// Centered label.
			const float labelX = rectMin.x + (w - labelSize.x) * 0.5f;
			const float labelY = rectMin.y + (h - labelSize.y) * 0.5f;
			ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2(labelX, labelY), textColor, label);

			// Side-aligned icon.
			FontAwesome::PushSolid();
			const float iconY = rectMin.y + (h - iconSize.y) * 0.5f;
			const float iconX = forward
				? (rectMax.x - accentW - padX - iconSize.x)
				: (rectMin.x + accentW + padX);

			const int vtxStart = drawList->VtxBuffer.Size;
			ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2(iconX, iconY), textColor, icon.c_str());
			if (rotateIcon) {
				const float pivotX = iconX + iconSize.x * 0.5f;
				const float pivotY = iconY + iconSize.y * 0.5f;
				for (int i = vtxStart; i < drawList->VtxBuffer.Size; ++i) {
					auto& v = drawList->VtxBuffer.Data[i];
					v.pos.x = pivotX + (pivotX - v.pos.x);
					v.pos.y = pivotY + (pivotY - v.pos.y);
				}
			}
			FontAwesome::Pop();

			if (ns.marginBottomPx > 0.0f) {
				ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2(0.0f, ns.marginBottomPx));
			}

			return pressed;
		}

		/// Returns the effective height of a NavButton given a style (for reserving bottom space).
		[[nodiscard]] float NavButtonReservedHeight(const NavButtonStyle& ns = kNavStyle)
		{
			if (ns.heightPx > 0.0f) {
				return ns.marginTopPx + ns.heightPx + ns.marginBottomPx + ImGuiMCP::ImGui::GetStyle()->ItemSpacing.y;
			}
			return ns.marginTopPx + ImGuiMCP::ImGui::GetFrameHeight() + ns.marginBottomPx + ImGuiMCP::ImGui::GetStyle()->ItemSpacing.y;
		}

		[[nodiscard]] float DefaultIconButtonSize()
		{
			// Match ImGui's framed widget height (checkbox/combo/etc).
			return ImGuiMCP::ImGui::GetFrameHeight();
		}

		[[nodiscard]] bool DefaultIconButton(const char* id, ButtonTheme theme = ButtonTheme::kGhost)
		{
			const float size = DefaultIconButtonSize();
			std::string buttonId = "##";
			buttonId += id;
			auto themeScope = PushButtonTheme(theme);
			const bool pressed = ImGuiMCP::ImGui::Button(buttonId.c_str(), ImGuiMCP::ImVec2(size, size));

			auto* drawList = ImGuiMCP::ImGui::GetWindowDrawList();
			const auto textColor = ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImGuiCol_Text);
			ImGuiMCP::ImVec2 rectMin{};
			ImGuiMCP::ImGui::GetItemRectMin(&rectMin);

			ImGuiMCP::ImVec2 iconSize{};
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::CalcTextSize(&iconSize, kDefaultIcon.c_str(), nullptr, false, 0.0f);
			const float x = rectMin.x + (size - iconSize.x) * 0.5f;
			const float y = rectMin.y + (size - iconSize.y) * 0.5f;
			ImGuiMCP::ImGui::ImDrawListManager::AddText(drawList, ImGuiMCP::ImVec2(x, y), textColor, kDefaultIcon.c_str());
			FontAwesome::Pop();

			return pressed;
		}

		void RightAlignOnLine(float totalWidth)
		{
			ImGuiMCP::ImVec2 contentMax{};
			ImGuiMCP::ImGui::GetWindowContentRegionMax(&contentMax);
			const float currentX = ImGuiMCP::ImGui::GetCursorPosX();
			const float targetX = contentMax.x - totalWidth;
			if (targetX > currentX) {
				ImGuiMCP::ImGui::SetCursorPosX(targetX);
			}
		}

		void DefaultIconTooltip(std::string_view defaultValueText);

		void TooltipOnHoverText(const char* tooltip)
		{
			if (ImGuiMCP::ImGui::IsItemHovered()) {
				ImGuiMCP::ImGui::BeginTooltip();
				ImGuiMCP::ImGui::PushTextWrapPos(ImGuiMCP::ImGui::GetFontSize() * kTooltipStyle.wrapWidthMultiplier);
				ImGuiMCP::ImGui::TextUnformatted(tooltip);
				ImGuiMCP::ImGui::PopTextWrapPos();
				ImGuiMCP::ImGui::EndTooltip();
			}
		}

		void TooltipOnHoverLocKey(const char* tooltipKey)
		{
			TooltipOnHoverText(Localization::CStr(tooltipKey));
		}


		void DefaultIconTooltip(std::string_view defaultValueText)
		{
			if (defaultValueText.empty()) {
				TooltipOnHoverLocKey("ui.tooltip.default_action");
				return;
			}

			std::string tip = Localization::Get("ui.tooltip.default_action");
			tip += "\n\n";
			tip += Localization::Get("ui.tooltip.default_value_prefix");
			tip += " ";
			tip += defaultValueText;
			TooltipOnHoverText(tip.c_str());
		}

		[[nodiscard]] const char* EnabledDisabledText(bool enabled)
		{
			return enabled ? Localization::CStr("ui.value.enabled") : Localization::CStr("ui.value.disabled");
		}

		void HelpMarker(const char* tooltip);

		// Measures the pixel width of a single FA Solid icon glyph at the current window scale.
		[[nodiscard]] static float FaIconWidth(const std::string& a_icon)
		{
			ImGuiMCP::ImVec2 sz{};
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::CalcTextSize(&sz, a_icon.c_str(), nullptr, false, 0.0f);
			FontAwesome::Pop();
			return sz.x;
		}

		// Returns the wider of two FA Solid icon glyphs (used for fixed-width column sizing).
		[[nodiscard]] static float FaMaxIconWidth(const std::string& a_iconA, const std::string& a_iconB)
		{
			return (std::max)(FaIconWidth(a_iconA), FaIconWidth(a_iconB));
		}

		[[nodiscard]] float HelpIconWidth()               { return FaIconWidth(kHelpIcon); }
		[[nodiscard]] float DismissedToggleIconWidth()    { return FaMaxIconWidth(kIconActiveOnly,    kIconAllFollowers); }
		[[nodiscard]] float SummonToggleIconWidth()       { return FaMaxIconWidth(kIconSummonsShow,   kIconSummonsHide); }
		[[nodiscard]] float InclusionToggleIconWidth()    { return FaMaxIconWidth(kIconInclusionShow, kIconInclusionHide); }
		[[nodiscard]] float CrossClearIconWidth()         { return FaMaxIconWidth(kIconCrossClear,    kIconCrossClearClose); }
		[[nodiscard]] float ActorExcludeIconWidth()       { return FaIconWidth(kIconActorExclude); }
		[[nodiscard]] float ActorRemoveIconWidth()        { return FaIconWidth(kIconActorRemove); }
		[[nodiscard]] float ShowExcludedActorsIconWidth() { return FaIconWidth(kIconExcludedShow); }

		[[nodiscard]] bool RightAlignedHelpAndDefault(const char* helpTooltip, const char* defaultButtonId, ButtonTheme theme = ButtonTheme::kGhost, std::string_view defaultValueText = {})
		{
			// Draw the help icon immediately to the left of the default button,
			// and keep the pair right-aligned.
			const auto* imguiStyle = ImGuiMCP::ImGui::GetStyle();
			const float gap = (imguiStyle ? imguiStyle->ItemInnerSpacing.x : 0.0f) + kSpacingStyle.helpDefaultExtraGapPx;
			const float totalW = HelpIconWidth() + gap + DefaultIconButtonSize();
			RightAlignOnLine(totalW);
			ImGuiMCP::ImGui::AlignTextToFramePadding();
			HelpMarker(helpTooltip);
			ImGuiMCP::ImGui::SameLine(0.0f, gap);
			const bool pressed = DefaultIconButton(defaultButtonId, theme);
			DefaultIconTooltip(defaultValueText);
			return pressed;
		}

		void HelpMarker(const char* tooltip)
		{
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::TextDisabled("%s", kHelpIcon.c_str());
			FontAwesome::Pop();
			TooltipOnHoverText(tooltip);
		}

		//=============================================================================
		// [SECTION 4] String + State Helpers
		//=============================================================================

		[[nodiscard]] std::string TrimCopy(std::string s)
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

		void CopyToBuffer(std::array<char, 96>& dst, const std::string& src)
		{
			dst.fill('\0');
			const auto n = std::min<std::size_t>(dst.size() - 1, src.size());
			std::copy_n(src.data(), n, dst.data());
		}

		void MarkDirty()
		{
			g_ui.dirty = true;
		}

		//=============================================================================
		// [SECTION 5] Settings State Management
		//   UIState lifecycle: Rebuild / Commit / Apply / Sync / Build
		//   EnsureInitialized / BeginSettingsPageChrome / EndSettingsPageChrome
		//=============================================================================

		void RebuildDerivedFromDraft()
		{
			// Keep all derived UI state (indices/buffers) sourced from draft.
			g_ui.logLevelIndex = LogLevelIndexFromString(g_ui.draft.logging.logLevel);
			g_ui.modKeyDik = static_cast<int>(g_ui.draft.keyboardControls.modKeyDik);
			g_ui.skyuiEquipModeKey = static_cast<int>(g_ui.draft.keyboardControls.skyuiEquipModeKey);
			g_ui.rechargeKeyDik = static_cast<int>(g_ui.draft.weaponEnchantmentRecharge.rechargeKeyDik);
			g_ui.gamepadModKey = static_cast<int>(g_ui.draft.gamepadControls.gamepadModKey);
			g_ui.gamepadRightHandKey = static_cast<int>(g_ui.draft.gamepadControls.gamepadRightHandKey);
			g_ui.gamepadLeftHandKey = static_cast<int>(g_ui.draft.gamepadControls.gamepadLeftHandKey);
			g_ui.quickTradeKeyDik = static_cast<int>(g_ui.draft.keyboardControls.quickTradeKeyDik);
			g_ui.quickTradeGamepadKey = static_cast<int>(g_ui.draft.gamepadControls.quickTradeGamepadKey);
			g_ui.gamepadRechargeKey = static_cast<int>(g_ui.draft.weaponEnchantmentRecharge.gamepadRechargeKey);
			CopyToBuffer(g_ui.indicatorText, g_ui.draft.buttonIndicators.modKeyIndicatorText);
			CopyToBuffer(g_ui.summonIndicatorText, g_ui.draft.buttonIndicators.summonIndicatorText);
			CopyToBuffer(g_ui.inclusionIndicatorText, g_ui.draft.buttonIndicators.inclusionIndicatorText);
			CopyToBuffer(g_ui.equipModeTextOverride, g_ui.draft.buttonIndicators.equipModeTextOverride);
			CopyToBuffer(g_ui.corpseIndicatorText, g_ui.draft.corpseEquipMode.indicatorText);
		}

		[[nodiscard]] static std::uint32_t ValidateGamepadKey(int val, std::uint32_t fallback) noexcept
		{
			constexpr int kBase = static_cast<int>(Controls::kGamepadOffset);
			return (val >= kBase && val < kBase + 16)
				? static_cast<std::uint32_t>(val)
				: fallback;
		}

		void CommitDerivedToDraft()
		{
			// Mirror derived UI state back into draft so cross-tab operations (like RESET ALL)
			// preserve unsaved edits without needing per-control glue.
			g_ui.draft.logging.logLevel = LogLevelStringFromIndex(g_ui.logLevelIndex);
			g_ui.draft.keyboardControls.modKeyDik = g_ui.modKeyDik <= 0 ? 0u : static_cast<std::uint32_t>(g_ui.modKeyDik);
			g_ui.draft.keyboardControls.skyuiEquipModeKey = g_ui.skyuiEquipModeKey <= 0 ? 0u : static_cast<std::uint32_t>(g_ui.skyuiEquipModeKey);
			g_ui.draft.weaponEnchantmentRecharge.rechargeKeyDik =
				g_ui.rechargeKeyDik <= 0 ? Defaults().weaponEnchantmentRecharge.rechargeKeyDik : static_cast<std::uint32_t>(g_ui.rechargeKeyDik);
			g_ui.draft.gamepadControls.gamepadModKey =
				ValidateGamepadKey(g_ui.gamepadModKey, Defaults().gamepadControls.gamepadModKey);
			g_ui.draft.gamepadControls.gamepadRightHandKey =
				ValidateGamepadKey(g_ui.gamepadRightHandKey, Defaults().gamepadControls.gamepadRightHandKey);
			g_ui.draft.gamepadControls.gamepadLeftHandKey =
				ValidateGamepadKey(g_ui.gamepadLeftHandKey, Defaults().gamepadControls.gamepadLeftHandKey);
			g_ui.draft.keyboardControls.quickTradeKeyDik = g_ui.quickTradeKeyDik <= 0 ? 0u : static_cast<std::uint32_t>(g_ui.quickTradeKeyDik);
			g_ui.draft.gamepadControls.quickTradeGamepadKey =
				ValidateGamepadKey(g_ui.quickTradeGamepadKey, 0u);
			g_ui.draft.weaponEnchantmentRecharge.gamepadRechargeKey =
				ValidateGamepadKey(g_ui.gamepadRechargeKey, Defaults().weaponEnchantmentRecharge.gamepadRechargeKey);
			g_ui.draft.buttonIndicators.modKeyIndicatorText = TrimCopy(g_ui.indicatorText.data());
			g_ui.draft.buttonIndicators.summonIndicatorText = TrimCopy(g_ui.summonIndicatorText.data());
			g_ui.draft.buttonIndicators.inclusionIndicatorText = TrimCopy(g_ui.inclusionIndicatorText.data());
			g_ui.draft.buttonIndicators.equipModeTextOverride = TrimCopy(g_ui.equipModeTextOverride.data());
			g_ui.draft.corpseEquipMode.indicatorText = TrimCopy(g_ui.corpseIndicatorText.data());
		}

		void ApplySettingsToUIState(const PluginSettings::Settings& settings)
		{
			g_ui.draft = settings;
			RebuildDerivedFromDraft();
			g_ui.initialized = true;
		}

		void SyncFromConfig()
		{
			ApplySettingsToUIState(PluginSettings::Get());
			g_ui.dirty = false;
		}

		[[nodiscard]] PluginSettings::Settings BuildSettingsFromUI()
		{
			return g_ui.draft;
		}

		void RecomputeDirtyFromUI()
		{
			if (!g_ui.initialized) {
				return;
			}
			const auto current = BuildSettingsFromUI();
			const auto& saved = PluginSettings::Get();
			g_ui.dirty = !(current == saved);
		}

		template <class ResetFn>
		void RenderToolbar(const char* reloadButtonId, const char* saveButtonId, const char* resetAllButtonId, bool canResetAll, ResetFn&& resetFn);

		void EnsureInitialized()
		{
			if (!g_ui.initialized) {
				SyncFromConfig();
			}
		}

		template <class ResetFn>
		void BeginSettingsPageChrome(const char* scrollRegionId, const char* reloadButtonId, const char* saveButtonId, const char* resetAllButtonId, bool canResetAll, ResetFn&& resetFn, float reservedBottomPx = 0.0f)
		{
			EnsureInitialized();

			RenderTitleLine(Localization::CStr("ui.title"));
			ImGuiMCP::ImGui::Separator();
			ToolbarGap();
			RenderToolbar(reloadButtonId, saveButtonId, resetAllButtonId, canResetAll, std::forward<ResetFn>(resetFn));
			SettingsSectionDivider();

			// Keep the title + toolbar fixed, and make the rest scrollable.
			// A negative height reserves space at the bottom for fixed widgets.
			const float childH = (reservedBottomPx > 0.0f) ? -reservedBottomPx : 0.0f;
			ImGuiMCP::ImGui::BeginChild(
				scrollRegionId,
				ImGuiMCP::ImVec2(0.0f, childH),
				ImGuiMCP::ImGuiChildFlags_None,
				ImGuiMCP::ImGuiWindowFlags_AlwaysVerticalScrollbar);
		}

		void EndSettingsPageChrome()
		{
			ImGuiMCP::ImGui::EndChild();
			CommitDerivedToDraft();
			RecomputeDirtyFromUI();
		}

		void ApplyRuntime(const PluginSettings::Settings& settings);

		template <class DefaultFn>
		void HelpAndDefaultOnLine(const char* helpKey, const char* defaultButtonId, DefaultFn&& defaultFn)
		{
			HelpAndDefaultOnLine(helpKey, defaultButtonId, std::string_view{}, std::forward<DefaultFn>(defaultFn));
		}

		template <class DefaultFn>
		void HelpAndDefaultOnLine(const char* helpKey, const char* defaultButtonId, std::string_view defaultValueText, DefaultFn&& defaultFn)
		{
			ImGuiMCP::ImGui::SameLine();
			if (RightAlignedHelpAndDefault(Localization::CStr(helpKey), defaultButtonId, ButtonTheme::kGhost, defaultValueText)) {
				defaultFn();
				MarkDirty();
			}
		}

		struct SegmentedOpt
		{
			const char* labelKey{};
			const char* idSuffix{};
		};

		// SegmentedStyle and kSegmentedDefaultStyle are defined at the top of the file
		// in the centralized style knobs section.

		template <std::size_t N, class OnSelectFn, class DefaultFn>
		void SegmentedRow(
			const SegmentedOpt (&opts)[N],
			int selectedIndex,
			const char* helpKey,
			const char* defaultButtonId,
			OnSelectFn&& onSelect,
			DefaultFn&& onDefault,
			const char* trailingLabelKey = nullptr,
			ConnectorBracket* connector = nullptr,
			SegmentedStyle segStyle = kSegmentedDefaultStyle,
			std::string_view defaultValueText = {})
		{
			static_assert(N >= 2, "SegmentedRow requires at least 2 options");

			const auto* imguiStyle = ImGuiMCP::ImGui::GetStyle();
			const float padX = imguiStyle ? imguiStyle->FramePadding.x : 0.0f;
			const float spacingX = imguiStyle ? imguiStyle->ItemSpacing.x : 0.0f;

			auto calcLabelMinWidth = [&](const char* text) {
				ImGuiMCP::ImVec2 textSize{};
				ImGuiMCP::ImGui::CalcTextSize(&textSize, text, nullptr, false, 0.0f);
				return textSize.x + (padX * 2.0f);
			};
			float labelMinW = 0.0f;
			for (std::size_t i = 0; i < N; ++i) {
				labelMinW = (std::max)(labelMinW, calcLabelMinWidth(Localization::CStr(opts[i].labelKey)));
			}

			ImGuiMCP::ImVec2 avail{};
			ImGuiMCP::ImGui::GetContentRegionAvail(&avail);

			ImGuiMCP::ImVec2 helpSize{};
			FontAwesome::PushSolid();
			ImGuiMCP::ImGui::CalcTextSize(&helpSize, kHelpIcon.c_str(), nullptr, false, 0.0f);
			FontAwesome::Pop();
			const float defaultW = DefaultIconButtonSize();

			// Reserve space so Help + Default can stay on the same row.
			// Help and Default are rendered as a right-aligned adjacent pair.
			const float reservedRightW = spacingX + helpSize.x + defaultW;
			float maxControlW = avail.x - reservedRightW;
			maxControlW = (std::max)(maxControlW, 1.0f);

			// Keep segmented controls aligned with other "narrow" widgets.
			// This ensures indented/sub-element segmented rows end at the same X as non-sub rows.
			float usableW = CalcSegmentedControlWidthPx();
			usableW = (std::min)(usableW, maxControlW);
			usableW = (std::max)(usableW, 1.0f);
			const float totalGapW = segStyle.gapPx * static_cast<float>(N - 1);
			float idealW = (usableW - totalGapW) / static_cast<float>(N);
			idealW = (std::max)(idealW, 1.0f);
			const float minWantedW = (std::max)(segStyle.minWidthPx, labelMinW);
			const float requiredForMinWanted = (minWantedW * static_cast<float>(N)) + totalGapW;

			float buttonW = idealW;
			if (requiredForMinWanted <= usableW) {
				buttonW = (std::max)(buttonW, minWantedW);
			}
			buttonW = (std::min)(buttonW, segStyle.maxWidthPx);
			buttonW = (std::max)(buttonW, 1.0f);
			const float totalButtonsW = (buttonW * static_cast<float>(N)) + totalGapW;
			// Right-align the segmented group within the "usable" narrow width so it ends at the
			// same X position regardless of indent and regardless of max-width clamping.
			const float startX = ImGuiMCP::ImGui::GetCursorPosX();
			if (totalButtonsW > 0.0f && totalButtonsW < usableW) {
				ImGuiMCP::ImGui::SetCursorPosX(startX + (usableW - totalButtonsW));
			}
			const auto buttonSize = ImGuiMCP::ImVec2(buttonW, 0.0f);

			std::array<std::string, N> buttonLabels{};
			for (std::size_t i = 0; i < N; ++i) {
				buttonLabels[i] = Label(opts[i].labelKey, opts[i].idSuffix);
			}

			for (std::size_t i = 0; i < N; ++i) {
				const int idx = static_cast<int>(i);
				const bool selected = (i == selectedIndex);
				auto themeScope = PushButtonTheme(selected ? ButtonTheme::kEmphasis : ButtonTheme::kNeutral);
				if (ImGuiMCP::ImGui::Button(buttonLabels[i].c_str(), buttonSize)) {
					onSelect(idx);
					MarkDirty();
				}
				if (i == 0u && connector) {
					// Sub-item connectors should join to the primary control, not to help/default.
					// For segmented controls, that means the first button.
					connector->AddJoinFromLastItem();
				}
				if (i + 1u != N) {
					ImGuiMCP::ImGui::SameLine(0.0f, segStyle.gapPx);
				}
			}

			if (trailingLabelKey) {
				ImGuiMCP::ImGui::SameLine(0.0f, spacingX);
				ImGuiMCP::ImGui::AlignTextToFramePadding();
				ImGuiMCP::ImGui::TextUnformatted(Localization::CStr(trailingLabelKey));
			}

			HelpAndDefaultOnLine(helpKey, defaultButtonId, defaultValueText, [&]() { onDefault(); });
		}

		//=============================================================================
		// [SECTION 6] Settings Control Primitives
		//   CheckboxRow / SegmentedRow / SliderRow
		//   HelpAndDefaultOnLine / RenderCheckboxGroup / RenderDependentBlock (templates)
		//=============================================================================

		template <class AfterCheckboxFn>
		bool CheckboxRow(const char* labelKey, const char* idSuffix, bool& value, const char* helpKey, const char* defaultButtonId, bool defaultValue, AfterCheckboxFn&& afterCheckbox)
		{
			const auto label = Label(labelKey, idSuffix);
			bool changed = false;
			if (ImGuiMCP::ImGui::Checkbox(label.c_str(), &value)) {
				changed = true;
			}
			afterCheckbox();
			HelpAndDefaultOnLine(helpKey, defaultButtonId, EnabledDisabledText(defaultValue), [&]() {
				value = defaultValue;
				changed = true;
			});
			if (changed) {
				MarkDirty();
			}
			return changed;
		}

		bool CheckboxRow(const char* labelKey, const char* idSuffix, bool& value, const char* helpKey, const char* defaultButtonId, bool defaultValue)
		{
			return CheckboxRow(labelKey, idSuffix, value, helpKey, defaultButtonId, defaultValue, []() {});
		}

		struct CheckboxOpt
		{
			bool* value{};
			const char* labelKey{};
			const char* idSuffix{};
			const char* helpKey{};
			const char* defaultButtonId{};
			bool defaultValue{};
		};

		template <std::size_t N>
		void RenderCheckboxGroup(const CheckboxOpt (&opts)[N], ConnectorBracket* connector = nullptr)
		{
			for (const auto& opt : opts) {
				if (connector) {
					CheckboxRow(opt.labelKey, opt.idSuffix, *opt.value, opt.helpKey, opt.defaultButtonId, opt.defaultValue, [&]() { connector->AddJoinFromLastItem(); });
				} else {
					CheckboxRow(opt.labelKey, opt.idSuffix, *opt.value, opt.helpKey, opt.defaultButtonId, opt.defaultValue);
				}
			}
		}

		bool FloatSliderRow(const char* labelKey, const char* idSuffix, float& value, float vMin, float vMax, const char* format, const char* helpKey, const char* defaultButtonId, float defaultValue, ConnectorBracket* connector = nullptr)
		{
			SetSliderControlWidth();
			const auto label = Label(labelKey, idSuffix);
			bool changed = false;
			if (ImGuiMCP::ImGui::SliderFloat(label.c_str(), &value, vMin, vMax, format, ImGuiMCP::ImGuiSliderFlags_AlwaysClamp)) {
				changed = true;
			}
			if (connector) {
				connector->AddJoinFromLastItem();
			}
			HelpAndDefaultOnLine(helpKey, defaultButtonId, std::format("{:.1f}", defaultValue), [&]() {
				value = defaultValue;
				changed = true;
			});
			if (changed) {
				MarkDirty();
			}
			return changed;
		}

		bool DoubleSliderRow(const char* labelKey, const char* idSuffix, double& value, int vMin, int vMax, const char* helpKey, const char* defaultButtonId, double defaultValue, ConnectorBracket* connector = nullptr)
		{
			SetSliderControlWidth();
			const auto label = Label(labelKey, idSuffix);
			bool changed = false;
			int intVal = static_cast<int>(std::round(value));
			if (ImGuiMCP::ImGui::SliderInt(label.c_str(), &intVal, vMin, vMax, "%d", ImGuiMCP::ImGuiSliderFlags_AlwaysClamp)) {
				value = static_cast<double>(intVal);
				changed = true;
			}
			if (connector) {
				connector->AddJoinFromLastItem();
			}
			HelpAndDefaultOnLine(helpKey, defaultButtonId, std::format("{}", static_cast<int>(std::round(defaultValue))), [&]() {
				value = defaultValue;
				changed = true;
			});
			if (changed) {
				MarkDirty();
			}
			return changed;
		}

		template <class DrawChildrenFn>
		void RenderDependentBlock(ConnectorBracket& connector, bool enabled, DrawChildrenFn&& drawChildren)
		{
			if (!enabled) {
				return;
			}
			SettingsItemGap();
			ImGuiMCP::ImGui::Indent();
			drawChildren();
			ImGuiMCP::ImGui::Unindent();
			connector.Draw(ImGuiMCP::ImGuiCol_Border);
		}

		template <class ResetFn>
		void RenderToolbar(const char* reloadButtonId, const char* saveButtonId, const char* resetAllButtonId, bool canResetAll, ResetFn&& resetFn)
		{
			// Push toolbar frame style so buttons get explicit rounding/border/padding.
			ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameRounding, kToolbarStyle.rounding);
			ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, kToolbarStyle.borderSize);
			ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding, ImGuiMCP::ImVec2(kToolbarStyle.padX, kToolbarStyle.padY));

			const auto reloadLabel = Localization::Get("ui.toolbar.revert");
			const auto saveLabel = Localization::Get("ui.toolbar.save");
			const auto resetAllLabel = Localization::Get("ui.toolbar.reset");
			const bool runtimeModEnabled = PluginSettings::Get().core.enableMod;

			// Left: RELOAD+SAVE. Right: RESET ALL. Status indicator appears after SAVE.
			const auto* style = ImGuiMCP::ImGui::GetStyle();
			const float spacing = style ? style->ItemSpacing.x : 0.0f;

			ImGuiMCP::ImVec2 contentMax{};
			ImGuiMCP::ImGui::GetWindowContentRegionMax(&contentMax);

			ImGuiMCP::ImGui::BeginDisabled(!g_ui.dirty);
			if (ButtonWithTrailingIcon(reloadButtonId, reloadLabel.c_str(), kReloadIcon)) {
				PluginSettings::Load();
				SyncFromConfig();
			}
			ImGuiMCP::ImGui::EndDisabled();
			TooltipOnHoverLocKey("ui.tooltip.revert");

			ImGuiMCP::ImGui::SameLine(0.0f, spacing);
			ImGuiMCP::ImGui::BeginDisabled(!g_ui.dirty);
			if (ButtonWithTrailingIcon(saveButtonId, saveLabel.c_str(), kSaveIcon, g_ui.dirty ? ButtonTheme::kEmphasis : ButtonTheme::kNeutral)) {
				CommitDerivedToDraft();
				auto s = BuildSettingsFromUI();
				if (PluginSettings::Save(s)) {
					ApplyRuntime(s);
					g_ui.dirty = false;
				}
			}
			ImGuiMCP::ImGui::EndDisabled();

			// Status indicator to the right of SAVE.
			if (!runtimeModEnabled) {
				ImGuiMCP::ImGui::SameLine(0.0f, spacing);
				ImGuiMCP::ImGui::AlignTextToFramePadding();
				FontAwesome::PushSolid();
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kToolbarStyle.disabledModColor);
				ImGuiMCP::ImGui::Text("%s", kDirtyIcon.c_str());
				ImGuiMCP::ImGui::PopStyleColor();
				FontAwesome::Pop();
				TooltipOnHoverLocKey("ui.tooltip.mod_disabled");
			} else if (g_ui.dirty) {
				ImGuiMCP::ImGui::SameLine(0.0f, spacing);
				ImGuiMCP::ImGui::AlignTextToFramePadding();
				FontAwesome::PushSolid();
				ImGuiMCP::ImGui::TextDisabled("%s", kDirtyIcon.c_str());
				FontAwesome::Pop();
				TooltipOnHoverLocKey("ui.tooltip.unsaved");
			}

			// Right group: RESET ALL right-aligned (same row when possible).
			const float resetW = ButtonWidthWithTrailingIcon(resetAllLabel.c_str(), kResetAllIcon);
			ImGuiMCP::ImGui::SameLine(0.0f, spacing);
			const float currentX = ImGuiMCP::ImGui::GetCursorPosX();
			const float startX = contentMax.x - resetW;
			if (startX > currentX) {
				ImGuiMCP::ImGui::SetCursorPosX(startX);
			} else {
				ImGuiMCP::ImGui::NewLine();
				const float currentX2 = ImGuiMCP::ImGui::GetCursorPosX();
				const float startX2 = contentMax.x - resetW;
				if (startX2 > currentX2) {
					ImGuiMCP::ImGui::SetCursorPosX(startX2);
				}
			}
			ImGuiMCP::ImGui::BeginDisabled(!canResetAll);
			if (ButtonWithTrailingIcon(resetAllButtonId, resetAllLabel.c_str(), kResetAllIcon)) {
				resetFn();
				MarkDirty();
			}
			ImGuiMCP::ImGui::EndDisabled();
			TooltipOnHoverLocKey("ui.tooltip.reset");

			ImGuiMCP::ImGui::PopStyleVar(3);
		}

		template <class OnDefaultFn>
		static void TextInputRow(
			const char* a_labelKey,
			const char* a_idSuffix,
			const char* a_hintKey,
			char* a_buf,
			std::size_t a_bufSize,
			ConnectorBracket& a_connector,
			const char* a_helpKey,
			const char* a_defaultButtonId,
			OnDefaultFn&& a_onDefault)
		{
			const auto* textCol = ImGuiMCP::ImGui::GetStyleColorVec4(ImGuiMCP::ImGuiCol_Text);
			const ImGuiMCP::ImVec4 hintCol{ textCol->x, textCol->y, textCol->z, 0.5f };

			SetNarrowControlWidth();
			const auto label = Label(a_labelKey, a_idSuffix);
			ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_TextDisabled, hintCol);
			if (ImGuiMCP::ImGui::InputTextWithHint(label.c_str(), Localization::CStr(a_hintKey), a_buf, a_bufSize)) {
				MarkDirty();
			}
			ImGuiMCP::ImGui::PopStyleColor();
			a_connector.AddJoinFromLastItem();
			HelpAndDefaultOnLine(a_helpKey, a_defaultButtonId, Localization::CStr(a_hintKey), std::forward<OnDefaultFn>(a_onDefault));
		}

		//=============================================================================
		// [SECTION 7] Toolbar + ApplyRuntime
		//=============================================================================

		void ApplyRuntime(const PluginSettings::Settings& settings)
		{
			// Update in-memory config first (features read from PluginSettings::Get()).
			PluginSettings::Set(settings);

			// Apply log level immediately.
			Logging::SetLevel(settings.logging.logLevel);

			// Global master enable: when off, keep System/Settings UI available but disable gameplay hooks.
			if (!settings.core.enableMod) {
				FeatureRegistry::InstallAllFeatures();
				return;
			}

			// Apply settings to features that cache values.
			// Keep this in the same order as the settings menu sections.
			if (settings.equipModeConsumableMode.enableConsumableMode) {
				EquipMode::Modes::ConsumableMode::Install();
				EquipMode::Modes::ConsumableMode::SetPotionsEnabled(settings.equipModeConsumableMode.enablePotions);
				EquipMode::Modes::ConsumableMode::SetFoodsEnabled(settings.equipModeConsumableMode.enableFoods);
				EquipMode::Modes::ConsumableMode::SetDrinksEnabled(settings.equipModeConsumableMode.enableDrinks);
				EquipMode::Modes::ConsumableMode::SetIngredientsEnabled(settings.equipModeConsumableMode.enableIngredients);
				EquipMode::Modes::ConsumableMode::SetIngredientDiscoveryForPlayerEnabled(settings.equipModeConsumableMode.enableIngredientDiscoveryForPlayer);
			} else {
				EquipMode::Modes::ConsumableMode::Uninstall();
			}

			if (settings.equipModePoisonMode.mode != PluginSettings::PoisonMode::kDisable) {
				EquipMode::Modes::PoisonMode::Install();
				EquipMode::Modes::PoisonMode::SetMode(settings.equipModePoisonMode.mode == PluginSettings::PoisonMode::kConsume ? EquipMode::Modes::PoisonMode::Mode::kConsume : EquipMode::Modes::PoisonMode::Mode::kApplyToWeapon);
			} else {
				EquipMode::Modes::PoisonMode::Uninstall();
			}

			if (settings.equipModeSpellTomeMode.enableSpellTomeMode) {
				EquipMode::Modes::SpellTomeMode::Install();
			} else {
				EquipMode::Modes::SpellTomeMode::Uninstall();
			}

			if (settings.engineFixes.enableStaleWeightCacheFix) {
				EquipMode::Fixes::StaleWeightCacheFix::Install();
			} else {
				EquipMode::Fixes::StaleWeightCacheFix::Uninstall();
			}

			if (settings.skyUIFixes.enableQuantityMenuBlocker) {
				EquipMode::Fixes::QuantityMenuBlocker::Install();
			} else {
				EquipMode::Fixes::QuantityMenuBlocker::Uninstall();
			}

			if (settings.skyUIFixes.enableZeroWeightFix) {
				EquipMode::Fixes::ZeroWeightTakeAllFix::Install();
			} else {
				EquipMode::Fixes::ZeroWeightTakeAllFix::Uninstall();
			}

			// Apply indicator enable state + pick up updated text overrides immediately.
			if (settings.buttonIndicators.enableModKeyIndicator) {
				FollowerEquipModeUIIndicator::Uninstall();
				FollowerEquipModeUIIndicator::Install();
			} else {
				FollowerEquipModeUIIndicator::Uninstall();
			}

			EquipGate::SetEquipBlockingEnabled(settings.equipGate.enableEquipBlocking);
			EquipGate::SetUnequipBlockingEnabled(settings.equipGate.enableUnequipBlocking);
			EquipGate::SetNeverBlockTorchEquipEnabled(settings.equipGate.enableNeverBlockTorchEquip);

			FollowerEquipModeUIIndicator::RefreshOpenMenu();
			WeaponEnchantmentRechargeUIIndicator::RefreshOpenMenu();

			// Re-run the global registry install to (re)apply config-gated features.
			// This is safe: each feature Install() is idempotent.
			FeatureRegistry::InstallAllFeatures();
		}

		//=============================================================================
		// [SECTION 8] Reset Helpers
		//=============================================================================

		void ResetSystemToDefaultsUI()
		{
			const auto& d = Defaults();
			g_ui.draft.core.enableMod = d.core.enableMod;
			g_ui.draft.uiFeedback = d.uiFeedback;
			g_ui.draft.logging = d.logging;
			RebuildDerivedFromDraft();
		}

		void ResetEquipModeToDefaultsUI()
		{
			const auto& d = Defaults();
			g_ui.draft.equipModeItems = d.equipModeItems;
			g_ui.draft.equipModeConsumableMode = d.equipModeConsumableMode;
			g_ui.draft.equipModePoisonMode = d.equipModePoisonMode;
			g_ui.draft.equipModeSpellTomeMode = d.equipModeSpellTomeMode;
			RebuildDerivedFromDraft();
		}

		void ResetEquipStabilityToDefaultsUI()
		{
			const auto& d = Defaults();
			g_ui.draft.outfitSync = d.outfitSync;
			g_ui.draft.hiddenItems = d.hiddenItems;
			g_ui.draft.itemInjectionBlocking = d.itemInjectionBlocking;
			g_ui.draft.lootBlocking = d.lootBlocking;
			g_ui.draft.autoEquipBlocking = d.autoEquipBlocking;
			g_ui.draft.equipGate = d.equipGate;
			RebuildDerivedFromDraft();
		}

		void ResetExtraFeaturesToDefaultsUI()
		{
			const auto& d = Defaults();
			g_ui.draft.weaponEnchantmentRecharge = d.weaponEnchantmentRecharge;
			g_ui.draft.statsDisplay = d.statsDisplay;
			g_ui.draft.corpseEquipMode = d.corpseEquipMode;
			RebuildDerivedFromDraft();
		}

		void ResetScopeAndAccessToDefaultsUI()
		{
			const auto& d = Defaults();
			g_ui.draft.actorScope.affectFormerFollowers = d.actorScope.affectFormerFollowers;
			g_ui.draft.actorScope.includeInclusionActors = d.actorScope.includeInclusionActors;
			g_ui.draft.actorScope.includePlayerSummons = d.actorScope.includePlayerSummons;
			g_ui.draft.quickTrade.enableQuickTrade = d.quickTrade.enableQuickTrade;
			g_ui.draft.quickTrade.enableQuickTradeForFollowers = d.quickTrade.enableQuickTradeForFollowers;
			g_ui.draft.quickTrade.enableQuickTradeForFormerFollowers = d.quickTrade.enableQuickTradeForFormerFollowers;
			g_ui.draft.quickTrade.enableQuickTradeForPlayerSummons = d.quickTrade.enableQuickTradeForPlayerSummons;
			g_ui.draft.quickTrade.enableQuickTradeForInclusionActors = d.quickTrade.enableQuickTradeForInclusionActors;
			g_ui.draft.quickTrade.enableQuickTradeForKNoneActors = d.quickTrade.enableQuickTradeForKNoneActors;
			g_ui.draft.quickTrade.enableQuickTradeForMounts = d.quickTrade.enableQuickTradeForMounts;
			g_ui.draft.quickTrade.quickTradeAllWorldMounts = d.quickTrade.quickTradeAllWorldMounts;
			RebuildDerivedFromDraft();
		}

		void ResetControlsAndUIToDefaultsUI()
		{
			const auto& d = Defaults();
			g_ui.draft.keyboardControls = d.keyboardControls;
			g_ui.draft.gamepadControls = d.gamepadControls;
			g_ui.draft.buttonIndicators = d.buttonIndicators;
			g_ui.draft.iconAppearance = d.iconAppearance;
			RebuildDerivedFromDraft();
		}

		void ResetCombatEquipToDefaultsUI()
		{
			const auto& d = Defaults();
			g_ui.draft.combatEquipPreference = d.combatEquipPreference;
			g_ui.draft.combatEquipEnforcement = d.combatEquipEnforcement;
			g_ui.draft.combatEquipRestore = d.combatEquipRestore;
			RebuildDerivedFromDraft();
		}

		void ResetFixesToDefaultsUI()
		{
			const auto& d = Defaults();
			g_ui.draft.engineFixes = d.engineFixes;
			g_ui.draft.skyUIFixes = d.skyUIFixes;
			RebuildDerivedFromDraft();
		}

		//=============================================================================
		// [SECTION 9] Settings Render Sections
		//   RenderSystem / RenderScopeAndAccess / RenderControlsAndUI
		//   RenderEquipMode / RenderEquipStability / RenderCombatEquip
		//=============================================================================

		void RenderSystem()
		{
			const auto& d = Defaults();
			const bool canResetAll = !(g_ui.draft.core == d.core &&
				g_ui.draft.uiFeedback == d.uiFeedback && g_ui.draft.logging == d.logging);
			BeginSettingsPageChrome("SystemScrollRegion", "ReloadSystem", "SaveSystem", "ResetAllSystem", canResetAll, []() { ResetSystemToDefaultsUI(); });

			if (CollapsingHeaderWithLeadingIcon("Core", kIconCore, Localization::CStr("ui.header.core"))) {
				CheckboxRow(
					"ui.system.enable_mod.label",
					"##EnableMod",
					g_ui.draft.core.enableMod,
					"ui.system.enable_mod.help",
					"DefaultEnableMod",
					Defaults().core.enableMod);
				SettingsSectionDivider();
			}
			if (CollapsingHeaderWithLeadingIcon("UIFeedback", kIconUIFeedback, Localization::CStr("ui.header.ui_feedback"))) {
				const CheckboxOpt opts[] = {
					{ &g_ui.draft.uiFeedback.enableNotifications, "ui.notifications.enable.label", "##EnableNotifications", "ui.notifications.enable.help", "DefaultEnableNotifications", Defaults().uiFeedback.enableNotifications },
					{ &g_ui.draft.uiFeedback.enableUISounds, "ui.uisounds.enable.label", "##EnableUISounds", "ui.uisounds.enable.help", "DefaultEnableUISounds", Defaults().uiFeedback.enableUISounds },
				};
				RenderCheckboxGroup(opts);
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("Logging", kIconLogging, Localization::CStr("ui.header.logging"))) {
				int idx = g_ui.logLevelIndex;
				SetNarrowControlWidth();
				const auto levelLabel = Label("ui.logging.level.label", "##LogLevel");
				if (ImGuiMCP::ImGui::Combo(levelLabel.c_str(), &idx, kLogLevels, 7)) {
					g_ui.logLevelIndex = idx;
					MarkDirty();
				}
				const char* defaultLogLevel = LogLevelStringFromIndex(LogLevelIndexFromString(Defaults().logging.logLevel));
				HelpAndDefaultOnLine("ui.logging.level.help", "DefaultLogLevel", defaultLogLevel, [&]() {
					g_ui.logLevelIndex = LogLevelIndexFromString(Defaults().logging.logLevel);
				});
				SettingsSectionDivider();
			}

			EndSettingsPageChrome();
		}

		void RenderScopeAndAccess()
		{
			const auto& d = Defaults();
			const bool systemScopeDefault =
				g_ui.draft.actorScope == d.actorScope &&
				g_ui.draft.quickTrade == d.quickTrade;
			const bool canResetAll = !systemScopeDefault;
			BeginSettingsPageChrome(
				"ScopeAndAccessScrollRegion",
				"ReloadScopeAndAccess",
				"SaveScopeAndAccess",
				"ResetAllScopeAndAccess",
				canResetAll,
				[]() { ResetScopeAndAccessToDefaultsUI(); });

			if (CollapsingHeaderWithLeadingIcon("ActorScope", kIconActorScope, Localization::CStr("ui.header.actor_scope"))) {
				CheckboxRow(
					"ui.actor_scope.affect_former_followers.label",
					"##AffectFormerFollowers",
					g_ui.draft.actorScope.affectFormerFollowers,
					"ui.actor_scope.affect_former_followers.help",
					"DefaultAffectFormerFollowers",
					Defaults().actorScope.affectFormerFollowers);
				CheckboxRow(
					"ui.actor_scope.include_inclusion_actors.label",
					"##IncludeInclusionActors",
					g_ui.draft.actorScope.includeInclusionActors,
					"ui.actor_scope.include_inclusion_actors.help",
					"DefaultIncludeInclusionActors",
					Defaults().actorScope.includeInclusionActors);
				CheckboxRow(
					"ui.actor_scope.include_player_summons.label",
					"##IncludePlayerSummons",
					g_ui.draft.actorScope.includePlayerSummons,
					"ui.actor_scope.include_player_summons.help",
					"DefaultIncludePlayerSummons",
					Defaults().actorScope.includePlayerSummons);
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("QuickTrade", kIconQuickTrade, Localization::CStr("ui.header.quick_trade"))) {
				ConnectorBracket qtConnector;
				CheckboxRow(
					"ui.quick_trade.enable.label",
					"##EnableQuickTrade",
					g_ui.draft.quickTrade.enableQuickTrade,
					"ui.quick_trade.enable.help",
					"DefaultEnableQuickTrade",
					Defaults().quickTrade.enableQuickTrade,
					[&]() { qtConnector.CaptureToggleFromLastItem(); });
				RenderDependentBlock(qtConnector, g_ui.draft.quickTrade.enableQuickTrade, [&]() {
					const CheckboxOpt subOpts[] = {
						{ &g_ui.draft.quickTrade.enableQuickTradeForFollowers, "ui.quick_trade.followers.label", "##EnableQuickTradeForFollowers", "ui.quick_trade.followers.help", "DefaultEnableQuickTradeForFollowers", Defaults().quickTrade.enableQuickTradeForFollowers },
						{ &g_ui.draft.quickTrade.enableQuickTradeForFormerFollowers, "ui.quick_trade.former_followers.label", "##EnableQuickTradeForFormerFollowers", "ui.quick_trade.former_followers.help", "DefaultEnableQuickTradeForFormerFollowers", Defaults().quickTrade.enableQuickTradeForFormerFollowers },
						{ &g_ui.draft.quickTrade.enableQuickTradeForInclusionActors, "ui.quick_trade.inclusion_actors.label", "##EnableQuickTradeForInclusionActors", "ui.quick_trade.inclusion_actors.help", "DefaultEnableQuickTradeForInclusionActors", Defaults().quickTrade.enableQuickTradeForInclusionActors },
						{ &g_ui.draft.quickTrade.enableQuickTradeForPlayerSummons, "ui.quick_trade.player_summons.label", "##EnableQuickTradeForPlayerSummons", "ui.quick_trade.player_summons.help", "DefaultEnableQuickTradeForPlayerSummons", Defaults().quickTrade.enableQuickTradeForPlayerSummons },
						{ &g_ui.draft.quickTrade.enableQuickTradeForKNoneActors, "ui.quick_trade.non_humanoid_actors.label", "##EnableQuickTradeForKNoneActors", "ui.quick_trade.non_humanoid_actors.help", "DefaultEnableQuickTradeForKNoneActors", Defaults().quickTrade.enableQuickTradeForKNoneActors },
					};
					RenderCheckboxGroup(subOpts, &qtConnector);

					ConnectorBracket mountsConnector;
					CheckboxRow(
						"ui.quick_trade.mounts.label",
						"##EnableQuickTradeForMounts",
						g_ui.draft.quickTrade.enableQuickTradeForMounts,
						"ui.quick_trade.mounts.help",
						"DefaultEnableQuickTradeForMounts",
						Defaults().quickTrade.enableQuickTradeForMounts,
						[&]() {
							qtConnector.AddJoinFromLastItem();
							mountsConnector.CaptureToggleFromLastItem();
						});
					RenderDependentBlock(mountsConnector, g_ui.draft.quickTrade.enableQuickTradeForMounts, [&]() {
						CheckboxRow(
							"ui.quick_trade.all_world_mounts.label",
							"##QuickTradeAllWorldMounts",
							g_ui.draft.quickTrade.quickTradeAllWorldMounts,
							"ui.quick_trade.all_world_mounts.help",
							"DefaultQuickTradeAllWorldMounts",
							Defaults().quickTrade.quickTradeAllWorldMounts,
							[&]() { mountsConnector.AddJoinFromLastItem(); });
					});
				});
				SettingsSectionDivider();
			}

			EndSettingsPageChrome();
		}

		void RenderControlsAndUI()
		{
			const auto& d = Defaults();
			const bool canResetAll = !(g_ui.draft.keyboardControls == d.keyboardControls &&
				g_ui.draft.gamepadControls == d.gamepadControls &&
				g_ui.draft.buttonIndicators == d.buttonIndicators &&
				g_ui.draft.iconAppearance == d.iconAppearance);
			BeginSettingsPageChrome(
				"ControlsAndUIScrollRegion",
				"ReloadControlsAndUI",
				"SaveControlsAndUI",
				"ResetAllControlsAndUI",
				canResetAll,
				[]() { ResetControlsAndUIToDefaultsUI(); });

			if (CollapsingHeaderWithLeadingIcon("KeyboardControls", kIconControls, Localization::CStr("ui.header.keyboard_controls"))) {
				RenderDikKeyCombo(
					"ui.controls.mod_key.label",
					"##ModKeyDik",
					g_ui.modKeyDik,
					"ui.controls.mod_key.help",
					"DefaultModKeyDik",
					Defaults().keyboardControls.modKeyDik);
				RenderDikKeyCombo(
					"ui.controls.skyui_equip_mode_key.label",
					"##SkyuiEquipModeKey",
					g_ui.skyuiEquipModeKey,
					"ui.controls.skyui_equip_mode_key.help",
					"DefaultSkyuiEquipModeKey",
					Defaults().keyboardControls.skyuiEquipModeKey,
					nullptr,
					Localization::CStr("ui.controls.skyui_equip_mode_key.none"));
				RenderDikKeyCombo(
					"ui.controls.quick_trade_key.label",
					"##QuickTradeKeyDik",
					g_ui.quickTradeKeyDik,
					"ui.controls.quick_trade_key.help",
					"DefaultQuickTradeKeyDik",
					Defaults().keyboardControls.quickTradeKeyDik,
					nullptr,
					Localization::CStr("ui.controls.quick_trade_key.none"));
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("GamepadControls", kIconGamepadControls, Localization::CStr("ui.header.gamepad_controls"))) {
				RenderGamepadKeyCombo(
					"ui.controls.gamepad_mod_key.label",
					"##GamepadModKey",
					g_ui.gamepadModKey,
					"ui.controls.gamepad_mod_key.help",
					"DefaultGamepadModKey",
					Defaults().gamepadControls.gamepadModKey);
				RenderGamepadKeyCombo(
					"ui.controls.gamepad_right_hand_key.label",
					"##GamepadRightHandKey",
					g_ui.gamepadRightHandKey,
					"ui.controls.gamepad_right_hand_key.help",
					"DefaultGamepadRightHandKey",
					Defaults().gamepadControls.gamepadRightHandKey);
				RenderGamepadKeyCombo(
					"ui.controls.gamepad_left_hand_key.label",
					"##GamepadLeftHandKey",
					g_ui.gamepadLeftHandKey,
					"ui.controls.gamepad_left_hand_key.help",
					"DefaultGamepadLeftHandKey",
					Defaults().gamepadControls.gamepadLeftHandKey);
				RenderGamepadKeyCombo(
					"ui.controls.quick_trade_gamepad_key.label",
					"##QuickTradeGamepadKey",
					g_ui.quickTradeGamepadKey,
					"ui.controls.quick_trade_gamepad_key.help",
					"DefaultQuickTradeGamepadKey",
					Defaults().gamepadControls.quickTradeGamepadKey,
					nullptr,
					Localization::CStr("ui.controls.quick_trade_gamepad_key.none"));
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("UIButtonIndicators", kIconButtonIndicators, Localization::CStr("ui.header.ui_button_indicators"))) {
				ConnectorBracket connector;
				CheckboxRow(
					"ui.controls.mod_key_indicator.label",
					"##EnableModKeyIndicator",
					g_ui.draft.buttonIndicators.enableModKeyIndicator,
					"ui.controls.mod_key_indicator.help",
					"DefaultEnableModKeyIndicator",
					Defaults().buttonIndicators.enableModKeyIndicator,
					[&]() { connector.CaptureToggleFromLastItem(); });

				RenderDependentBlock(connector, g_ui.draft.buttonIndicators.enableModKeyIndicator, [&]() {
					TextInputRow("ui.controls.indicator_text.label", "##ModKeyIndicatorText",
						"ui.controls.indicator_text.default",
						g_ui.indicatorText.data(), g_ui.indicatorText.size(), connector,
						"ui.controls.indicator_text.help", "DefaultModKeyIndicatorText",
						[&]() { CopyToBuffer(g_ui.indicatorText, Localization::Get("ui.controls.indicator_text.default")); });

					TextInputRow("ui.controls.inclusion_indicator_text.label", "##InclusionIndicatorText",
						"ui.controls.inclusion_indicator_text.default",
						g_ui.inclusionIndicatorText.data(), g_ui.inclusionIndicatorText.size(), connector,
						"ui.controls.inclusion_indicator_text.help", "DefaultInclusionIndicatorText",
						[&]() { CopyToBuffer(g_ui.inclusionIndicatorText, Localization::Get("ui.controls.inclusion_indicator_text.default")); });

					TextInputRow("ui.controls.summon_indicator_text.label", "##SummonIndicatorText",
						"ui.controls.summon_indicator_text.default",
						g_ui.summonIndicatorText.data(), g_ui.summonIndicatorText.size(), connector,
						"ui.controls.summon_indicator_text.help", "DefaultSummonIndicatorText",
						[&]() { CopyToBuffer(g_ui.summonIndicatorText, Localization::Get("ui.controls.summon_indicator_text.default")); });

					TextInputRow("ui.controls.equip_mode_text_override.label", "##EquipModeTextOverride",
						"ui.controls.equip_mode_text_override.default",
						g_ui.equipModeTextOverride.data(), g_ui.equipModeTextOverride.size(), connector,
						"ui.controls.equip_mode_text_override.help", "DefaultEquipModeTextOverride",
						[&]() { CopyToBuffer(g_ui.equipModeTextOverride, Localization::Get("ui.controls.equip_mode_text_override.default")); });
				});
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("UIIconIndicators", kIconAppearance, Localization::CStr("ui.header.ui_icon_indicators"))) {
				ConnectorBracket iconConnector;
				CheckboxRow(
					"ui.icon_appearance.enable_indicators.label",
					"##EnableIconIndicator",
					g_ui.draft.iconAppearance.enableIconIndicator,
					"ui.icon_appearance.enable_indicators.help",
					"DefaultEnableIconIndicator",
					Defaults().iconAppearance.enableIconIndicator,
					[&]() { iconConnector.CaptureToggleFromLastItem(); });

				RenderDependentBlock(iconConnector, g_ui.draft.iconAppearance.enableIconIndicator, [&]() {
					const CheckboxOpt iconOpts[] = {
						{ &g_ui.draft.iconAppearance.enableCombatEquipIcon, "ui.combat_equip.combat_preference_icon.label", "##EnableCombatEquipIcon", "ui.combat_equip.combat_preference_icon.help", "DefaultEnableCombatEquipIcon", Defaults().iconAppearance.enableCombatEquipIcon },
						{ &g_ui.draft.iconAppearance.enableHeadgearIcon, "ui.combat_equip.headgear_preference_icon.label", "##EnableHeadgearIcon", "ui.combat_equip.headgear_preference_icon.help", "DefaultEnableHeadgearIcon", Defaults().iconAppearance.enableHeadgearIcon },
						{ &g_ui.draft.iconAppearance.enableOutfitSyncIcon, "ui.outfit_sync.saved_outfit_item_icon.label", "##EnableOutfitSyncIcon", "ui.outfit_sync.saved_outfit_item_icon.help", "DefaultEnableOutfitSyncIcon", Defaults().iconAppearance.enableOutfitSyncIcon },
						{ &g_ui.draft.iconAppearance.enableHandItemIcon, "ui.hand_item_restore.saved_hand_item_icon.label", "##EnableHandItemIcon", "ui.hand_item_restore.saved_hand_item_icon.help", "DefaultEnableHandItemIcon", Defaults().iconAppearance.enableHandItemIcon },
					};
					RenderCheckboxGroup(iconOpts, &iconConnector);

					ConnectorBracket customConnector;
					CheckboxRow(
						"ui.icon_appearance.enable_customization.label",
						"##EnableIconCustomization",
						g_ui.draft.iconAppearance.enableCustomization,
						"ui.icon_appearance.enable_customization.help",
						"DefaultEnableIconCustomization",
						Defaults().iconAppearance.enableCustomization,
						[&]() {
							iconConnector.AddJoinFromLastItem();
							customConnector.CaptureToggleFromLastItem();
						});

					RenderDependentBlock(customConnector, g_ui.draft.iconAppearance.enableCustomization, [&]() {
						DoubleSliderRow("ui.icon_appearance.icon_size.label", "##IconSize",
							g_ui.draft.iconAppearance.iconSize, 4, 24,
							"ui.icon_appearance.icon_size.help", "DefaultIconSize",
							Defaults().iconAppearance.iconSize, &customConnector);
						DoubleSliderRow("ui.icon_appearance.gap_after_text.label", "##GapAfterText",
							g_ui.draft.iconAppearance.gapAfterText, 0, 20,
							"ui.icon_appearance.gap_after_text.help", "DefaultGapAfterText",
							Defaults().iconAppearance.gapAfterText, &customConnector);
						DoubleSliderRow("ui.icon_appearance.gap_after_icon.label", "##GapAfterIcon",
							g_ui.draft.iconAppearance.gapAfterIcon, 0, 20,
							"ui.icon_appearance.gap_after_icon.help", "DefaultGapAfterIcon",
							Defaults().iconAppearance.gapAfterIcon, &customConnector);
						DoubleSliderRow("ui.icon_appearance.icon_spacing.label", "##FecIconSpacing",
							g_ui.draft.iconAppearance.fecIconSpacing, 0, 20,
							"ui.icon_appearance.icon_spacing.help", "DefaultFecIconSpacing",
							Defaults().iconAppearance.fecIconSpacing, &customConnector);
					});
				});
				SettingsSectionDivider();
			}

			EndSettingsPageChrome();
		}

		void RenderEquipMode()
		{
			const auto& d = Defaults();
			const bool canResetAll = !(g_ui.draft.equipModeItems == d.equipModeItems &&
				g_ui.draft.equipModeConsumableMode == d.equipModeConsumableMode && g_ui.draft.equipModePoisonMode == d.equipModePoisonMode &&
				g_ui.draft.equipModeSpellTomeMode == d.equipModeSpellTomeMode);
			BeginSettingsPageChrome(
				"EquipModeScrollRegion",
				"ReloadEquipMode",
				"SaveEquipMode",
				"ResetAllEquipMode",
				canResetAll,
				[]() { ResetEquipModeToDefaultsUI(); });

			if (CollapsingHeaderWithLeadingIcon("EquipModeFilter", kIconEquipMode, Localization::CStr("ui.header.equippable_items"))) {
				ConnectorBracket connector;
				CheckboxRow(
					"ui.equip_mode.enable.label",
					"##EnableEquipModeItems",
					g_ui.draft.equipModeItems.enable,
					"ui.equip_mode.enable.help",
					"DefaultEnableEquipModeItems",
					Defaults().equipModeItems.enable,
					[&]() { connector.CaptureToggleFromLastItem(); });

				RenderDependentBlock(connector, g_ui.draft.equipModeItems.enable, [&]() {
					const CheckboxOpt opts[] = {
						{ &g_ui.draft.equipModeItems.enableWeapons, "ui.equip_mode.weapons.label", "##EnableEquipModeWeapons", "ui.equip_mode.weapons.help", "DefaultEquipModeWeapons", Defaults().equipModeItems.enableWeapons },
						{ &g_ui.draft.equipModeItems.enableArmor, "ui.equip_mode.armor.label", "##EnableEquipModeArmor", "ui.equip_mode.armor.help", "DefaultEquipModeArmor", Defaults().equipModeItems.enableArmor },
						{ &g_ui.draft.equipModeItems.enableAmmo, "ui.equip_mode.ammo.label", "##EnableEquipModeAmmo", "ui.equip_mode.ammo.help", "DefaultEquipModeAmmo", Defaults().equipModeItems.enableAmmo },
						{ &g_ui.draft.equipModeItems.enableTorches, "ui.equip_mode.torches.label", "##EnableEquipModeTorches", "ui.equip_mode.torches.help", "DefaultEquipModeTorches", Defaults().equipModeItems.enableTorches },
						{ &g_ui.draft.equipModeItems.enableScrolls, "ui.equip_mode.scrolls.label", "##EnableEquipModeScrolls", "ui.equip_mode.scrolls.help", "DefaultEquipModeScrolls", Defaults().equipModeItems.enableScrolls },
					};
					RenderCheckboxGroup(opts, &connector);
				});
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("ConsumableMode", kIconConsumable, Localization::CStr("ui.header.consumables"))) {
				ConnectorBracket connector;
				CheckboxRow(
					"ui.consumable.enable.label",
					"##EnableConsumableMode",
					g_ui.draft.equipModeConsumableMode.enableConsumableMode,
					"ui.consumable.enable.help",
					"DefaultEnableConsumableMode",
					Defaults().equipModeConsumableMode.enableConsumableMode,
					[&]() { connector.CaptureToggleFromLastItem(); });

				RenderDependentBlock(connector, g_ui.draft.equipModeConsumableMode.enableConsumableMode, [&]() {
					const CheckboxOpt opts[] = {
						{ &g_ui.draft.equipModeConsumableMode.enablePotions, "ui.consumable.potions.label", "##EnablePotions", "ui.consumable.potions.help", "DefaultEnablePotions", Defaults().equipModeConsumableMode.enablePotions },
						{ &g_ui.draft.equipModeConsumableMode.enableFoods, "ui.consumable.foods.label", "##EnableFoods", "ui.consumable.foods.help", "DefaultEnableFoods", Defaults().equipModeConsumableMode.enableFoods },
						{ &g_ui.draft.equipModeConsumableMode.enableDrinks, "ui.consumable.drinks.label", "##EnableDrinks", "ui.consumable.drinks.help", "DefaultEnableDrinks", Defaults().equipModeConsumableMode.enableDrinks },
					};
					RenderCheckboxGroup(opts, &connector);

					ConnectorBracket ingredientsConnector;
					CheckboxRow(
						"ui.consumable.ingredients.label",
						"##EnableIngredients",
						g_ui.draft.equipModeConsumableMode.enableIngredients,
						"ui.consumable.ingredients.help",
						"DefaultEnableIngredients",
						Defaults().equipModeConsumableMode.enableIngredients,
						[&]() {
							connector.AddJoinFromLastItem();
							ingredientsConnector.CaptureToggleFromLastItem();
						});

					RenderDependentBlock(ingredientsConnector, g_ui.draft.equipModeConsumableMode.enableIngredients, [&]() {
						CheckboxRow(
							"ui.consumable.discovery.label",
							"##EnableIngredientDiscoveryForPlayer",
							g_ui.draft.equipModeConsumableMode.enableIngredientDiscoveryForPlayer,
							"ui.consumable.discovery.help",
							"DefaultEnableIngredientDiscoveryForPlayer",
							Defaults().equipModeConsumableMode.enableIngredientDiscoveryForPlayer,
							[&]() { ingredientsConnector.AddJoinFromLastItem(); });
					});
				});
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("PoisonMode", kIconPoison, Localization::CStr("ui.header.poisons"))) {
				// 3-state segmented selection:
				// - Apply: feature on, click poison applies to weapon
				// - Consume: feature on, click poison consumes it
				// - Disable: feature off
				int selection = 2;  // Disable (default)
				switch (g_ui.draft.equipModePoisonMode.mode) {
				case PluginSettings::PoisonMode::kConsume:
					selection = 1;
					break;
				case PluginSettings::PoisonMode::kApplyToWeapon:
					selection = 0;
					break;
				case PluginSettings::PoisonMode::kDisable:
				default:
					selection = 2;
					break;
				}

				const SegmentedOpt opts[] = {
					{ "ui.poison.apply", "##PoisonModeApply" },
					{ "ui.poison.consume", "##PoisonModeConsume" },
					{ "ui.poison.disable", "##PoisonModeDisable" },
				};
				int defaultSelection = 2;
				switch (Defaults().equipModePoisonMode.mode) {
				case PluginSettings::PoisonMode::kConsume:
					defaultSelection = 1;
					break;
				case PluginSettings::PoisonMode::kApplyToWeapon:
					defaultSelection = 0;
					break;
				case PluginSettings::PoisonMode::kDisable:
				default:
					defaultSelection = 2;
					break;
				}
				ConnectorBracket applyConnector;
				{
					ImGuiMCP::ImVec2 segRowTopScreen{};
					ImGuiMCP::ImGui::GetCursorScreenPos(&segRowTopScreen);
					SegmentedRow(
						opts,
						selection,
						"ui.poison.help",
						"DefaultPoisonModeSelection",
						[&](int index) {
							if (index == 2) {
								g_ui.draft.equipModePoisonMode.mode = PluginSettings::PoisonMode::kDisable;
								return;
							}

							g_ui.draft.equipModePoisonMode.mode = (index == 1) ? PluginSettings::PoisonMode::kConsume : PluginSettings::PoisonMode::kApplyToWeapon;
						},
						[&]() {
							g_ui.draft.equipModePoisonMode.mode = Defaults().equipModePoisonMode.mode;
						},
						nullptr,
						nullptr,
						kSegmentedDefaultStyle,
						Localization::CStr(opts[defaultSelection].labelKey));
					ImGuiMCP::ImVec2 segRowNextScreen{};
					ImGuiMCP::ImGui::GetCursorScreenPos(&segRowNextScreen);
					const auto* segRowStyle = ImGuiMCP::ImGui::GetStyle();
					const float segItemSpY = segRowStyle ? segRowStyle->ItemSpacing.y : 4.0f;
					applyConnector.toggleMin = segRowTopScreen;
					applyConnector.toggleMax = ImGuiMCP::ImVec2(segRowTopScreen.x + 1.0f, segRowNextScreen.y - segItemSpY);
					applyConnector.haveToggle = true;
				}

				RenderDependentBlock(applyConnector, g_ui.draft.equipModePoisonMode.mode == PluginSettings::PoisonMode::kApplyToWeapon, [&]() {
					ConnectorBracket stackingConnector;
					CheckboxRow(
						"ui.poison.stacking.label",
						"##EnablePoisonStacking",
						g_ui.draft.equipModePoisonMode.enableStacking,
						"ui.poison.stacking.help",
						"DefaultEnablePoisonStacking",
						Defaults().equipModePoisonMode.enableStacking,
						[&]() {
							applyConnector.AddJoinFromLastItem();
							stackingConnector.CaptureToggleFromLastItem();
						});

					RenderDependentBlock(stackingConnector, g_ui.draft.equipModePoisonMode.enableStacking, [&]() {
						SetSliderControlWidth();
						const auto maxChargesLabel = Label("ui.poison.max_charges.label", "##MaxPoisonCharges");
						int chargesVal = static_cast<int>(g_ui.draft.equipModePoisonMode.maxCharges);
						const char* chargesFmt = (chargesVal == 100) ? Localization::CStr("ui.poison.max_charges.unlimited") : "%d";
						if (ImGuiMCP::ImGui::SliderInt(maxChargesLabel.c_str(), &chargesVal, 1, 100, chargesFmt, ImGuiMCP::ImGuiSliderFlags_AlwaysClamp)) {
							g_ui.draft.equipModePoisonMode.maxCharges = static_cast<std::uint32_t>(chargesVal);
							MarkDirty();
						}
						stackingConnector.AddJoinFromLastItem();
						const auto defaultChargesRaw = static_cast<int>(Defaults().equipModePoisonMode.maxCharges);
						const std::string defaultChargesDisplay = (defaultChargesRaw == 100) ? Localization::Get("ui.poison.max_charges.unlimited") : std::to_string(defaultChargesRaw);
						HelpAndDefaultOnLine("ui.poison.max_charges.help", "DefaultMaxPoisonCharges", defaultChargesDisplay, [&]() {
							g_ui.draft.equipModePoisonMode.maxCharges = static_cast<std::uint32_t>(defaultChargesRaw);
							MarkDirty();
						});
					});
				});

				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("SpellTomeMode", kIconSpellTome, Localization::CStr("ui.header.spell_tome_mode"))) {
				ConnectorBracket learningConnector;

				CheckboxRow(
					"ui.spell_tome.enable.label",
					"##EnableSpellTomeMode",
					g_ui.draft.equipModeSpellTomeMode.enableSpellTomeMode,
					"ui.spell_tome.enable.help",
					"DefaultEnableSpellTomeMode",
					Defaults().equipModeSpellTomeMode.enableSpellTomeMode,
					[&]() {
						learningConnector.CaptureToggleFromLastItem();
					});

				RenderDependentBlock(learningConnector, g_ui.draft.equipModeSpellTomeMode.enableSpellTomeMode, [&]() {
					CheckboxRow(
						"ui.spell_tome.do_not_consume.label",
						"##DoNotConsumeSpellTomes",
						g_ui.draft.equipModeSpellTomeMode.doNotConsumeSpellTomes,
						"ui.spell_tome.do_not_consume.help",
						"DefaultDoNotConsumeSpellTomes",
						Defaults().equipModeSpellTomeMode.doNotConsumeSpellTomes,
						[&]() {
							learningConnector.AddJoinFromLastItem();
						});
				});

				SettingsSectionDivider();
			}

			EndSettingsPageChrome();
		}

		void RenderCombatEquip()
		{
			const auto& d = Defaults();
			const bool canResetAll = !(g_ui.draft.combatEquipPreference == d.combatEquipPreference &&
				g_ui.draft.combatEquipEnforcement == d.combatEquipEnforcement &&
				g_ui.draft.combatEquipRestore == d.combatEquipRestore);
			BeginSettingsPageChrome(
				"CombatEquipScrollRegion",
				"ReloadCombatEquip",
				"SaveCombatEquip",
				"ResetAllCombatEquip",
				canResetAll,
				[]() { ResetCombatEquipToDefaultsUI(); });

			if (CollapsingHeaderWithLeadingIcon("PreferenceMatch", kIconCombatEquip, Localization::CStr("ui.header.preference_match"))) {
				const CheckboxOpt opts[] = {
					{ &g_ui.draft.combatEquipPreference.enableScoring, "ui.combat_equip.equip_priority.label", "##EnableScoring", "ui.combat_equip.equip_priority.help", "DefaultEnableCombatEquipScoring", Defaults().combatEquipPreference.enableScoring },
					{ &g_ui.draft.combatEquipPreference.enableInstanceAlign, "ui.combat_equip.exact_item_match.label", "##EnableInstanceAlign", "ui.combat_equip.exact_item_match.help", "DefaultEnableCombatEquipInstanceAlign", Defaults().combatEquipPreference.enableInstanceAlign },
					{ &g_ui.draft.combatEquipPreference.enableClearPreferencesOnUnequip, "ui.combat_equip.clear_on_unequip.label", "##EnableClearPreferencesOnUnequip", "ui.combat_equip.clear_on_unequip.help", "DefaultEnableClearPreferencesOnUnequip", Defaults().combatEquipPreference.enableClearPreferencesOnUnequip },
					{ &g_ui.draft.combatEquipPreference.enableClearLeftHandPreferenceOnUnequip, "ui.combat_equip.clear_left_on_unequip.label", "##EnableClearLeftHandPreferenceOnUnequip", "ui.combat_equip.clear_left_on_unequip.help", "DefaultEnableClearLeftHandPreferenceOnUnequip", Defaults().combatEquipPreference.enableClearLeftHandPreferenceOnUnequip },
				};
				RenderCheckboxGroup(opts);
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("Enforcement", kIconCombatEquipEnforcement, Localization::CStr("ui.header.equip_overrides"))) {
				ConnectorBracket meleeConnector;
				CheckboxRow(
					"ui.combat_equip.melee_override.label",
					"##EnableMeleeEnforcement",
					g_ui.draft.combatEquipEnforcement.enableMeleeEnforcement,
					"ui.combat_equip.melee_override.help",
					"DefaultEnableCombatEquipMeleeEnforcement",
					Defaults().combatEquipEnforcement.enableMeleeEnforcement,
					[&]() { meleeConnector.CaptureToggleFromLastItem(); });
				
				RenderDependentBlock(meleeConnector, g_ui.draft.combatEquipEnforcement.enableMeleeEnforcement, [&]() {
					CheckboxRow(
						"ui.combat_equip.force_unarmed.label",
						"##EnableMeleeEnforcementEmptyHand",
						g_ui.draft.combatEquipEnforcement.enableMeleeEnforcementEmptyHand,
						"ui.combat_equip.force_unarmed.help",
						"DefaultEnableMeleeEnforcementEmptyHand",
						Defaults().combatEquipEnforcement.enableMeleeEnforcementEmptyHand,
						[&]() { meleeConnector.AddJoinFromLastItem(); });
					SettingsItemGap();
				});
				const CheckboxOpt ammoOpts[] = {
					{ &g_ui.draft.combatEquipEnforcement.enableAmmoPreference,
						"ui.combat_equip.ammo_override.label",
						"##EnableAmmoPreference",
						"ui.combat_equip.ammo_override.help",
						"DefaultEnableCombatEquipAmmoPreference",
						Defaults().combatEquipEnforcement.enableAmmoPreference },
				};
				RenderCheckboxGroup(ammoOpts);
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("AutoEquipRestore", kIconCombatEquipRestore, Localization::CStr("ui.header.auto_equip_restore"))) {
				const CheckboxOpt opts[] = {
					{ &g_ui.draft.combatEquipRestore.enableRestorePreCombatOnExit, "ui.combat_equip.pre_combat_restore.label", "##EnableRestorePreCombatOnExit", "ui.combat_equip.pre_combat_restore.help", "DefaultEnableRestorePreCombatOnExit", Defaults().combatEquipRestore.enableRestorePreCombatOnExit },
					{ &g_ui.draft.combatEquipRestore.enableHeadgearAutoEquip, "ui.combat_equip.headgear_auto_equip.label", "##EnableHeadgearAutoEquip", "ui.combat_equip.headgear_auto_equip.help", "DefaultEnableHeadgearAutoEquip", Defaults().combatEquipRestore.enableHeadgearAutoEquip },
					{ &g_ui.draft.combatEquipRestore.enableInfiniteAmmo, "ui.combat_equip.infinite_ammo.label", "##EnableInfiniteAmmo", "ui.combat_equip.infinite_ammo.help", "DefaultEnableInfiniteAmmo", Defaults().combatEquipRestore.enableInfiniteAmmo },
				};
				RenderCheckboxGroup(opts);
				SettingsSectionDivider();
			}

			EndSettingsPageChrome();
		}

		void RenderPreferenceCapture(float reservedBottomPx = 0.0f, int* viewMode = nullptr);

		void RenderEquipStability()
		{
			const auto& d = Defaults();
			const bool canResetAll = !(g_ui.draft.outfitSync == d.outfitSync &&
				g_ui.draft.hiddenItems == d.hiddenItems &&
				g_ui.draft.itemInjectionBlocking == d.itemInjectionBlocking &&
				g_ui.draft.lootBlocking == d.lootBlocking &&
				g_ui.draft.autoEquipBlocking == d.autoEquipBlocking &&
				g_ui.draft.equipGate == d.equipGate);
			BeginSettingsPageChrome(
				"EquipStabilityScrollRegion",
				"ReloadEquipStability",
				"SaveEquipStability",
				"ResetAllEquipStability",
				canResetAll,
				[]() { ResetEquipStabilityToDefaultsUI(); });

			if (CollapsingHeaderWithLeadingIcon("OutfitSync", kIconOutfitSync, Localization::CStr("ui.header.outfit_sync"))) {
				ConnectorBracket updateNpcOutfitConnector;
				CheckboxRow(
					"ui.outfit_sync.block_outfit_reapplication.label",
					"##EnableUpdateNpcOutfitSuppression",
					g_ui.draft.outfitSync.enableUpdateNpcOutfitSuppression,
					"ui.outfit_sync.block_outfit_reapplication.help",
					"DefaultEnableUpdateNpcOutfitSuppression",
					Defaults().outfitSync.enableUpdateNpcOutfitSuppression,
					[&]() { updateNpcOutfitConnector.CaptureToggleFromLastItem(); });
				RenderDependentBlock(updateNpcOutfitConnector, g_ui.draft.outfitSync.enableUpdateNpcOutfitSuppression, [&]() {
					ConnectorBracket snapshotConnector;
					CheckboxRow(
						"ui.outfit_sync.reequip_saved_outfit_items.label",
						"##EnableOutfitSnapshotRestore",
						g_ui.draft.outfitSync.enableOutfitSnapshotRestore,
						"ui.outfit_sync.reequip_saved_outfit_items.help",
						"DefaultEnableOutfitSnapshotRestore",
						Defaults().outfitSync.enableOutfitSnapshotRestore,
						[&]() {
							updateNpcOutfitConnector.AddJoinFromLastItem();
							snapshotConnector.CaptureToggleFromLastItem();
						});

					RenderDependentBlock(snapshotConnector, g_ui.draft.outfitSync.enableOutfitSnapshotRestore, [&]() {
						CheckboxRow(
							"ui.outfit_sync.allow_external_outfit_changes.label",
							"##AllowOutfitChanges",
							g_ui.draft.outfitSync.allowOutfitChanges,
							"ui.outfit_sync.allow_external_outfit_changes.help",
							"DefaultAllowOutfitChanges",
							Defaults().outfitSync.allowOutfitChanges,
							[&]() { snapshotConnector.AddJoinFromLastItem(); });
					});
				});
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("HiddenItems", kIconHiddenItems, Localization::CStr("ui.header.hidden_items"))) {
				ConnectorBracket nonPlayableGroupConnector;
				CheckboxRow(
					"ui.hidden_items.non_playable.label",
					"##EnableNonPlayableItems",
					g_ui.draft.hiddenItems.enableNonPlayableItems,
					"ui.hidden_items.non_playable.help",
					"DefaultEnableNonPlayableItems",
					Defaults().hiddenItems.enableNonPlayableItems,
					[&]() { nonPlayableGroupConnector.CaptureToggleFromLastItem(); });
				RenderDependentBlock(nonPlayableGroupConnector, g_ui.draft.hiddenItems.enableNonPlayableItems, [&]() {
					// --- Armor Handling ---
					CheckboxRow(
						"ui.hidden_items.remove_hidden_armor.label",
						"##RemoveHiddenArmor",
						g_ui.draft.hiddenItems.removeHiddenArmor,
						"ui.hidden_items.remove_hidden_armor.help",
						"DefaultRemoveHiddenArmor",
						Defaults().hiddenItems.removeHiddenArmor,
						[&]() { nonPlayableGroupConnector.AddJoinFromLastItem(); });

					// --- Weapon Handling ---
					CheckboxRow(
						"ui.hidden_items.remove_hidden_weapons.label",
						"##RemoveHiddenWeapon",
						g_ui.draft.hiddenItems.removeHiddenWeapon,
						"ui.hidden_items.remove_hidden_weapons.help",
						"DefaultRemoveHiddenWeapon",
						Defaults().hiddenItems.removeHiddenWeapon,
						[&]() { nonPlayableGroupConnector.AddJoinFromLastItem(); });

					// --- Ammo Handling ---
					CheckboxRow(
						"ui.hidden_items.remove_hidden_ammo.label",
						"##RemoveHiddenAmmo",
						g_ui.draft.hiddenItems.removeHiddenAmmo,
						"ui.hidden_items.remove_hidden_ammo.help",
						"DefaultRemoveHiddenAmmo",
						Defaults().hiddenItems.removeHiddenAmmo,
						[&]() { nonPlayableGroupConnector.AddJoinFromLastItem(); });
					SettingsItemGap();
				});

				ConnectorBracket outfitItemConnector;
				CheckboxRow(
					"ui.hidden_items.outfit_items.label",
					"##EnableOutfitItems",
					g_ui.draft.hiddenItems.enableOutfitItems,
					"ui.hidden_items.outfit_items.help",
					"DefaultEnableOutfitItems",
					Defaults().hiddenItems.enableOutfitItems,
					[&]() { outfitItemConnector.CaptureToggleFromLastItem(); });
				RenderDependentBlock(outfitItemConnector, g_ui.draft.hiddenItems.enableOutfitItems, [&]() {
					CheckboxRow(
						"ui.hidden_items.reveal_default_outfit_items.label",
						"##EnableRevealDefaultOutfitItems",
						g_ui.draft.hiddenItems.enableRevealDefaultOutfitItems,
						"ui.hidden_items.reveal_default_outfit_items.help",
						"DefaultEnableRevealDefaultOutfitItems",
						Defaults().hiddenItems.enableRevealDefaultOutfitItems,
						[&]() { outfitItemConnector.AddJoinFromLastItem(); });
					CheckboxRow(
						"ui.hidden_items.reveal_external_outfit_items.label",
						"##EnableRevealExternalOutfitItems",
						g_ui.draft.hiddenItems.enableRevealExternalOutfitItems,
						"ui.hidden_items.reveal_external_outfit_items.help",
						"DefaultEnableRevealExternalOutfitItems",
						Defaults().hiddenItems.enableRevealExternalOutfitItems,
						[&]() { outfitItemConnector.AddJoinFromLastItem(); });
				});
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("ItemInjectionBlocking", kIconItemInjectionBlocking, Localization::CStr("ui.header.item_injection_blocking"))) {
				const CheckboxOpt opts[] = {
					{ &g_ui.draft.itemInjectionBlocking.enableOutfitItemBlocker, "ui.equip_suppression.block_outfit_injection.label", "##EnableOutfitItemBlocker", "ui.equip_suppression.block_outfit_injection.help", "DefaultEnableOutfitItemBlocker", Defaults().itemInjectionBlocking.enableOutfitItemBlocker },
					{ &g_ui.draft.itemInjectionBlocking.enableLeveledItemBlocker, "ui.equip_suppression.block_leveled_injection.label", "##EnableLeveledItemBlocker", "ui.equip_suppression.block_leveled_injection.help", "DefaultEnableLeveledItemBlocker", Defaults().itemInjectionBlocking.enableLeveledItemBlocker },
				};
				RenderCheckboxGroup(opts);
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("LootBlocking", kIconLootBlocking, Localization::CStr("ui.header.loot_blocking"))) {
				const CheckboxOpt opts[] = {
					{ &g_ui.draft.lootBlocking.enablePreventCombatLoot, "ui.equip_suppression.block_weapon_acquisition.label", "##EnablePreventCombatLoot", "ui.equip_suppression.block_weapon_acquisition.help", "DefaultEnablePreventCombatLoot", Defaults().lootBlocking.enablePreventCombatLoot },
					{ &g_ui.draft.lootBlocking.enablePreventContainerLoot, "ui.equip_suppression.block_looting.label", "##EnablePreventContainerLoot", "ui.equip_suppression.block_looting.help", "DefaultEnablePreventContainerLoot", Defaults().lootBlocking.enablePreventContainerLoot },
					{ &g_ui.draft.lootBlocking.enablePreventPickupObject, "ui.equip_suppression.block_pickup.label", "##EnablePreventPickupObject", "ui.equip_suppression.block_pickup.help", "DefaultEnablePreventPickupObject", Defaults().lootBlocking.enablePreventPickupObject },
				};
				RenderCheckboxGroup(opts);
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("AutoEquipBlocking", kIconAutoEquipBlocking, Localization::CStr("ui.header.auto_equip_blocking"))) {
				ConnectorBracket nonCombatConnector;
				CheckboxRow(
					"ui.equip_suppression.block_non_combat_auto_equip.label",
					"##EnableNonCombatEquipBlocker",
					g_ui.draft.autoEquipBlocking.enableNonCombatEquipBlocker,
					"ui.equip_suppression.block_non_combat_auto_equip.help",
					"DefaultEnableNonCombatEquipBlocker",
					Defaults().autoEquipBlocking.enableNonCombatEquipBlocker,
					[&]() { nonCombatConnector.CaptureToggleFromLastItem(); });

				RenderDependentBlock(nonCombatConnector, g_ui.draft.autoEquipBlocking.enableNonCombatEquipBlocker, [&]() {
					CheckboxRow(
						"ui.hand_item_restore.reequip_saved_hand_items.label",
						"##EnableHandItemRestore",
						g_ui.draft.autoEquipBlocking.enableHandItemRestore,
						"ui.hand_item_restore.reequip_saved_hand_items.help",
						"DefaultEnableHandItemRestore",
						Defaults().autoEquipBlocking.enableHandItemRestore,
						[&]() { nonCombatConnector.AddJoinFromLastItem(); });
					SettingsItemGap();
				});

				CheckboxRow(
					"ui.equip_suppression.block_best_weapon_auto_equip.label",
					"##EnableBestWeaponAutoEquipSuppressor",
					g_ui.draft.autoEquipBlocking.enableBestWeaponAutoEquipSuppressor,
					"ui.equip_suppression.block_best_weapon_auto_equip.help",
					"DefaultEnableBestWeaponAutoEquipSuppressor",
					Defaults().autoEquipBlocking.enableBestWeaponAutoEquipSuppressor);
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("EquipGate", kIconEquipGate, Localization::CStr("ui.header.equip_gate"))) {
				ConnectorBracket connector;
				CheckboxRow(
					"ui.equip_gate.block_equip.label",
					"##EnableEquipBlocking",
					g_ui.draft.equipGate.enableEquipBlocking,
					"ui.equip_gate.block_equip.help",
					"DefaultEnableEquipBlocking",
					Defaults().equipGate.enableEquipBlocking);
				CheckboxRow(
					"ui.equip_gate.block_unequip.label",
					"##EnableUnequipBlocking",
					g_ui.draft.equipGate.enableUnequipBlocking,
					"ui.equip_gate.block_unequip.help",
					"DefaultEnableUnequipBlocking",
					Defaults().equipGate.enableUnequipBlocking,
					[&]() { connector.CaptureToggleFromLastItem(); });

				const bool enableNeverBlockTorchUi = g_ui.draft.equipGate.enableEquipBlocking || g_ui.draft.equipGate.enableUnequipBlocking;
				RenderDependentBlock(connector, enableNeverBlockTorchUi, [&]() {
					CheckboxRow(
						"ui.equip_gate.never_block_torch.label",
						"##NeverBlockTorchEquip",
						g_ui.draft.equipGate.enableNeverBlockTorchEquip,
						"ui.equip_gate.never_block_torch.help",
						"DefaultNeverBlockTorchEquip",
						Defaults().equipGate.enableNeverBlockTorchEquip,
						[&]() { connector.AddJoinFromLastItem(); });
				});
				SettingsSectionDivider();
			}

			EndSettingsPageChrome();
		}

		//=============================================================================
		// [SECTION 10] Actor Management + Settings (tail)
		//   10a. RenderActorManagement - entry point, view state (Preferences / Spell List)
		//   RenderExtraFeatures / RenderFixes - also located in this section
		//   10b. namespace SpellListView - per-actor spell list with suppress toggles
		//        SpellListView::Render is called by RenderPreferenceCapture.
		//   10c. Actor Management utilities - Hex8, CEPCategoryName, CEPCat,
		//        BuildCEPCategorySets, DescribeTESFormNameOnly, DescribeTemperQuality,
		//        BaseFormHasEnchantment, DescribeEnchantmentEffects, ResolveEnchantment,
		//        BuildSignatureLines, SetFrameHelper
		//   10d. RenderPreferenceCapture - actor tabs, view toggle,
		//        preferences table, SpellListView::Render dispatch, modals
		//=============================================================================

		void RenderActorManagement()
		{
			ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowPadding, ImGuiMCP::ImVec2(0.0f, 0.0f));
			ImGuiMCP::ImGui::BeginChild(
				"ActorManagementLayout",
				ImGuiMCP::ImVec2(0.0f, 0.0f),
				ImGuiMCP::ImGuiChildFlags_None,
				ImGuiMCP::ImGuiWindowFlags_NoScrollbar | ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse);
			ImGuiMCP::ImGui::PopStyleVar();

			EnsureInitialized();

			// Active view: 0 = Preferences (Preference Capture), 1 = Spell List.
			static int s_actorMgmtView = 0;
			RenderPreferenceCapture(0.0f, &s_actorMgmtView);

			CommitDerivedToDraft();
			RecomputeDirtyFromUI();
			ImGuiMCP::ImGui::EndChild();
		}

		void RenderExtraFeatures()
		{
			const auto& d = Defaults();
			const bool canResetAll = !(g_ui.draft.weaponEnchantmentRecharge == d.weaponEnchantmentRecharge && g_ui.draft.statsDisplay == d.statsDisplay && g_ui.draft.corpseEquipMode == d.corpseEquipMode);
			BeginSettingsPageChrome(
				"ExtraFeaturesScrollRegion",
				"ReloadExtraFeatures",
				"SaveExtraFeatures",
				"ResetAllExtraFeatures",
				canResetAll,
				[]() { ResetExtraFeaturesToDefaultsUI(); });

			if (CollapsingHeaderWithLeadingIcon("WeaponEnchantmentRecharge", kIconWeaponRecharge, Localization::CStr("ui.header.enchantment_recharge"))) {
				ConnectorBracket connector;
				CheckboxRow(
					"ui.weapon_recharge.enable_recharge.label",
					"##EnableRecharge",
					g_ui.draft.weaponEnchantmentRecharge.enableRecharge,
					"ui.weapon_recharge.enable_recharge.help",
					"DefaultEnableRecharge",
					Defaults().weaponEnchantmentRecharge.enableRecharge,
					[&]() { connector.CaptureToggleFromLastItem(); });

				RenderDependentBlock(connector, g_ui.draft.weaponEnchantmentRecharge.enableRecharge, [&]() {
					RenderDikKeyCombo(
						"ui.weapon_recharge.recharge_key.label",
						"##RechargeKeyDik",
						g_ui.rechargeKeyDik,
						"ui.weapon_recharge.recharge_key.help",
						"DefaultWeaponRechargeKeyDik",
						Defaults().weaponEnchantmentRecharge.rechargeKeyDik,
						&connector);

					RenderGamepadKeyCombo(
						"ui.weapon_recharge.gamepad_recharge_key.label",
						"##GamepadRechargeKey",
						g_ui.gamepadRechargeKey,
						"ui.weapon_recharge.gamepad_recharge_key.help",
						"DefaultGamepadRechargeKey",
						Defaults().weaponEnchantmentRecharge.gamepadRechargeKey,
						&connector);
					SettingsItemGap();

					CheckboxRow(
						"ui.weapon_recharge.enable_indicator.label",
						"##EnableRechargeUIIndicator",
						g_ui.draft.weaponEnchantmentRecharge.enableUIIndicator,
						"ui.weapon_recharge.enable_indicator.help",
						"DefaultEnableRechargeUIIndicator",
						Defaults().weaponEnchantmentRecharge.enableUIIndicator,
						[&]() { connector.AddJoinFromLastItem(); });

					ConnectorBracket advConnector;
					CheckboxRow(
						"ui.weapon_recharge.advanced_display.label",
						"##WeaponRechargeAdvancedSoulGemDisplay",
						g_ui.draft.weaponEnchantmentRecharge.advancedSoulGemDisplay,
						"ui.weapon_recharge.advanced_display.help",
						"DefaultWeaponRechargeAdvancedSoulGemDisplay",
						Defaults().weaponEnchantmentRecharge.advancedSoulGemDisplay,
						[&]() {
							connector.AddJoinFromLastItem();
							advConnector.CaptureToggleFromLastItem();
						});

					RenderDependentBlock(advConnector, g_ui.draft.weaponEnchantmentRecharge.advancedSoulGemDisplay, [&]() {
						CheckboxRow(
							"ui.weapon_recharge.show_stack_count.label",
							"##WeaponRechargeShowStackCount",
							g_ui.draft.weaponEnchantmentRecharge.showStackCount,
							"ui.weapon_recharge.show_stack_count.help",
							"DefaultWeaponRechargeShowStackCount",
							Defaults().weaponEnchantmentRecharge.showStackCount,
							[&]() { advConnector.AddJoinFromLastItem(); });
						SettingsItemGap();

						// Show contained soul (segmented): Disable / Lower-only / Always
						{
							const int selection = static_cast<int>(g_ui.draft.weaponEnchantmentRecharge.containedSoulDisplayMode);
							const int defaultSelection = static_cast<int>(Defaults().weaponEnchantmentRecharge.containedSoulDisplayMode);
							const SegmentedOpt opts[] = {
								{ "ui.weapon_recharge.contained_soul_mode.disable", "##WeaponRechargeContainedSoulDisable" },
								{ "ui.weapon_recharge.contained_soul_mode.lower_only", "##WeaponRechargeContainedSoulLowerOnly" },
								{ "ui.weapon_recharge.contained_soul_mode.always", "##WeaponRechargeContainedSoulAlways" },
							};
							SegmentedRow(
								opts,
								selection,
								"ui.weapon_recharge.contained_soul_mode.help",
								"DefaultWeaponRechargeContainedSoulMode",
								[&](int index) {
									g_ui.draft.weaponEnchantmentRecharge.containedSoulDisplayMode = static_cast<PluginSettings::ContainedSoulDisplayMode>(index);
								},
								[&]() {
									g_ui.draft.weaponEnchantmentRecharge.containedSoulDisplayMode = Defaults().weaponEnchantmentRecharge.containedSoulDisplayMode;
								},
								"ui.weapon_recharge.contained_soul_mode.label",
								&advConnector,
								kSegmentedDefaultStyle,
								Localization::CStr(opts[defaultSelection].labelKey));
						}
						SettingsItemGap();

						// Show source (segmented): Disable / Follower / Player / Both
						{
							const int selection = static_cast<int>(g_ui.draft.weaponEnchantmentRecharge.sourceDisplayMode);
							const int defaultSelection = static_cast<int>(Defaults().weaponEnchantmentRecharge.sourceDisplayMode);
							const SegmentedOpt opts[] = {
								{ "ui.weapon_recharge.source_tag.disable", "##WeaponRechargeSourceDisable" },
								{ "ui.weapon_recharge.source_tag.follower", "##WeaponRechargeSourceFollower" },
								{ "ui.weapon_recharge.source_tag.player", "##WeaponRechargeSourcePlayer" },
								{ "ui.weapon_recharge.source_tag.both", "##WeaponRechargeSourceBoth" },
							};
							SegmentedRow(
								opts,
								selection,
								"ui.weapon_recharge.source_tag.help",
								"DefaultWeaponRechargeSourceMode",
								[&](int index) {
									g_ui.draft.weaponEnchantmentRecharge.sourceDisplayMode = static_cast<PluginSettings::SoulGemSourceDisplayMode>(index);
								},
								[&]() {
									g_ui.draft.weaponEnchantmentRecharge.sourceDisplayMode = Defaults().weaponEnchantmentRecharge.sourceDisplayMode;
								},
								"ui.weapon_recharge.source_tag.label",
								&advConnector,
								kSegmentedDefaultStyle,
								Localization::CStr(opts[defaultSelection].labelKey));
						}
						SettingsItemGap();

						// Soul Gem Sorting (segmented): Name / Soul ASC / Soul DESC
						{
							const int selection = static_cast<int>(g_ui.draft.weaponEnchantmentRecharge.soulGemSortMode);
							const int defaultSelection = static_cast<int>(Defaults().weaponEnchantmentRecharge.soulGemSortMode);
							const SegmentedOpt opts[] = {
								{ "ui.weapon_recharge.soul_gem_sorting.name", "##WeaponRechargeSortName" },
								{ "ui.weapon_recharge.soul_gem_sorting.soul_asc", "##WeaponRechargeSortSoulAsc" },
								{ "ui.weapon_recharge.soul_gem_sorting.soul_desc", "##WeaponRechargeSortSoulDesc" },
							};
							SegmentedRow(
								opts,
								selection,
								"ui.weapon_recharge.soul_gem_sorting.help",
								"DefaultWeaponRechargeSoulGemSortMode",
								[&](int index) {
									g_ui.draft.weaponEnchantmentRecharge.soulGemSortMode = static_cast<PluginSettings::SoulGemSortMode>(index);
								},
								[&]() {
									g_ui.draft.weaponEnchantmentRecharge.soulGemSortMode = Defaults().weaponEnchantmentRecharge.soulGemSortMode;
								},
								"ui.weapon_recharge.soul_gem_sorting.label",
								&advConnector,
								kSegmentedDefaultStyle,
								Localization::CStr(opts[defaultSelection].labelKey));
						}
					});
				});
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("StatsDisplay", kIconStatsDisplay, Localization::CStr("ui.header.follower_stats"))) {
				// 3-state segmented selection:
				// - Always: show follower stats on Take + Give
				// - Follower Side: show follower stats only on Take (current behavior)
				// - Disable: feature off
				int selection = 1;  // Follower Side
				switch (g_ui.draft.statsDisplay.followerStatsInTradeMenuMode) {
				case PluginSettings::FollowerStatsInTradeMenuMode::kDisable:
					selection = 2;
					break;
				case PluginSettings::FollowerStatsInTradeMenuMode::kAlways:
					selection = 0;
					break;
				case PluginSettings::FollowerStatsInTradeMenuMode::kFollowerSide:
				default:
					selection = 1;
					break;
				}

				const SegmentedOpt opts[] = {
					{ "ui.stats_display.always", "##FollowerStatsTradeMenuAlways" },
					{ "ui.stats_display.follower_side", "##FollowerStatsTradeMenuFollowerSide" },
					{ "ui.stats_display.disable", "##FollowerStatsTradeMenuDisable" },
				};
				int defaultSelection = 1;
				switch (Defaults().statsDisplay.followerStatsInTradeMenuMode) {
				case PluginSettings::FollowerStatsInTradeMenuMode::kDisable:
					defaultSelection = 2;
					break;
				case PluginSettings::FollowerStatsInTradeMenuMode::kAlways:
					defaultSelection = 0;
					break;
				case PluginSettings::FollowerStatsInTradeMenuMode::kFollowerSide:
				default:
					defaultSelection = 1;
					break;
				}
				SegmentedRow(
					opts,
					selection,
					"ui.stats_display.help",
					"DefaultEnableFollowerStatsInTradeMenu",
					[&](int index) {
						switch (index) {
						case 0:
							g_ui.draft.statsDisplay.followerStatsInTradeMenuMode = PluginSettings::FollowerStatsInTradeMenuMode::kAlways;
							break;
						case 2:
							g_ui.draft.statsDisplay.followerStatsInTradeMenuMode = PluginSettings::FollowerStatsInTradeMenuMode::kDisable;
							break;
						case 1:
						default:
							g_ui.draft.statsDisplay.followerStatsInTradeMenuMode = PluginSettings::FollowerStatsInTradeMenuMode::kFollowerSide;
							break;
						}
					},
					[&]() {
						g_ui.draft.statsDisplay.followerStatsInTradeMenuMode = Defaults().statsDisplay.followerStatsInTradeMenuMode;
					},
					nullptr,
					nullptr,
					kSegmentedDefaultStyle,
					Localization::CStr(opts[defaultSelection].labelKey));
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("CorpseEquipMode", kIconCorpseEquip, Localization::CStr("ui.header.corpse_equip"))) {
				ConnectorBracket connector;
				CheckboxRow(
					"ui.corpse_equip.enable.label",
					"##EnableCorpseEquipMode",
					g_ui.draft.corpseEquipMode.enable,
					"ui.corpse_equip.enable.help",
					"DefaultEnableCorpseEquipMode",
					Defaults().corpseEquipMode.enable,
					[&]() { connector.CaptureToggleFromLastItem(); });

				RenderDependentBlock(connector, g_ui.draft.corpseEquipMode.enable, [&]() {
					TextInputRow("ui.corpse_equip.indicator_text.label", "##CorpseIndicatorText",
						"ui.corpse_equip.indicator_text.default",
						g_ui.corpseIndicatorText.data(), g_ui.corpseIndicatorText.size(), connector,
						"ui.corpse_equip.indicator_text.help", "DefaultCorpseIndicatorText",
						[&]() { CopyToBuffer(g_ui.corpseIndicatorText, Localization::Get("ui.corpse_equip.indicator_text.default")); });
				});

				SettingsSectionDivider();
			}

			EndSettingsPageChrome();
		}

		void RenderFixes()
		{
			const auto& d = Defaults();
			const bool canResetAll = !(g_ui.draft.engineFixes == d.engineFixes &&
				g_ui.draft.skyUIFixes == d.skyUIFixes);
			BeginSettingsPageChrome(
				"FixesScrollRegion",
				"ReloadFixes",
				"SaveFixes",
				"ResetAllFixes",
				canResetAll,
				[]() { ResetFixesToDefaultsUI(); });

			if (CollapsingHeaderWithLeadingIcon("EngineFixes", kIconFixesEngine, Localization::CStr("ui.header.engine_fixes"))) {
				const CheckboxOpt opts[] = {
					{ &g_ui.draft.engineFixes.enableStaleWeightCacheFix, "ui.stale_weight_cache.enable.label", "##EnableStaleWeightCacheFix", "ui.stale_weight_cache.enable.help", "DefaultEnableStaleWeightCacheFix", Defaults().engineFixes.enableStaleWeightCacheFix },
				};
				RenderCheckboxGroup(opts);
				SettingsSectionDivider();
			}

			if (CollapsingHeaderWithLeadingIcon("SkyUIFixes", kIconFixesSkyUI, Localization::CStr("ui.header.skyui_fixes"))) {
				const CheckboxOpt opts[] = {
					{ &g_ui.draft.skyUIFixes.enableQuantityMenuBlocker, "ui.quantity_menu_blocker.enable.label", "##EnableQuantityMenuBlocker", "ui.quantity_menu_blocker.enable.help", "DefaultEnableQuantityMenuBlocker", Defaults().skyUIFixes.enableQuantityMenuBlocker },
					{ &g_ui.draft.skyUIFixes.enableZeroWeightFix, "ui.zero_weight.enable.label", "##EnableZeroWeightFix", "ui.zero_weight.enable.help", "DefaultEnableZeroWeightFix", Defaults().skyUIFixes.enableZeroWeightFix },
				};
				RenderCheckboxGroup(opts);
				SettingsSectionDivider();
			}

			EndSettingsPageChrome();
		}

		// -- 10b. namespace SpellListView ------------------------------------------
		// Spell List view (per-actor known spells with suppress toggles)
		// -------------------------------------------------------------------------
		namespace SpellListView
		{
			// -- Data ---------------------------------------------------------------
			struct PropLine {
				std::string         label;
				std::string         value;
				ImGuiMCP::ImVec4    valueColor = { 0.90f, 0.90f, 0.90f, 1.0f };
				const char*         labelKey   = nullptr;
			};

			// A run of text with a fixed color. Used for description rich-text.
			struct DescSegment {
				std::string      text;
				ImGuiMCP::ImVec4 color;
			};

			struct SpellEntry
			{
				RE::SpellItem*        spell      = nullptr;
				const char*           name       = nullptr;
				RE::ActorValue        school     = RE::ActorValue::kNone;
				std::int32_t          tier       = 0;
				bool                  casting           = false;
				bool                  suppressed        = false;
				bool                  isConcentration   = false;
				bool                  isBaseSpell       = false;  // from NPC template or race record
				std::vector<DescSegment> descSegments; // rich description: plain text + colored tag values
				std::vector<PropLine>    infoProps;    // Plugin (metadata row)
				std::vector<PropLine>    statProps;    // Magnitude, Duration, Area, Cost
			};

			// Vanilla schools in fixed display order.
			constexpr std::array<RE::ActorValue, 5> kVanillaSchools = {
				RE::ActorValue::kAlteration,
				RE::ActorValue::kConjuration,
				RE::ActorValue::kDestruction,
				RE::ActorValue::kIllusion,
				RE::ActorValue::kRestoration,
			};

			[[nodiscard]] inline bool IsVanillaSchool(RE::ActorValue a_av) noexcept
			{
				for (auto v : kVanillaSchools) {
					if (v == a_av) return true;
				}
				return false;
			}

			[[nodiscard]] inline const char* TierLocKey(std::int32_t a_minSkill) noexcept
			{
				if (a_minSkill >= 100) return "ui.spell_list.tier.master";
				if (a_minSkill >= 75)  return "ui.spell_list.tier.expert";
				if (a_minSkill >= 50)  return "ui.spell_list.tier.adept";
				if (a_minSkill >= 25)  return "ui.spell_list.tier.apprentice";
				return "ui.spell_list.tier.novice";
			}

			// Runtime school display name - vanilla hard-coded, mod-added via ActorValueInfo.
			[[nodiscard]] inline std::string SchoolDisplayName(RE::ActorValue a_av)
			{
				switch (a_av) {
					case RE::ActorValue::kAlteration:  return Localization::Get("ui.spell_list.school.alteration");
					case RE::ActorValue::kConjuration: return Localization::Get("ui.spell_list.school.conjuration");
					case RE::ActorValue::kDestruction: return Localization::Get("ui.spell_list.school.destruction");
					case RE::ActorValue::kIllusion:    return Localization::Get("ui.spell_list.school.illusion");
					case RE::ActorValue::kRestoration: return Localization::Get("ui.spell_list.school.restoration");
					default: break;
				}
				if (auto* avl = RE::ActorValueList::GetSingleton()) {
					if (auto* info = avl->GetActorValue(a_av)) {
						if (const char* fn = info->GetFullName(); fn && fn[0] != '\0') {
							return std::string(fn);
						}
						if (info->enumName && info->enumName[0] != '\0') {
							return std::string(info->enumName);
						}
					}
				}
				return std::string(Localization::CStr("ui.spell_list.school.other"));
			}

			// Per-school border/header color for vanilla schools; golden-angle hash for mod-added schools.
			[[nodiscard]] inline ImGuiMCP::ImVec4 SchoolColor(RE::ActorValue a_av)
			{
				const auto& sl = kSpellListStyle;
				switch (a_av) {
					case RE::ActorValue::kAlteration:  return sl.colorAlteration;
					case RE::ActorValue::kConjuration: return sl.colorConjuration;
					case RE::ActorValue::kDestruction: return sl.colorDestruction;
					case RE::ActorValue::kIllusion:    return sl.colorIllusion;
					case RE::ActorValue::kRestoration: return sl.colorRestoration;
					default: break;
				}
				// Deterministic hue from raw AV integer (golden-angle hash for perceptual spread).
				const std::uint32_t raw = static_cast<std::uint32_t>(a_av);
				const float hue = std::fmod(static_cast<float>(raw) * 137.508f, 360.0f) / 360.0f;
				const float s = 0.55f, v = 0.72f;
				const float fi = std::floor(hue * 6.0f);
				const float f  = hue * 6.0f - fi;
				const float p  = v * (1.0f - s);
				const float q  = v * (1.0f - f * s);
				const float t  = v * (1.0f - (1.0f - f) * s);
				float r = 0.0f, g = 0.0f, b = 0.0f;
				switch (static_cast<int>(fi) % 6) {
					case 0: r = v; g = t; b = p; break;
					case 1: r = q; g = v; b = p; break;
					case 2: r = p; g = v; b = t; break;
					case 3: r = p; g = q; b = v; break;
					case 4: r = t; g = p; b = v; break;
					case 5: r = v; g = p; b = q; break;
				}
				return ImGuiMCP::ImVec4{ r, g, b, 1.0f };
			}

			// Per-tier color - used for right-side row overlay and metadata display.
			// Novice -> grey, Apprentice -> green, Adept -> sky-blue, Expert -> purple, Master -> gold.
			[[nodiscard]] inline ImGuiMCP::ImVec4 TierColor(std::int32_t a_minSkill) noexcept
			{
				const auto& sl = kSpellListStyle;
				if (a_minSkill >= 100) return sl.colorTierMaster;
				if (a_minSkill >= 75)  return sl.colorTierExpert;
				if (a_minSkill >= 50)  return sl.colorTierAdept;
				if (a_minSkill >= 25)  return sl.colorTierApprentice;
				return sl.colorTierNovice;
			}

			// -- Spell collection ----------------------------------------------------

			// Build rich description segments from a template with <tag> substitutions.
			// Plain text gets kDescCol, <mag>/<dur>/<area> get their stat colors,
			// pure-numeric tags get a warm accent; other unknown tags are silently dropped.
			inline void BuildDescSegments(SpellEntry& e, const char* a_tmpl,
				float a_mag, std::uint32_t a_dur, std::uint32_t a_area)
			{
				if (!a_tmpl || a_tmpl[0] == '\0') return;
				const auto& kDescCol     = kSpellListStyle.descTextCol;
				const auto& kTagOtherCol = kSpellListStyle.tagNumericCol;
				std::string plain;
				const auto flush = [&]() {
					if (!plain.empty()) {
						e.descSegments.push_back({ plain, kDescCol });
						plain.clear();
					}
				};
				const char* p = a_tmpl;
				while (*p) {
					if (*p == '[') {
						// Survival Mode bracket tag: [KEY=value] - show only the value in survivalColor.
						const char* eq = p + 1;
						while (*eq && *eq != '=' && *eq != ']') ++eq;
						if (*eq == '=') {
							const char* vs = eq + 1;
							const char* ve = vs;
							while (*ve && *ve != ']') ++ve;
							if (*ve == ']') {
								flush();
								const std::string val(vs, static_cast<std::size_t>(ve - vs));
								if (!val.empty())
									e.descSegments.push_back({ val, kSpellListStyle.survivalColor });
								p = ve + 1;
								continue;
							}
						}
						plain += *p++;
						continue;
					}
					if (*p != '<') { plain += *p++; continue; }
					const char* ts = p + 1;
					const char* te = ts;
					while (*te && *te != '>') ++te;
					if (*te != '>') { plain += *p++; continue; }
					const std::string_view tag(ts, static_cast<std::size_t>(te - ts));
					char buf[32] = {};
					ImGuiMCP::ImVec4 col = kTagOtherCol;
					if (tag == "mag") {
						std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(a_mag + 0.5f));
						col = kSpellListStyle.magnitudeColor;
					} else if (tag == "dur") {
						std::snprintf(buf, sizeof(buf), "%u", a_dur);
						col = kSpellListStyle.durationColor;
					} else if (tag == "area") {
						std::snprintf(buf, sizeof(buf), "%u", a_area);
						col = kSpellListStyle.areaColor;
					} else {
						bool allDigits = !tag.empty();
						for (char c : tag) if (!std::isdigit(static_cast<unsigned char>(c))) { allDigits = false; break; }
						if (allDigits) std::snprintf(buf, sizeof(buf), "%.*s", static_cast<int>(tag.size()), tag.data());
						// else: silently drop unknown tags
					}
					if (buf[0] != '\0') { flush(); e.descSegments.push_back({ buf, col }); }
					p = te + 1;
				}
				flush();
			}

			// Builds infoProps / statProps / descSegments for a SpellEntry.
			// Description: DNAM text first; if empty, falls back to aggregated effect descriptions.
			inline void BuildEntryStrings(SpellEntry& e)
			{
				e.infoProps.clear();
				e.statProps.clear();
			e.descSegments.clear();
				if (auto* file = e.spell->GetFile(); file) {
					const auto sv = file->GetFilename();
					if (!sv.empty()) e.infoProps.push_back({ "Plugin", std::string(sv) });
				}

				// -- Column 3: Magnitude / Duration / Area - always 3 rows, blank value when zero ----
				{
					float         mag  = 0.0f;
					std::uint32_t dur  = 0;
					std::uint32_t area = 0;
					if (!e.spell->effects.empty()) {
						if (auto* eff = e.spell->effects[0]; eff) {
							mag  = eff->effectItem.magnitude;
							dur  = eff->effectItem.duration;
							area = eff->effectItem.area;
						}
					}
					{ char buf[32] = {}; if (mag  > 0.0f) std::snprintf(buf, sizeof(buf), "%d",    static_cast<int>(mag + 0.5f)); e.statProps.push_back({ "Magnitude", buf, kSpellListStyle.magnitudeColor, "ui.spell_list.detail.stat.magnitude" }); }
					{ char buf[32] = {}; if (dur  > 0) { if (dur > 900000u) std::snprintf(buf, sizeof(buf), "%s", "\x01"); else std::snprintf(buf, sizeof(buf), "%u", dur); } e.statProps.push_back({ "Duration",  buf, kSpellListStyle.durationColor,  "ui.spell_list.detail.stat.duration"  }); }
					{ char buf[32] = {}; if (area > 0)    std::snprintf(buf, sizeof(buf), "%u",    area);                         e.statProps.push_back({ "Area",      buf, kSpellListStyle.areaColor,      "ui.spell_list.detail.stat.area"      }); }
					{
						const int costInt = static_cast<int>(e.spell->CalculateMagickaCost(nullptr) + 0.5f);
						char buf[32] = {};
						std::snprintf(buf, sizeof(buf), "%d", costInt);
						e.statProps.push_back({ "Cost", buf, kSpellListStyle.costColor });
						e.isConcentration = (e.spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration);
					}
				}

				// -- Column 1: Description -------------------------------------------
				// Priority:
				//   1. SpellItem DNAM (TESDescription::GetDescription) - many mod spells have this.
				//   2. EffectSetting::magicItemDescription (BSFixedString at 0x180, DNAM on the
				//      MagicEffect record) - this is exactly what SkyUI shows on the item card.
				//      Concatenated across all effects with '\n' as separator.
				//   3. Empty -> UI shows "-" as a placeholder.
				{
					RE::BSString desc;
					e.spell->GetDescription(desc, e.spell);
					const char* s = desc.c_str();
					if (s && s[0] != '\0') {
						// SpellItem DNAM exists - build rich segments using first effect's values.
						if (!e.spell->effects.empty() && e.spell->effects[0]) {
							const auto& ei0 = e.spell->effects[0]->effectItem;
							BuildDescSegments(e, s, ei0.magnitude, ei0.duration, ei0.area);
						} else {
							e.descSegments.push_back({ std::string(s), kSpellListStyle.descTextCol });
						}
					} else {
						// Fallback: per-effect magicItemDescription (EffectSetting DNAM).
						// Each effect's description is resolved against that effect's own values.
						for (auto* eff : e.spell->effects) {
							if (!eff || !eff->baseEffect) continue;
							if (eff->baseEffect->magicItemDescription.empty()) continue;
							const char* d = eff->baseEffect->magicItemDescription.c_str();
							if (!d || d[0] == '\0') continue;
							if (!e.descSegments.empty()) e.descSegments.push_back({ "\n", kSpellListStyle.descTextCol });
							BuildDescSegments(e, d, eff->effectItem.magnitude, eff->effectItem.duration, eff->effectItem.area);
						}
					}
				}
			}

			struct CollectVisitor : public RE::Actor::ForEachSpellVisitor
			{
				std::vector<SpellEntry>*                      out        = nullptr;
				RE::Actor*                                    actor      = nullptr;
				RE::FormID                                    actorID    = 0;
				const std::unordered_set<RE::SpellItem*>*     baseSpells = nullptr;

				RE::BSContainer::ForEachResult Visit(RE::SpellItem* a_spell) override
				{
					if (!a_spell) return RE::BSContainer::ForEachResult::kContinue;
					if (a_spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell)
						return RE::BSContainer::ForEachResult::kContinue;

					SpellEntry e{};
					e.spell       = a_spell;
					e.name        = a_spell->GetFullName();
					if (!e.name || e.name[0] == '\0') e.name = "<unnamed>";
					e.school      = a_spell->GetAssociatedSkill();
					if (auto* c = a_spell->GetCostliestEffectItem(RE::MagicSystem::Delivery::kTotal, false))
						if (c->baseEffect) e.tier = c->baseEffect->GetMinimumSkillLevel();
					e.casting = false;
					if (actor) {
						for (int s = 0; s <= static_cast<int>(RE::MagicSystem::CastingSource::kOther); ++s) {
							auto* caster = actor->GetMagicCaster(static_cast<RE::MagicSystem::CastingSource>(s));
							if (!caster || !caster->currentSpell) continue;
							const auto st = caster->state.get();
							if ((st == RE::MagicCaster::State::kCasting ||
							     st == RE::MagicCaster::State::kCharging)
								&& caster->currentSpell == a_spell) {
								e.casting = true;
								break;
							}
						}
					}
					e.suppressed  = false;
					e.isBaseSpell = (baseSpells && baseSpells->count(a_spell) > 0);
					BuildEntryStrings(e);
					out->push_back(std::move(e));
					return RE::BSContainer::ForEachResult::kContinue;
				}
			};

			inline void CollectSpells(RE::Actor* a_actor, std::vector<SpellEntry>& a_out)
			{
				a_out.clear();
				if (!a_actor) return;

				const RE::FormID actorID = a_actor->GetFormID();

				// Build base spell set from the NPC base form (TESNPC::spellList) and race record.
				// Spells in these lists live on the shared form data, not on the actor reference.
				// Actor::RemoveSpell only removes actor-reference-level spells (added via
				// Actor::AddSpell) and silently fails for anything in the base form.
				// Mods such as SPID (Spell Perk Item Distributor) distribute spells by calling
				// npc->GetSpellList()->AddSpells() directly, so SPID-granted spells land here
				// and are equally non-removable at the actor-reference level.
				// FEC marks all spells found in either list as isBaseSpell = true and renders
				// an informational badge in place of the suppress button for them.
				std::unordered_set<RE::SpellItem*> baseSpellSet;
				if (auto* npc = a_actor->GetActorBase(); npc) {
					if (auto* spellData = npc->GetSpellList(); spellData && spellData->spells) {
						for (std::uint32_t i = 0; i < spellData->numSpells; ++i)
							if (spellData->spells[i]) baseSpellSet.insert(spellData->spells[i]);
					}
				}
				if (auto* race = a_actor->GetRace(); race) {
					if (auto* spellData = race->actorEffects; spellData && spellData->spells) {
						for (std::uint32_t i = 0; i < spellData->numSpells; ++i)
							if (spellData->spells[i]) baseSpellSet.insert(spellData->spells[i]);
					}
				}

				CollectVisitor v;
				v.out        = &a_out;
				v.actor      = a_actor;
				v.actorID    = actorID;
				v.baseSpells = &baseSpellSet;
				a_actor->VisitSpells(v);

				// Build set of all spell FormIDs currently on the actor (via VisitSpells).
				// Used below to detect spells that were re-learned after being suppressed.
				std::unordered_set<RE::FormID> visitedIDs;
				visitedIDs.reserve(a_out.size());
				for (const auto& e : a_out) visitedIDs.insert(e.spell->GetFormID());

				// Surface suppressed (forgotten) spells so they remain restorable in the UI.
				// If a suppressed spell is now visible via VisitSpells it was re-learned by
				// another means - auto-unsuppress it and skip the duplicate entry.
				SpellSuppressionState::ForEachSuppressed([&](RE::FormID fActor, RE::FormID fSpell) {
					if (fActor != actorID) return;
					if (visitedIDs.count(fSpell)) {
						// Re-learned - clear the stale suppression record.
						SpellSuppressionState::Unsuppress(fActor, fSpell);
						return;
					}
					auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(fSpell);
					if (!spell) return;
					SpellEntry e{};
					e.spell       = spell;
					e.name        = spell->GetFullName();
					if (!e.name || e.name[0] == '\0') e.name = "<unnamed>";
					e.school      = spell->GetAssociatedSkill();
					if (auto* c = spell->GetCostliestEffectItem(RE::MagicSystem::Delivery::kTotal, false))
						if (c->baseEffect) e.tier = c->baseEffect->GetMinimumSkillLevel();
					e.casting     = false;
					e.suppressed  = true;
					e.isBaseSpell = false;  // suppressed entries are always formerly-added spells
					BuildEntryStrings(e);
					a_out.push_back(std::move(e));
				});
			}

			// -- Toggle action -------------------------------------------------------
			// All game-state mutations run on the game thread via AddTask.
			inline void TryToggleSpellSuppression(
				RE::FormID a_actorID, RE::FormID a_spellID, bool a_currentlySuppressed)
			{
				auto* tasks = SKSE::GetTaskInterface();
				if (!tasks) return;

				const bool suppress = !a_currentlySuppressed;
				tasks->AddTask([a_actorID, a_spellID, suppress]() {
					auto* actor = RE::TESForm::LookupByID<RE::Actor>(a_actorID);
					auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(a_spellID);
					if (!actor || !spell) return;

					if (suppress) {
						// Game-thread casting re-check.
						bool isCasting = false;
						for (int s = 0; s <= static_cast<int>(RE::MagicSystem::CastingSource::kOther); ++s) {
							auto* caster = actor->GetMagicCaster(static_cast<RE::MagicSystem::CastingSource>(s));
							if (!caster || !caster->currentSpell) continue;
							const auto st = caster->state.get();
							if ((st == RE::MagicCaster::State::kCasting ||
							     st == RE::MagicCaster::State::kCharging)
								&& caster->currentSpell == spell) {
								isCasting = true;
								break;
							}
						}
						if (isCasting) {
							return;
						}
						if (actor->HasSpell(spell)) actor->RemoveSpell(spell);
						SpellSuppressionState::Suppress(a_actorID, a_spellID);
					} else {
						SpellSuppressionState::Unsuppress(a_actorID, a_spellID);
						if (!actor->HasSpell(spell)) actor->AddSpell(spell);
					}
				});
			}

			// -- Render --------------------------------------------------------------
			// Layout (collapsed):  [accent strip] [Spell Name]  [Tier  /  Status/Cost]
			// Layout (expanded):   Description -> Stats row -> separator -> Metadata -> Button
			inline void Render(RE::FormID a_selectedActorID, float a_scrollChildH)
			{
				ImGuiMCP::ImGui::BeginChild(
					"SpellListScroll",
					ImGuiMCP::ImVec2(0.0f, a_scrollChildH),
					ImGuiMCP::ImGuiChildFlags_None,
					ImGuiMCP::ImGuiWindowFlags_None);

				auto* actor = (a_selectedActorID != 0)
					? RE::TESForm::LookupByID<RE::Actor>(a_selectedActorID)
					: nullptr;

				std::vector<SpellEntry> entries;
				CollectSpells(actor, entries);

				if (entries.empty()) {
					ImGuiMCP::ImGui::Spacing();
					ImGuiMCP::ImGui::TextDisabled("%s", Localization::CStr("ui.spell_list.empty"));
					ImGuiMCP::ImGui::EndChild();
					return;
				}

				// Split entries into base spells (top section) and added/suppressed spells (bottom section).
				std::vector<const SpellEntry*> baseSection;
				std::vector<const SpellEntry*> addedSection;
				for (const auto& e : entries) {
					if (e.isBaseSpell) baseSection.push_back(&e);
					else               addedSection.push_back(&e);
				}

				// Sort base section: school order, then tier, then name.
				auto spellSortCmp = [](const SpellEntry* a, const SpellEntry* b) {
					if (a->school != b->school) {
						// Vanilla schools precede mod-added; within vanilla preserve kVanillaSchools order.
						const bool aV = IsVanillaSchool(a->school);
						const bool bV = IsVanillaSchool(b->school);
						if (aV != bV) return aV > bV;
						if (aV && bV) {
							// Both vanilla - preserve kVanillaSchools index order.
							int ai = 0, bi = 0;
							for (int k = 0; k < static_cast<int>(kVanillaSchools.size()); ++k) {
								if (kVanillaSchools[k] == a->school) ai = k;
								if (kVanillaSchools[k] == b->school) bi = k;
							}
							if (ai != bi) return ai < bi;
						}
					}
					if (a->tier != b->tier) return a->tier < b->tier;
					return std::strcmp(a->name, b->name) < 0;
				};
				std::sort(baseSection.begin(),  baseSection.end(),  spellSortCmp);
				std::sort(addedSection.begin(), addedSection.end(), spellSortCmp);

				// Re-build grouped map (used by left-panel school-card renderer).
				// We keep two separate maps: one for each section.
				auto buildGrouped = [](const std::vector<const SpellEntry*>& src)
					-> std::map<RE::ActorValue, std::vector<const SpellEntry*>>
				{
					std::map<RE::ActorValue, std::vector<const SpellEntry*>> g;
					for (const auto* e : src) g[e->school].push_back(e);
					return g;
				};
				auto makeOrder = [](const std::map<RE::ActorValue, std::vector<const SpellEntry*>>& g)
					-> std::vector<RE::ActorValue>
				{
					std::vector<RE::ActorValue> ord;
					ord.reserve(g.size());
					for (auto v : kVanillaSchools) { if (g.count(v)) ord.push_back(v); }
					for (const auto& [av, _unused2] : g) { if (!IsVanillaSchool(av)) ord.push_back(av); }
					return ord;
				};
				auto groupedBase  = buildGrouped(baseSection);
				auto groupedAdded = buildGrouped(addedSection);
				auto orderBase    = makeOrder(groupedBase);
				auto orderAdded   = makeOrder(groupedAdded);

				// Style values from kSpellListStyle (centralized style knobs).
				const auto& sl = kSpellListStyle;

				// Persistent selection state
				static RE::FormID s_selectedSpellID = 0;
				static RE::FormID s_lastActorID     = 0;
				if (a_selectedActorID != s_lastActorID) {
					s_lastActorID     = a_selectedActorID;
					s_selectedSpellID = 0;
				}
				{
					bool found = false;
					for (const auto& e : entries)
						if (e.spell->GetFormID() == s_selectedSpellID) { found = true; break; }
					if (!found) s_selectedSpellID = 0;
				}

				// LEFT PANEL: compact spell list
				ImGuiMCP::ImVec2 spellViewAvail{};
				ImGuiMCP::ImGui::GetContentRegionAvail(&spellViewAvail);
				const float leftPanelW = (std::max)(spellViewAvail.x - sl.panelGapPx - sl.detailPanelWidthPx, 1.0f);
				ImGuiMCP::ImGui::BeginChild("##spellList",
					ImGuiMCP::ImVec2{ leftPanelW, 0.0f },
					ImGuiMCP::ImGuiChildFlags_None,
					ImGuiMCP::ImGuiWindowFlags_None);

				// Render a section (base or added) as school-grouped cards.
				// sectionIdx: 0 = base spells, 1 = added/suppressed spells.
				// Each section uses a unique card ID prefix to prevent ImGui child-window ID collisions
				// when the same school appears in both sections.
				auto renderSection = [&](
					const std::vector<RE::ActorValue>& sectionOrder,
					std::map<RE::ActorValue, std::vector<const SpellEntry*>>& sectionGrouped,
					int sectionIdx)
				{
					for (auto av : sectionOrder) {
						auto& vec = sectionGrouped[av];
						if (vec.empty()) continue;

						const auto schoolColor = SchoolColor(av);
						const auto& sc = schoolColor;

						// -- School card: school-colored border, neutral background ------
						{
							ImGuiMCP::ImVec2 cardAvail{};
							ImGuiMCP::ImGui::GetContentRegionAvail(&cardAvail);

							ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowPadding,
								ImGuiMCP::ImVec2{ 0.0f, sl.cardPadY });
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ChildBg, sl.schoolCardNeutralBg);
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Border,
								ImGuiMCP::ImVec4{ sc.x * sl.schoolCardBorderFactor, sc.y * sl.schoolCardBorderFactor,
								                 sc.z * sl.schoolCardBorderFactor, sl.schoolCardBorderAlpha });
							ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildRounding,   sl.schoolCardRounding);
							ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildBorderSize, 1.0f);

							char cardId[40];
							std::snprintf(cardId, sizeof(cardId), "##school_%d_%d", sectionIdx, static_cast<int>(av));
							ImGuiMCP::ImGui::BeginChild(cardId,
								ImGuiMCP::ImVec2{ cardAvail.x, 0.0f },
								ImGuiMCP::ImGuiChildFlags_Border |
								ImGuiMCP::ImGuiChildFlags_AutoResizeY |
								ImGuiMCP::ImGuiChildFlags_AlwaysUseWindowPadding,
								ImGuiMCP::ImGuiWindowFlags_NoScrollbar);
						ImGuiMCP::ImGui::PopStyleVar(3);
						ImGuiMCP::ImGui::PopStyleColor(2);

						auto* cardDrawList = ImGuiMCP::ImGui::GetWindowDrawList();

						// Selectable height: explicit size.y (fontSize + 2*rowExtraPadY).
						// Text indent = SetCursorPosX(spellNameLeftPad) per row (WindowPadding.x=0 so clip rect starts at card border).
						// ItemSpacing.y = 0 -> rows adjacent. SelectableTextAlign.y = 0.5 -> text vertically centered.
						const auto* cardSt      = ImGuiMCP::ImGui::GetStyle();
						const float rowH        = ImGuiMCP::ImGui::GetFontSize() + 2.0f * sl.rowExtraPadY;
						const float origItemSpX = cardSt ? cardSt->ItemSpacing.x : 8.0f;
						ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ItemSpacing,
							ImGuiMCP::ImVec2{ origItemSpX, 0.0f });

						// -- Spell rows ------------------------------------------------
						for (const auto* ePtr : vec) {
							const auto& e        = *ePtr;
							const RE::FormID spellID    = e.spell->GetFormID();
							const bool       isSelected = (spellID == s_selectedSpellID);
							ImGuiMCP::ImGui::PushID(static_cast<int>(spellID));
							// Full-width invisible button
							ImGuiMCP::ImVec2 rowAvail{};
							ImGuiMCP::ImGui::GetContentRegionAvail(&rowAvail);
							ImGuiMCP::ImGui::InvisibleButton("##row",
								ImGuiMCP::ImVec2{ rowAvail.x, rowH });
							const bool clicked = ImGuiMCP::ImGui::IsItemClicked();
							const bool hovered = ImGuiMCP::ImGui::IsItemHovered();
							if (clicked) s_selectedSpellID = spellID;

							// -- Draw-list ------------------------------------------------
						{
							ImGuiMCP::ImVec2 rowMin{}, rowMax{};
							ImGuiMCP::ImGui::GetItemRectMin(&rowMin);
							ImGuiMCP::ImGui::GetItemRectMax(&rowMax);

							// Background - school-tinted when selected or hovered
							if (isSelected || hovered) {
								const float bgA = isSelected ? 0.22f : 0.09f;
								const ImGuiMCP::ImVec4 bgCol4{ sc.x * 0.6f, sc.y * 0.6f, sc.z * 0.6f, bgA };
								ImGuiMCP::ImGui::ImDrawListManager::AddRectFilled(cardDrawList,
									rowMin, rowMax,
									ImGuiMCP::ImGui::GetColorU32(bgCol4), 0.0f, 0);
							}

							// Accent strip - school color
							const ImGuiMCP::ImVec4 accentCol = e.suppressed
								? sl.inactiveBorder
								: ImGuiMCP::ImVec4{ sc.x, sc.y, sc.z, 0.90f };
							ImGuiMCP::ImGui::ImDrawListManager::AddRectFilled(cardDrawList,
								ImGuiMCP::ImVec2{ rowMin.x, rowMin.y + sl.accentStripPadY },
								ImGuiMCP::ImVec2{ rowMin.x + sl.accentStripWidthPx, rowMax.y - sl.accentStripPadY },
								ImGuiMCP::ImGui::GetColorU32(accentCol), 0.0f, 0);

							// Spell name
							const ImGuiMCP::ImVec4 textCol4 = e.suppressed ? sl.inactiveText : sl.spellNameCol;
							const float textX = rowMin.x + sl.spellNameLeftPad;
							const float textY = rowMin.y + (rowH - ImGuiMCP::ImGui::GetFontSize()) * 0.5f;
							ImGuiMCP::ImGui::ImDrawListManager::AddText(cardDrawList,
								ImGuiMCP::ImVec2{ textX, textY },
								ImGuiMCP::ImGui::GetColorU32(textCol4),
								e.name);

							// Dot on the right edge - only for suppressed (removed) spells
							if (e.suppressed) {
								const float dotCX = rowMax.x - sl.accentStripWidthPx * 0.5f - sl.suppressedDotRightPad;
								const float dotCY = (rowMin.y + rowMax.y) * 0.5f;
								ImGuiMCP::ImGui::ImDrawListManager::AddCircleFilled(cardDrawList,
									ImGuiMCP::ImVec2{ dotCX, dotCY },
									sl.suppressedDotRadius,
									ImGuiMCP::ImGui::GetColorU32(sl.suppressedDotCol),
									0);
							}
						}

							ImGuiMCP::ImGui::PopID();
						}

						ImGuiMCP::ImGui::PopStyleVar(1); // ItemSpacing
						ImGuiMCP::ImGui::EndChild(); // ##school_N
					}

					if (sl.cardGapPx > 0.0f)
						ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, sl.cardGapPx });
				}
				}; // end renderSection lambda

				// -- BASE SPELLS section (top) ------------------------------------
				if (!baseSection.empty()) {
					renderSection(orderBase, groupedBase, 0);
					// Separator between base and added sections
					if (!addedSection.empty()) {
						ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, sl.baseSepPadTopPx });
						ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Separator, sl.baseSepCol);
						ImGuiMCP::ImGui::Separator();
						ImGuiMCP::ImGui::PopStyleColor();
						ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, sl.baseSepPadBottomPx });
					}
				}

				// -- ADDED / SUPPRESSED SPELLS section ---------------------------
				renderSection(orderAdded, groupedAdded, 1);
				ImGuiMCP::ImGui::EndChild(); // ##spellList

				// RIGHT PANEL: spell detail
				ImGuiMCP::ImGui::SameLine(0.0f, sl.panelGapPx);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ChildBg,
					sl.detailPanelBg);
				ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Border, sl.sepCol);
				ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildRounding,   sl.detailPanelRounding);
				ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildBorderSize, sl.detailPanelBorderSize);
				ImGuiMCP::ImGui::BeginChild("##spellDetail",
					ImGuiMCP::ImVec2{ sl.detailPanelWidthPx, 0.0f },
					ImGuiMCP::ImGuiChildFlags_Border,
					ImGuiMCP::ImGuiWindowFlags_None);
				ImGuiMCP::ImGui::PopStyleVar(2);
				ImGuiMCP::ImGui::PopStyleColor(2);
				if (sl.detailPadTopPx > 0.0f)
					ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, sl.detailPadTopPx });

				// Locate selected entry
				const SpellEntry* selEntry = nullptr;
				if (s_selectedSpellID != 0) {
					for (const auto& e : entries)
						if (e.spell->GetFormID() == s_selectedSpellID) { selEntry = &e; break; }
				}

				if (selEntry == nullptr) {
					// Placeholder
					ImGuiMCP::ImVec2 detailAvail{};
					ImGuiMCP::ImGui::GetContentRegionAvail(&detailAvail);
					const char* hint = Localization::CStr("ui.spell_list.detail.select_hint");
					ImGuiMCP::ImVec2 hintSz{};
					ImGuiMCP::ImGui::CalcTextSize(&hintSz, hint, nullptr, false, 0.0f);
					ImGuiMCP::ImGui::SetCursorPos({
						(detailAvail.x - hintSz.x) * 0.5f,
						(detailAvail.y - hintSz.y) * 0.5f });
					ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, sl.labelCol);
					ImGuiMCP::ImGui::TextUnformatted(hint);
					ImGuiMCP::ImGui::PopStyleColor();
				} else {
					const auto& e   = *selEntry;
					const auto  dsc = SchoolColor(e.school);
					auto* detailDL = ImGuiMCP::ImGui::GetWindowDrawList();
					const float kDetailPad  = sl.detailPadPx;
					const float kSideMargin = sl.detailSideMarginPx;
					// GetContentRegionAvail stops at (Size.x - WindowPadding.x), so the right margin
					// is WindowPadding.x larger than the left unless we compensate.
					const float kWinPadX = [](){ const auto* st = ImGuiMCP::ImGui::GetStyle(); return st ? st->WindowPadding.x : 8.0f; }();

					// Title bar: gradient bg for name only, school outside bg, both ALL CAPS, scaled font, top+bot separators
					{
						const float kBaseH   = ImGuiMCP::ImGui::GetFontSize();
						const float kNameH   = kBaseH * sl.titleFontScale;  // scaled line height
						const float kBandH   = sl.titleLineSize + sl.titleNamePadY
							+ kNameH + sl.titleNamePadY + sl.titleLineSize;  // top-sep + pad + text + pad + bot-sep
						const float kSchoolH = sl.titleSchoolGapPx + kBaseH + sl.titleSchoolPadB;
						const float kTotalH  = kBandH + kSchoolH;
						ImGuiMCP::ImVec2 ts{};
						ImGuiMCP::ImGui::GetCursorScreenPos(&ts);
						ImGuiMCP::ImVec2 ta{};
						ImGuiMCP::ImGui::GetContentRegionAvail(&ta);

						// Gradient background - covers only the name band (between separators)
						const float bgY0 = ts.y + sl.titleLineSize;
						const float bgY1 = ts.y + kBandH - sl.titleLineSize;
						const float midX = ts.x + ta.x * 0.5f;
						const ImGuiMCP::ImVec4& mc = sl.titleBarBgMidCol;
						const ImGuiMCP::ImU32 colMid  = ImGuiMCP::ImGui::GetColorU32(mc);
						const ImGuiMCP::ImU32 colEdge = ImGuiMCP::ImGui::GetColorU32(
							ImGuiMCP::ImVec4{ mc.x, mc.y, mc.z, sl.titleBarBgEdgeAlpha });
						ImGuiMCP::ImGui::ImDrawListManager::AddRectFilledMultiColor(detailDL,
							ImGuiMCP::ImVec2{ ts.x, bgY0 }, ImGuiMCP::ImVec2{ midX, bgY1 },
							colEdge, colMid, colMid, colEdge);
						ImGuiMCP::ImGui::ImDrawListManager::AddRectFilledMultiColor(detailDL,
							ImGuiMCP::ImVec2{ midX, bgY0 }, ImGuiMCP::ImVec2{ ts.x + ta.x, bgY1 },
							colMid, colEdge, colEdge, colMid);

						// Top separator
						ImGuiMCP::ImGui::ImDrawListManager::AddRectFilled(detailDL,
							ImGuiMCP::ImVec2{ ts.x, ts.y },
							ImGuiMCP::ImVec2{ ts.x + ta.x, ts.y + sl.titleLineSize },
							ImGuiMCP::ImGui::GetColorU32(sl.titleTopLineCol), 0.0f, 0);

						// Bottom separator
						ImGuiMCP::ImGui::ImDrawListManager::AddRectFilled(detailDL,
							ImGuiMCP::ImVec2{ ts.x, ts.y + kBandH - sl.titleLineSize },
							ImGuiMCP::ImVec2{ ts.x + ta.x, ts.y + kBandH },
							ImGuiMCP::ImGui::GetColorU32(sl.titleBotLineCol), 0.0f, 0);

						// Spell name - ALL CAPS, scaled, horizontally centered
						std::string nameUpper = e.name;
						for (auto& ch : nameUpper) if (ch >= 'a' && ch <= 'z') ch -= 32;
						auto* curFont = ImGuiMCP::ImGui::GetFont();
						ImGuiMCP::ImVec2 nameSz{};
						ImGuiMCP::ImGui::CalcTextSize(&nameSz, nameUpper.c_str(), nullptr, false, 0.0f);
						const float nameScale = sl.titleFontScale;
						const float nameX = ts.x + (ta.x - nameSz.x * nameScale) * 0.5f;
						const float nameY = ts.y + sl.titleLineSize + sl.titleNamePadY
							+ (kNameH - kBaseH * nameScale) * 0.5f;  // vertically center scaled text
						ImGuiMCP::ImGui::ImDrawListManager::AddText(detailDL,
							curFont, kBaseH * nameScale,
							ImGuiMCP::ImVec2{ nameX, nameY },
							ImGuiMCP::ImGui::GetColorU32(sl.titleTextCol),
							nameUpper.c_str(), nullptr);

						// School label - ALL CAPS, school theme color, below the band
						std::string schoolStr = SchoolDisplayName(e.school);
						for (auto& ch : schoolStr) if (ch >= 'a' && ch <= 'z') ch -= 32;
						ImGuiMCP::ImVec2 schoolSz{};
						ImGuiMCP::ImGui::CalcTextSize(&schoolSz, schoolStr.c_str(), nullptr, false, 0.0f);
						ImGuiMCP::ImGui::ImDrawListManager::AddText(detailDL,
							ImGuiMCP::ImVec2{
								ts.x + (ta.x - schoolSz.x) * 0.5f,
								ts.y + kBandH + sl.titleSchoolGapPx },
							ImGuiMCP::ImGui::GetColorU32(
								ImGuiMCP::ImVec4{ dsc.x, dsc.y, dsc.z, sl.titleSchoolAlpha }),
							schoolStr.c_str(), nullptr);

						ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ ta.x, kTotalH });
					}

					ImGuiMCP::ImGui::SetCursorPosX(kDetailPad + kSideMargin);

					// Description card
					{
						ImGuiMCP::ImVec2 da{};
						ImGuiMCP::ImGui::GetContentRegionAvail(&da);
						const float kCardPad  = sl.descCardPadPx;
						const float kCardW    = da.x - kDetailPad - kSideMargin + kWinPadX;
						const float kWrapW    = (std::max)(kCardW - kCardPad * 2.0f, 10.0f);
						const float kLineH   = ImGuiMCP::ImGui::GetFontSize();

						// Dry-run: word-wrap to measure rendered content height
						float contentH = kLineH;
						{
							if (!e.descSegments.empty()) {
								float cx = 0.0f, cy = 0.0f;
								for (const auto& seg : e.descSegments) {
									if (seg.text == "\n") { cx = 0.0f; cy += kLineH; continue; }
									const char* p   = seg.text.c_str();
									const char* end = p + seg.text.size();
									while (p < end) {
										if (cx == 0.0f) { while (p < end && *p == ' ') ++p; }
										if (p >= end) break;
										const char* wordEnd  = p;
										while (wordEnd < end && *wordEnd != ' ') ++wordEnd;
										const char* spaceEnd = wordEnd;
										while (spaceEnd < end && *spaceEnd == ' ') ++spaceEnd;
										ImGuiMCP::ImVec2 wSz{}, tSz{};
										ImGuiMCP::ImGui::CalcTextSize(&wSz, p, wordEnd,  false, 0.0f);
										ImGuiMCP::ImGui::CalcTextSize(&tSz, p, spaceEnd, false, 0.0f);
										if (cx > 0.0f && cx + wSz.x > kWrapW) { cx = 0.0f; cy += kLineH; }
										cx += tSz.x;
										p = spaceEnd;
									}
								}
								contentH = cy + kLineH;
							}
						}
						const float rawH   = contentH + kCardPad * 2.0f;
						const float kCardH = (std::max)(sl.descCardMinHeightPx,
							(std::min)(rawH, sl.descCardMaxHeightPx));

						ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ChildBg, sl.descCardBgCol);
						ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Border,  sl.descCardBorderCol);
						ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildRounding,   sl.descCardRounding);
						ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildBorderSize, sl.descCardBorderSize);
						ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_WindowPadding,
							ImGuiMCP::ImVec2{ 0.0f, 0.0f });
						ImGuiMCP::ImGui::BeginChild("##detdesc",
							ImGuiMCP::ImVec2{ kCardW, kCardH },
							ImGuiMCP::ImGuiChildFlags_Border,
							ImGuiMCP::ImGuiWindowFlags_NoScrollbar);
						ImGuiMCP::ImGui::PopStyleVar(3);
						ImGuiMCP::ImGui::PopStyleColor(2);

						auto* descDL = ImGuiMCP::ImGui::GetWindowDrawList();
						ImGuiMCP::ImVec2 descBase{};
						ImGuiMCP::ImGui::GetCursorScreenPos(&descBase);
						const float baseX = descBase.x + kCardPad;
						const float baseY = descBase.y + kCardPad;

						// Two-pass horizontally-centered word-wrap renderer
						struct DescWord { const char* b; const char* e; ImGuiMCP::ImVec4 col; float xOff; float w; };
						struct DescLine { std::vector<DescWord> words; float totalW = 0.0f; };
						std::vector<DescLine> descLines;
						descLines.emplace_back();

						if (!e.descSegments.empty()) {
							float cx = 0.0f;
							for (const auto& seg : e.descSegments) {
								if (seg.text == "\n") { cx = 0.0f; descLines.emplace_back(); continue; }
								const char* p   = seg.text.c_str();
								const char* end = p + seg.text.size();
								while (p < end) {
									if (cx == 0.0f) { while (p < end && *p == ' ') ++p; }
									if (p >= end) break;
									const char* wEnd = p; while (wEnd < end && *wEnd != ' ') ++wEnd;
									const char* sEnd = wEnd; while (sEnd < end && *sEnd == ' ') ++sEnd;
									ImGuiMCP::ImVec2 wSz{}, tSz{};
									ImGuiMCP::ImGui::CalcTextSize(&wSz, p, wEnd, false, 0.0f);
									ImGuiMCP::ImGui::CalcTextSize(&tSz, p, sEnd, false, 0.0f);
									if (cx > 0.0f && cx + wSz.x > kWrapW) { cx = 0.0f; descLines.emplace_back(); }
									descLines.back().words.push_back({ p, wEnd, seg.color, cx, wSz.x });
									descLines.back().totalW = cx + wSz.x;
									cx += tSz.x;
									p = sEnd;
								}
							}
							float cy = 0.0f;
							for (const auto& line : descLines) {
								const float lineX = (kWrapW - line.totalW) * 0.5f;
								for (const auto& w : line.words) {
									ImGuiMCP::ImGui::ImDrawListManager::AddText(descDL,
										ImGuiMCP::ImVec2{ baseX + lineX + w.xOff, baseY + cy },
										ImGuiMCP::ImGui::GetColorU32(w.col),
										w.b, w.e);
								}
								cy += kLineH;
							}
						} else {
							ImGuiMCP::ImVec2 dashSz{};
							ImGuiMCP::ImGui::CalcTextSize(&dashSz, "\xe2\x80\x94", nullptr, false, 0.0f);
							ImGuiMCP::ImGui::ImDrawListManager::AddText(descDL,
								ImGuiMCP::ImVec2{ baseX + (kWrapW - dashSz.x) * 0.5f, baseY },
								ImGuiMCP::ImGui::GetColorU32(sl.descTextCol),
								"\xe2\x80\x94", nullptr);
						}
						ImGuiMCP::ImGui::EndChild();
					}

					ImGuiMCP::ImGui::SetCursorPosX(kDetailPad + kSideMargin);

					// Cost + Level row (below description card)
					// Layout: [cost(grey) 250(pale-blue)]  ...  [level(grey) Novice(tier-color)]
					{
						ImGuiMCP::ImVec2 clPos{};
						ImGuiMCP::ImGui::GetCursorScreenPos(&clPos);
						ImGuiMCP::ImVec2 clAvail{};
						ImGuiMCP::ImGui::GetContentRegionAvail(&clAvail);
						const float clW = clAvail.x - kDetailPad - kSideMargin + kWinPadX;
						const float clH = ImGuiMCP::ImGui::GetFontSize();

						// -- Left: cost label (grey) + number (pale-blue) --------------------
						std::string costNum;
						for (const auto& pl : e.statProps) {
							if (pl.label == "Cost") { costNum = pl.value; break; }
						}
						if (!costNum.empty()) {
							std::string costLabel = std::string(Localization::CStr("ui.spell_list.detail.label.cost")) + " ";
							const char* costLabelCStr = costLabel.c_str();
							ImGuiMCP::ImVec2 costLabelSz{};
							ImGuiMCP::ImGui::CalcTextSize(&costLabelSz, costLabelCStr, nullptr, false, 0.0f);
							ImGuiMCP::ImGui::ImDrawListManager::AddText(detailDL,
								ImGuiMCP::ImVec2{ clPos.x, clPos.y },
								ImGuiMCP::ImGui::GetColorU32(sl.labelCol),
								costLabelCStr, nullptr);
							ImGuiMCP::ImGui::ImDrawListManager::AddText(detailDL,
								ImGuiMCP::ImVec2{ clPos.x + costLabelSz.x, clPos.y },
								ImGuiMCP::ImGui::GetColorU32(sl.costColor),
								costNum.c_str(), nullptr);
							if (e.isConcentration) {
								ImGuiMCP::ImVec2 costNumSz{};
								ImGuiMCP::ImGui::CalcTextSize(&costNumSz, costNum.c_str(), nullptr, false, 0.0f);
								std::string perSecLabel = " " + std::string(Localization::CStr("ui.spell_list.detail.label.per_sec"));
								const char* perSecLabelCStr = perSecLabel.c_str();
								const float smallSz = clH * 0.72f;
								// Baseline-align the smaller text: offset down by (clH - smallSz)
								const float perSecY = clPos.y + (clH - smallSz);
								ImGuiMCP::ImGui::ImDrawListManager::AddText(detailDL,
									ImGuiMCP::ImGui::GetFont(), smallSz,
									ImGuiMCP::ImVec2{ clPos.x + costLabelSz.x + costNumSz.x, perSecY },
									ImGuiMCP::ImGui::GetColorU32(sl.perSecSuffixCol),
									perSecLabelCStr, nullptr, 0.0f, nullptr);
							}
						}
						// -- Right: level label (grey) + tier name (tier-color) --------------
						const char* tierName = Localization::CStr(TierLocKey(e.tier));
						if (tierName && tierName[0] != '\0') {
							std::string levelLabel = std::string(Localization::CStr("ui.spell_list.detail.label.level")) + " ";
							const char* levelLabelCStr = levelLabel.c_str();
							ImGuiMCP::ImVec2 levelLabelSz{};
							ImGuiMCP::ImGui::CalcTextSize(&levelLabelSz, levelLabelCStr, nullptr, false, 0.0f);
							ImGuiMCP::ImVec2 tierNameSz{};
							ImGuiMCP::ImGui::CalcTextSize(&tierNameSz, tierName, nullptr, false, 0.0f);
							const float rightX = clPos.x + clW - levelLabelSz.x - tierNameSz.x;
							ImGuiMCP::ImGui::ImDrawListManager::AddText(detailDL,
								ImGuiMCP::ImVec2{ rightX, clPos.y },
								ImGuiMCP::ImGui::GetColorU32(sl.labelCol),
								levelLabelCStr, nullptr);
							ImGuiMCP::ImGui::ImDrawListManager::AddText(detailDL,
								ImGuiMCP::ImVec2{ rightX + levelLabelSz.x, clPos.y },
								ImGuiMCP::ImGui::GetColorU32(TierColor(e.tier)),
								tierName, nullptr);
						}

						ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ clW, clH });
					}

					// -- Bottom-align: push remaining space above stats block --------
					{
						const float lineH = ImGuiMCP::ImGui::GetFontSize();
						// Get actual ItemSpacing.y so we can count every inter-item gap precisely.
						// Between push->SettingsItemGap, SettingsItemGap->table, table->Dummy, Dummy->Sep,
						// Sep->Dummy, Dummy->pluginText = 6 ItemSpacing.y values total.
						const float itemSpY = [](){ const auto* st = ImGuiMCP::ImGui::GetStyle(); return st ? st->ItemSpacing.y : 4.0f; }();
						// Separator ItemSize height = max(3, thickness) = 3 (ImGui default).
						constexpr float kSepItemH = 3.0f;
						int numStatRows = 0;
						for (const auto& pl : e.statProps)
							if (pl.label != "Cost") ++numStatRows;
						// Table row height = lineH + 2 * CellPadding.y (default CellPadding.y = 2).
						const float kCellPadY = [](){ const auto* st = ImGuiMCP::ImGui::GetStyle(); return st ? st->CellPadding.y : 2.0f; }();
						const float kTableHEst = static_cast<float>(numStatRows) * (lineH + 2.0f * kCellPadY);
						// Compute plugin text height accounting for word-wrap.
						ImGuiMCP::ImVec2 crMax{};
						ImGuiMCP::ImGui::GetWindowContentRegionMax(&crMax);
						const float pluginValueWrapW = crMax.x - kDetailPad;
						float pluginTextH = lineH;
						for (const auto& pl : e.infoProps) {
							if (pl.label != "Plugin") continue;
							ImGuiMCP::ImVec2 valueSz{};
							ImGuiMCP::ImGui::CalcTextSize(&valueSz, pl.value.c_str(), nullptr, false,
								pluginValueWrapW > 0.0f ? pluginValueWrapW : 0.0f);
							pluginTextH = valueSz.y;
							break;
						}
						const float kGapPx   = kSpacingStyle.settingsItemGapPx;
						// 6 x itemSpY: push->gap, gap->table, table->sepDummy, sepDummy->sep, sep->pluginPad, pluginPad->pluginText
						const float kBottomH = kGapPx
						                     + 6.0f * itemSpY
						                     + (std::max)(kTableHEst, sl.actionBtnH)
						                     + sl.statsSepGapPx
						                     + kSepItemH
						                     + sl.pluginInfoPadTopPx
						                     + pluginTextH;
						ImGuiMCP::ImVec2 remAvail{};
						ImGuiMCP::ImGui::GetContentRegionAvail(&remAvail);
						const float pushH = remAvail.y - kBottomH;
						if (pushH > 0.0f)
							ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, pushH });
					}

					SettingsItemGap();
					ImGuiMCP::ImGui::SetCursorPosX(kDetailPad);

					// Stats table (left) + Button/warning (right, bottom edge always aligned to table bottom)
					{
						ImGuiMCP::ImVec2 contentAvail{};
						ImGuiMCP::ImGui::GetContentRegionAvail(&contentAvail);
						const float statsTopY = ImGuiMCP::ImGui::GetCursorPosY();

						if (ImGuiMCP::ImGui::BeginTable("##detstats", 2,
							ImGuiMCP::ImGuiTableFlags_SizingFixedFit |
							ImGuiMCP::ImGuiTableFlags_NoPadOuterX)) {
							for (const auto& pl : e.statProps) {
								if (pl.label == "Cost") continue;
								ImGuiMCP::ImGui::TableNextRow();
								ImGuiMCP::ImGui::TableNextColumn();
								ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, sl.statsLabelCol);
								ImGuiMCP::ImGui::TextUnformatted(pl.labelKey ? Localization::CStr(pl.labelKey) : pl.label.c_str());
								ImGuiMCP::ImGui::PopStyleColor();
								ImGuiMCP::ImGui::TableNextColumn();
							if (!pl.value.empty()) {
								if (pl.value[0] == '\x01') {
									// Infinite duration - draw two touching circles (infinity shape)
									auto* dl = ImGuiMCP::ImGui::GetWindowDrawList();
									ImGuiMCP::ImVec2 curPos{};
									ImGuiMCP::ImGui::GetCursorScreenPos(&curPos);
									const float fSz = ImGuiMCP::ImGui::GetFontSize();
									const float r   = fSz * 0.18f;
									const float cy  = curPos.y + fSz * 0.6f;
									const auto col = ImGuiMCP::ImGui::GetColorU32(pl.valueColor);
									ImGuiMCP::ImGui::ImDrawListManager::AddCircle(dl,
										ImGuiMCP::ImVec2{ curPos.x + r, cy }, r, col, 0, 2.5f);
									ImGuiMCP::ImGui::ImDrawListManager::AddCircle(dl,
										ImGuiMCP::ImVec2{ curPos.x + r * 3.0f, cy }, r, col, 0, 2.5f);
									ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ r * 4.0f, fSz });
								} else {
									ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, pl.valueColor);
									ImGuiMCP::ImGui::TextUnformatted(pl.value.c_str());
									ImGuiMCP::ImGui::PopStyleColor();
								}
							} else {
									ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, sl.labelCol);
									ImGuiMCP::ImGui::TextUnformatted(Localization::CStr("ui.spell_list.detail.stat.empty"));
									ImGuiMCP::ImGui::PopStyleColor();
								}
							}
							ImGuiMCP::ImGui::EndTable();
						}

						// Measure actual table height -> bottom-align button exactly
						ImGuiMCP::ImVec2 tableActualSize{};
						ImGuiMCP::ImGui::GetItemRectSize(&tableActualSize);
						const float btnOffY = (tableActualSize.y > sl.actionBtnH)
						                    ? (tableActualSize.y - sl.actionBtnH)
						                    : 0.0f;

						// Button / casting warning - right of stats, bottom edge = table bottom
						ImGuiMCP::ImGui::SameLine();
						const float wBtn = e.casting ? sl.warnWidthPx
						                 : (e.isBaseSpell ? sl.baseBadgeWidthPx : sl.actionBtnW);
						ImGuiMCP::ImGui::SetCursorPosX(kDetailPad + contentAvail.x - wBtn);
						ImGuiMCP::ImGui::SetCursorPosY(statsTopY + btnOffY);
						if (e.casting) {
							// CASTING badge - shows label, tooltip shows the warning text
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ChildBg,  sl.warnBg);
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Border,   sl.warnBorder);
							ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildRounding,   sl.warnRounding);
							ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildBorderSize, sl.warnBorderSize);
							ImGuiMCP::ImGui::BeginChild("##detwarn",
								ImGuiMCP::ImVec2{ sl.warnWidthPx, sl.warnHeightPx },
								ImGuiMCP::ImGuiChildFlags_Border,
								ImGuiMCP::ImGuiWindowFlags_NoScrollbar);
							ImGuiMCP::ImGui::SetCursorPosY(
								(sl.warnHeightPx - ImGuiMCP::ImGui::GetTextLineHeight()) * 0.5f);
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, sl.warnText);
							{
								const char* castingLabel = Localization::CStr("ui.spell_list.toggle.casting");
								ImGuiMCP::ImVec2 castingLabelSize{};
								ImGuiMCP::ImGui::CalcTextSize(&castingLabelSize, castingLabel, nullptr, false, -1.0f);
								ImGuiMCP::ImGui::SetCursorPosX((sl.warnWidthPx - castingLabelSize.x) * 0.5f);
								ImGuiMCP::ImGui::TextUnformatted(castingLabel);
							}
							ImGuiMCP::ImGui::PopStyleColor();
							ImGuiMCP::ImGui::EndChild();
							ImGuiMCP::ImGui::PopStyleVar(2);
							ImGuiMCP::ImGui::PopStyleColor(2);
							// Tooltip: explain why it cannot be removed while equipped
							if (ImGuiMCP::ImGui::IsItemHovered()) {
								ImGuiMCP::ImGui::BeginTooltip();
								ImGuiMCP::ImGui::PushTextWrapPos(
									ImGuiMCP::ImGui::GetFontSize() * kTooltipStyle.wrapWidthMultiplier);
								ImGuiMCP::ImGui::TextUnformatted(
									Localization::CStr("ui.spell_list.toggle.casting_warning"));
								ImGuiMCP::ImGui::PopTextWrapPos();
								ImGuiMCP::ImGui::EndTooltip();
							}
						} else if (e.isBaseSpell) {
							// BASE SPELL badge - informational only, cannot be removed.
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ChildBg, sl.baseBadgeBg);
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Border,  sl.baseBadgeBorder);
							ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildRounding,   sl.baseBadgeRounding);
							ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ChildBorderSize, sl.baseBadgeBorderSize);
							ImGuiMCP::ImGui::BeginChild("##detbase",
								ImGuiMCP::ImVec2{ sl.baseBadgeWidthPx, sl.baseBadgeHeightPx },
								ImGuiMCP::ImGuiChildFlags_Border,
								ImGuiMCP::ImGuiWindowFlags_NoScrollbar);
							ImGuiMCP::ImGui::SetCursorPosY(
								(sl.baseBadgeHeightPx - ImGuiMCP::ImGui::GetTextLineHeight()) * 0.5f);
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, sl.baseBadgeText);
							{
								const char* baseLabel = Localization::CStr("ui.spell_list.toggle.base_spell");
								ImGuiMCP::ImVec2 baseLabelSize{};
								ImGuiMCP::ImGui::CalcTextSize(&baseLabelSize, baseLabel, nullptr, false, -1.0f);
								ImGuiMCP::ImGui::SetCursorPosX((sl.baseBadgeWidthPx - baseLabelSize.x) * 0.5f);
								ImGuiMCP::ImGui::TextUnformatted(baseLabel);
							}
							ImGuiMCP::ImGui::PopStyleColor();
							ImGuiMCP::ImGui::EndChild();
							ImGuiMCP::ImGui::PopStyleVar(2);
							ImGuiMCP::ImGui::PopStyleColor(2);
							// Tooltip explaining why base spells cannot be removed
							if (ImGuiMCP::ImGui::IsItemHovered()) {
								ImGuiMCP::ImGui::BeginTooltip();
								ImGuiMCP::ImGui::PushTextWrapPos(
									ImGuiMCP::ImGui::GetFontSize() * kTooltipStyle.wrapWidthMultiplier);
								ImGuiMCP::ImGui::TextUnformatted(
									Localization::CStr("ui.spell_list.toggle.base_spell_tooltip"));
								ImGuiMCP::ImGui::PopTextWrapPos();
								ImGuiMCP::ImGui::EndTooltip();
							}
						} else {
							const bool isSuppressed = e.suppressed;
							const char* btnLabel = isSuppressed ? Localization::CStr("ui.spell_list.toggle.unsuppress") : Localization::CStr("ui.spell_list.toggle.suppress");
							const ImGuiMCP::ImVec4 btnBg = isSuppressed
								? ImGuiMCP::ImVec4{ dsc.x*sl.unsuppressBgFactor, dsc.y*sl.unsuppressBgFactor, dsc.z*sl.unsuppressBgFactor, sl.unsuppressBgAlpha }
								: sl.suppressBg;
							const ImGuiMCP::ImVec4 btnHov = isSuppressed
								? ImGuiMCP::ImVec4{ dsc.x*sl.unsuppressHovFactor, dsc.y*sl.unsuppressHovFactor, dsc.z*sl.unsuppressHovFactor, sl.unsuppressHovAlpha }
								: sl.suppressHov;
							const ImGuiMCP::ImVec4 btnPrs = isSuppressed
								? ImGuiMCP::ImVec4{ dsc.x*sl.unsuppressPrsFactor, dsc.y*sl.unsuppressPrsFactor, dsc.z*sl.unsuppressPrsFactor, sl.unsuppressPrsAlpha }
								: sl.suppressPrs;
							const ImGuiMCP::ImVec4 btnBorder = isSuppressed
								? ImGuiMCP::ImVec4{ dsc.x*sl.unsuppressBorderFactor, dsc.y*sl.unsuppressBorderFactor, dsc.z*sl.unsuppressBorderFactor, sl.unsuppressBorderAlpha }
								: sl.suppressBorder;
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button,        btnBg);
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, btnHov);
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive,  btnPrs);
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Border,        btnBorder);
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, isSuppressed ? sl.unsuppressBtnTextCol : sl.suppressBtnTextCol);
							ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameRounding,   sl.actionBtnRounding);
							ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, sl.actionBtnBorderSize);
							if (ImGuiMCP::ImGui::Button(btnLabel,
								ImGuiMCP::ImVec2{ sl.actionBtnW, sl.actionBtnH })) {
								TryToggleSpellSuppression(
									a_selectedActorID, e.spell->GetFormID(), isSuppressed);
							}
							// Tooltip: explain what REMOVE / RESTORE will do
							if (ImGuiMCP::ImGui::IsItemHovered()) {
								ImGuiMCP::ImGui::BeginTooltip();
								ImGuiMCP::ImGui::PushTextWrapPos(
									ImGuiMCP::ImGui::GetFontSize() * kTooltipStyle.wrapWidthMultiplier);
								ImGuiMCP::ImGui::TextUnformatted(Localization::CStr(
									isSuppressed
										? "ui.spell_list.toggle.unsuppress_tooltip"
										: "ui.spell_list.toggle.suppress_tooltip"));
								ImGuiMCP::ImGui::PopTextWrapPos();
								ImGuiMCP::ImGui::EndTooltip();
							}
							ImGuiMCP::ImGui::PopStyleVar(2);
							ImGuiMCP::ImGui::PopStyleColor(5);
						}
					}

					ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, sl.statsSepGapPx });
					ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Separator, sl.sepCol);
					ImGuiMCP::ImGui::Separator();
					ImGuiMCP::ImGui::PopStyleColor();

					// Plugin info - below separator (ItemSpacing suppressed so padding is exact)
					{
						ImGuiMCP::ImVec2 sepPos{};
						ImGuiMCP::ImGui::GetCursorScreenPos(&sepPos);
						ImGuiMCP::ImGui::SetCursorScreenPos(
							ImGuiMCP::ImVec2{ sepPos.x, sepPos.y + sl.pluginInfoPadTopPx });
						ImGuiMCP::ImGui::SetCursorPosX(kDetailPad);
						for (const auto& pl : e.infoProps) {
							if (pl.label != "Plugin") continue;
							ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, sl.metaCol);
							ImGuiMCP::ImGui::PushTextWrapPos(0.0f);
							ImGuiMCP::ImGui::TextUnformatted(pl.value.c_str());
							ImGuiMCP::ImGui::PopTextWrapPos();
							ImGuiMCP::ImGui::PopStyleColor();
							break;
						}
					}
				}
				if (sl.detailPadBottomPx > 0.0f)
					ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, sl.detailPadBottomPx });

				ImGuiMCP::ImGui::EndChild(); // ##spellDetail
				ImGuiMCP::ImGui::EndChild(); // SpellListScroll
			}
		}

		// -- 10c. Actor Management utilities ------------------------------------------
		// Used exclusively by RenderPreferenceCapture and SpellListView::Render.
		// -------------------------------------------------------------------------

		[[nodiscard]] std::string Hex8(RE::FormID id)
		{
			char buf[16]{};
			std::snprintf(buf, sizeof(buf), "%08X", id);
			return std::string(buf);
		}

		[[nodiscard]] const char* CEPCategoryName(CombatEquipPreference::Category cat)
		{
			using Cat = CombatEquipPreference::Category;
			switch (cat) {
			case Cat::kOneHandRight:
				return Localization::CStr("ui.preference_capture.category.one_hand_right");
			case Cat::kOneHandLeft:
				return Localization::CStr("ui.preference_capture.category.one_hand_left");
			case Cat::kShieldLeft:
				return Localization::CStr("ui.preference_capture.category.shield_left");
			case Cat::kTwoHand:
				return Localization::CStr("ui.preference_capture.category.two_hand");
			case Cat::kBow:
				return Localization::CStr("ui.preference_capture.category.bow");
			case Cat::kArrow:
				return Localization::CStr("ui.preference_capture.category.arrow");
			case Cat::kCrossbow:
				return Localization::CStr("ui.preference_capture.category.crossbow");
			case Cat::kBolt:
				return Localization::CStr("ui.preference_capture.category.bolt");
			case Cat::kStaffRight:
				return Localization::CStr("ui.preference_capture.category.staff_right");
			case Cat::kStaffLeft:
				return Localization::CStr("ui.preference_capture.category.staff_left");
			case Cat::kScrollRight:
				return Localization::CStr("ui.preference_capture.category.scroll_right");
			case Cat::kScrollLeft:
				return Localization::CStr("ui.preference_capture.category.scroll_left");
			case Cat::kScrollBoth:
				return Localization::CStr("ui.preference_capture.category.scroll_both");
			default:
				return Localization::CStr("ui.preference_capture.category.unknown");
			}
		}

		using CEPCat = CombatEquipPreference::Category;
		constexpr std::size_t kCEPCategoryCount = static_cast<std::size_t>(CEPCat::kTotal);

		[[nodiscard]] std::vector<std::vector<std::size_t>> BuildCEPCategorySets()
		{
			const auto idx = [](CEPCat c) {
				return static_cast<std::size_t>(c);
			};
			// Fully manual set definitions (UI grouping only).
			std::vector<std::vector<std::size_t>> sets = {
				{ idx(CEPCat::kOneHandRight), idx(CEPCat::kOneHandLeft), idx(CEPCat::kShieldLeft), idx(CEPCat::kTwoHand) },
				{ idx(CEPCat::kBow), idx(CEPCat::kCrossbow) },
				{ idx(CEPCat::kArrow), idx(CEPCat::kBolt) },
				{ idx(CEPCat::kStaffRight), idx(CEPCat::kStaffLeft) },
				{ idx(CEPCat::kScrollRight), idx(CEPCat::kScrollLeft), idx(CEPCat::kScrollBoth) },
			};
			return sets;
		}

		[[nodiscard]] std::string DescribeTESFormNameOnly(RE::FormID formID)
		{
			if (formID == 0) {
				return "<none>";
			}
			if (auto* form = RE::TESForm::LookupByID(formID)) {
				if (auto* refr = form->As<RE::TESObjectREFR>()) {
					if (const auto* full = refr->GetDisplayFullName(); full && full[0] != '\0') {
						return full;
					}
					if (const auto* base = refr->GetBaseObject()) {
						if (const auto* baseName = base->GetName(); baseName && baseName[0] != '\0') {
							return baseName;
						}
					}
				}
				const auto name = form->GetName();
				if (name && name[0] != '\0') {
					return name;
				}
			}
			return "<unnamed>";
		}

		// Resolves the in-game temper quality label from GMST settings.
		// Uses the same formula as the engine:
		//   level = round((factor - 1.0) * 10.0); level < 1 means not tempered.
		// Vanilla defines sHealthDataPrefix{Weap|Armo}1..6.
		// An ESP mod could theoretically register indices beyond 6, so we try the
		// exact level first and fall back to 6 (vanilla Legendary cap) if not found.
		// NOTE: ImprovementNamesCustomizedSSE does NOT add new GMSTs; it hooks the
		// display function at render time, so its custom names are invisible to us.
		[[nodiscard]] std::string DescribeTemperQuality(RE::FormID baseObjectID, float temperFactor)
		{
			const float fLevel = std::roundf((temperFactor - 1.0f) * 10.0f);
			if (fLevel < 1.0f) {
				return {};
			}
			const auto level = static_cast<std::uint32_t>(fLevel);

			// Determine weapon vs armor for the correct GMST set.
			bool isWeapon = false;
			if (baseObjectID != 0) {
				if (auto* form = RE::TESForm::LookupByID(baseObjectID)) {
					isWeapon = form->Is(RE::FormType::Weapon);
				}
			}

			auto* gmstCollection = RE::GameSettingCollection::GetSingleton();
			if (!gmstCollection) {
				return {};
			}

			const char* prefix = isWeapon ? "Weap" : "Armo";

			// Try exact level first (supports mods that add GMST indices beyond 6),
			// then fall back to 6 (vanilla Legendary cap).
			char gmstName[32]{};
			std::snprintf(gmstName, sizeof(gmstName), "sHealthDataPrefix%s%u", prefix, level);
			auto* setting = gmstCollection->GetSetting(gmstName);
			if (!setting && level > 6u) {
				std::snprintf(gmstName, sizeof(gmstName), "sHealthDataPrefix%s6", prefix);
				setting = gmstCollection->GetSetting(gmstName);
			}
			if (!setting) {
				return {};
			}
			const char* str = setting->GetString();
			if (!str || str[0] == '\0') {
				return {};
			}

			// GMST strings typically have a trailing space (e.g. "Fine "); trim it.
			std::string label = str;
			while (!label.empty() && std::isspace(static_cast<unsigned char>(label.back()))) {
				label.pop_back();
			}
			return label;
		}

		[[nodiscard]] bool BaseFormHasEnchantment(RE::FormID baseObjectID)
		{
			if (baseObjectID == 0) {
				return false;
			}
			auto* form = RE::TESForm::LookupByID(baseObjectID);
			if (!form) {
				return false;
			}
			auto* enchantable = form->As<RE::TESEnchantableForm>();
			return enchantable && enchantable->formEnchanting;
		}

		// Collects individual magic effect names from an EnchantmentItem.
		// Returns one string per effect (e.g. "Fire Damage", "Frost Damage").
		[[nodiscard]] std::vector<std::string> DescribeEnchantmentEffects(const RE::EnchantmentItem* enchantment)
		{
			std::vector<std::string> names;
			if (!enchantment) {
				return names;
			}
			for (const auto* effect : enchantment->effects) {
				if (effect && effect->baseEffect) {
					const char* name = effect->baseEffect->GetName();
					if (name && name[0] != '\0') {
						names.emplace_back(name);
					}
				}
			}
			return names;
		}

		// Resolves the EnchantmentItem for an item, checking ExtraEnchantment first, then the base form.
		[[nodiscard]] RE::EnchantmentItem* ResolveEnchantment(RE::FormID enchantmentFormID, bool hasExtra, RE::FormID baseObjectID)
		{
			if (hasExtra && enchantmentFormID != 0) {
				if (auto* form = RE::TESForm::LookupByID(enchantmentFormID)) {
					return form->As<RE::EnchantmentItem>();
				}
			}
			if (baseObjectID != 0) {
				if (auto* form = RE::TESForm::LookupByID(baseObjectID)) {
					if (auto* enchantable = form->As<RE::TESEnchantableForm>()) {
						return enchantable->formEnchanting;
					}
				}
			}
			return nullptr;
		}

		[[nodiscard]] std::vector<std::string> BuildSignatureLines(RE::FormID baseObjectID, const InstanceSignature& sig)
		{
			std::vector<std::string> out;
			if (!sig.IsMeaningful() && !BaseFormHasEnchantment(baseObjectID)) {
				return out;
			}

			if (sig.hasCustomName) {
				out.emplace_back(std::vformat(
					Localization::Get("ui.preference_capture.signature.custom_name"),
					std::make_format_args(sig.customName)));
			}

			if (sig.hasTemperFactor) {
				auto quality = DescribeTemperQuality(baseObjectID, sig.temperFactor);
				if (!quality.empty()) {
					out.emplace_back(std::vformat(
						Localization::Get("ui.preference_capture.signature.temper_quality"),
						std::make_format_args(quality)));
				}
			}

			// Enchantment: list individual magic effect names.
			// Works for both player-enchanted (ExtraEnchantment) and base-enchanted items.
			if (auto* ench = ResolveEnchantment(sig.enchantmentFormID, sig.hasEnchantmentExtra, baseObjectID)) {
				auto effectNames = DescribeEnchantmentEffects(ench);
				for (auto& name : effectNames) {
					out.emplace_back(std::vformat(
						Localization::Get("ui.preference_capture.signature.enchantment"),
						std::make_format_args(name)));
				}
			}

			// Charge capacity: player-enchanted from ExtraEnchantment, base-enchanted from the form.
			if (sig.hasEnchantmentCharge) {
				auto chargeStr = std::to_string(sig.enchantmentCharge);
				out.emplace_back(std::vformat(
					Localization::Get("ui.preference_capture.signature.charge_capacity"),
					std::make_format_args(chargeStr)));
			} else if (!sig.hasEnchantmentExtra && baseObjectID != 0) {
				if (auto* form = RE::TESForm::LookupByID(baseObjectID)) {
					if (auto* enchantable = form->As<RE::TESEnchantableForm>()) {
						if (enchantable->formEnchanting && enchantable->amountofEnchantment > 0) {
							auto chargeStr = std::to_string(enchantable->amountofEnchantment);
							out.emplace_back(std::vformat(
								Localization::Get("ui.preference_capture.signature.charge_capacity"),
								std::make_format_args(chargeStr)));
						}
					}
				}
			}

			// Poison, item charge, and item health are intentionally not displayed.
			// Preference Capture records a snapshot at capture time and does not track
			// subsequent changes. These values are volatile, so showing stale data would be misleading.

			return out;
		}

		// Shared helper for rendering "set frame" groups inside preference-style tables.
		// Both the actor-preference table and the cross-clear table use this to avoid
		// duplicating the top/bottom padding, coordinate tracking, cell helpers, and
		// rounded-rect frame drawing.
		struct SetFrameHelper
		{
			float x0, x1;
			float y0 = 0.0f, y1 = 0.0f;
			float minRowHeight = 0.0f;
			ImGuiMCP::ImVec2 baseCellPadding{};
			ImGuiMCP::ImDrawList* drawList = nullptr;
			ImGuiMCP::ImU32 borderCol = 0;

			void updateY1()
			{
				ImGuiMCP::ImVec2 itemMax{};
				ImGuiMCP::ImGui::GetItemRectMax(&itemMax);
				if (itemMax.y > y1) y1 = itemMax.y;
			}

			void cellIndent()
			{
				ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ kPrefCaptureTableStyle.setInnerPadX, 0.0f });
				ImGuiMCP::ImGui::SameLine(0.0f, 0.0f);
			}

			void cellText(const char* s)
			{
				cellIndent();
				ImGuiMCP::ImGui::TextUnformatted(s);
				updateY1();
			}

			void cellNone()
			{
				cellIndent();
				ImGuiMCP::ImGui::TextDisabled("%s", Localization::CStr("ui.preference_capture.none"));
				updateY1();
			}

			void beginSetFrame()
			{
				ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_CellPadding, baseCellPadding);
				ImGuiMCP::ImGui::TableNextRow(0, kPrefCaptureTableStyle.setInnerPadY);
				ImGuiMCP::ImGui::TableSetColumnIndex(0);
				ImGuiMCP::ImVec2 p0{};
				ImGuiMCP::ImGui::GetCursorScreenPos(&p0);
				y0 = p0.y;
				y1 = y0;
				ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, kPrefCaptureTableStyle.setInnerPadY });
				updateY1();
				ImGuiMCP::ImGui::PopStyleVar();
			}

			void endSetFrame()
			{
				ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_CellPadding, baseCellPadding);
				ImGuiMCP::ImGui::TableNextRow(0, kPrefCaptureTableStyle.setInnerPadY);
				ImGuiMCP::ImGui::TableSetColumnIndex(0);
				ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, kPrefCaptureTableStyle.setInnerPadY });
				updateY1();
				ImGuiMCP::ImGui::PopStyleVar();

				ImGuiMCP::ImGui::ImDrawListManager::AddRect(
					drawList,
					ImGuiMCP::ImVec2{ x0, y0 },
					ImGuiMCP::ImVec2{ x1, y1 },
					borderCol,
					kPrefCaptureTableStyle.setFrameRounding,
					ImGuiMCP::ImDrawFlags_RoundCornersAll,
					kPrefCaptureTableStyle.setBorderThickness);
			}

			// Initialise from current ImGui context.  Call BEFORE PushPaddedCellStyle()
			// so that baseCellPadding captures the original (un-padded) value.
			static SetFrameHelper Make(float tableLeftX, float tableRightX)
			{
				const auto base = ImGuiMCP::ImGui::GetStyle()->CellPadding;
				const auto padded = ImGuiMCP::ImVec2{ base.x, base.y + kPrefCaptureTableStyle.rowExtraCellPaddingY };
				float rowH = ImGuiMCP::ImGui::GetFontSize() + (padded.y * 2.0f) + kPrefCaptureTableStyle.rowExtraMinHeightPx;
				rowH = (std::max)(rowH, 1.0f);

				SetFrameHelper h{};
				h.x0 = tableLeftX;
				h.x1 = tableRightX;
				h.minRowHeight = rowH;
				h.baseCellPadding = base;
				h.drawList = ImGuiMCP::ImGui::GetWindowDrawList();
				h.borderCol = ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImGuiCol_Border);
				return h;
			}

			// Push the padded CellPadding style.  Caller must PopStyleVar after the table.
			// MUST be called AFTER Make() so Make captures the un-padded base.
			static void PushPaddedCellStyle()
			{
				const auto base = ImGuiMCP::ImGui::GetStyle()->CellPadding;
				ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_CellPadding,
					ImGuiMCP::ImVec2{ base.x, base.y + kPrefCaptureTableStyle.rowExtraCellPaddingY });
			}
		};

		// Center the next single-line text item horizontally.
		// contentW must be the current GetContentRegionAvail().x.
		static void CenterText(const char* a_text, float a_contentW)
		{
			ImGuiMCP::ImVec2 sz{};
			ImGuiMCP::ImGui::CalcTextSize(&sz, a_text, nullptr, false, 0.0f);
			const float offset = (a_contentW - sz.x) * 0.5f;
			if (offset > 0.0f) ImGuiMCP::ImGui::SetCursorPosX(ImGuiMCP::ImGui::GetCursorPosX() + offset);
		}

		// Count the number of display lines the word-wrap algorithm would produce
		// for the given text at the given wrap width. Used to pre-compute body
		// text height for vertical centering inside confirmation modals.
		static int MeasureWrappedLines(const char* a_text, float a_wrapW)
		{
			int count = 0;
			const char* p = a_text;
			while (*p) {
				if (*p == '\n') { ++count; ++p; continue; }
				const char* lineStart = p;
				const char* lineEnd   = p;
				bool firstToken = true;
				while (*p && *p != '\n') {
					const char* wStart = p;
					while (*p && *p != ' ' && *p != '\n') ++p;
					const char* wEnd = p;
					while (*p == ' ') ++p;
					ImGuiMCP::ImVec2 sz{};
					ImGuiMCP::ImGui::CalcTextSize(&sz, lineStart, wEnd, false, 0.0f);
					if (!firstToken && sz.x > a_wrapW) { p = wStart; break; }
					lineEnd    = wEnd;
					firstToken = false;
				}
				if (lineEnd > lineStart) ++count;
				if (*p == '\n') ++p;
			}
			return count;
		}

		// Render multi-line body text with each line individually centered.
		// a_bodyPadX adds equal left/right margins, narrowing the wrap column.
		static void CenterWrappedText(const char* a_text, float a_contentW, float a_bodyPadX)
		{
			const float wrapW   = a_contentW - a_bodyPadX * 2.0f;
			const float padX    = a_bodyPadX;
			const char* p       = a_text;
			while (*p) {
				if (*p == '\n') {
					ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, ImGuiMCP::ImGui::GetFontSize() });
					++p;
					continue;
				}
				const char* lineStart = p;
				const char* lineEnd   = p;
				bool firstToken = true;
				while (*p && *p != '\n') {
					const char* wStart = p;
					while (*p && *p != ' ' && *p != '\n') ++p;
					const char* wEnd = p;
					while (*p == ' ') ++p;
					ImGuiMCP::ImVec2 sz{};
					ImGuiMCP::ImGui::CalcTextSize(&sz, lineStart, wEnd, false, 0.0f);
					if (!firstToken && sz.x > wrapW) {
						p = wStart;
						break;
					}
					lineEnd    = wEnd;
					firstToken = false;
				}
				if (lineEnd > lineStart) {
					ImGuiMCP::ImVec2 sz{};
					ImGuiMCP::ImGui::CalcTextSize(&sz, lineStart, lineEnd, false, 0.0f);
					const float off = padX + (wrapW - sz.x) * 0.5f;
					if (off > 0.0f) ImGuiMCP::ImGui::SetCursorPosX(ImGuiMCP::ImGui::GetCursorPosX() + off);
					ImGuiMCP::ImGui::TextUnformatted(lineStart, lineEnd);
				}
				if (*p == '\n') ++p;
			}
		}

		// Shared layout for the two actor confirmation modals (Exclude and Remove).
		// Positions and sizes the popup window, renders the actor name header, body
		// text with vertical centering, the Cancel button, then delegates the confirm
		// button to the caller via a_renderConfirmButton(btnW, btnH).
		template <class ConfirmFn>
		static void RenderActorConfirmModal(
			const char* a_popupId,
			const char* a_bodyText,
			RE::FormID& a_actionTargetID,
			ConfirmFn&& a_renderConfirmButton)
		{
			{
				ImGuiMCP::ImVec2 wPos{}; ImGuiMCP::ImGui::GetWindowPos(&wPos);
				ImGuiMCP::ImVec2 wSz{};  ImGuiMCP::ImGui::GetWindowSize(&wSz);
				ImGuiMCP::ImGui::SetNextWindowPos(
					ImGuiMCP::ImVec2{ wPos.x + wSz.x * 0.5f, wPos.y + wSz.y * 0.5f },
					ImGuiMCP::ImGuiCond_Always, ImGuiMCP::ImVec2{ 0.5f, 0.5f });
				ImGuiMCP::ImGui::SetNextWindowSizeConstraints(
					ImGuiMCP::ImVec2{ kActorConfirmModalStyle.width, kActorConfirmModalStyle.height },
					ImGuiMCP::ImVec2{ kActorConfirmModalStyle.width, FLT_MAX });
			}
			const auto flags = ImGuiMCP::ImGuiWindowFlags_NoTitleBar
				| ImGuiMCP::ImGuiWindowFlags_NoResize
				| ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize;
			if (!ImGuiMCP::ImGui::BeginPopupModal(a_popupId, nullptr, flags))
				return;

			if (a_actionTargetID == 0) {
				ImGuiMCP::ImGui::CloseCurrentPopup();
				ImGuiMCP::ImGui::EndPopup();
				return;
			}

			ImGuiMCP::ImVec2 avail{};
			ImGuiMCP::ImGui::GetContentRegionAvail(&avail);
			const float cW           = avail.x;
			const auto* st           = ImGuiMCP::ImGui::GetStyle();
			const float fontSize     = ImGuiMCP::ImGui::GetFontSize();
			const float framePadY    = st ? st->FramePadding.y  : 3.0f;
			const float itemSpacingY = st ? st->ItemSpacing.y   : 4.0f;
			const float itemSpacingX = st ? st->ItemSpacing.x   : 8.0f;
			const float winPadY      = st ? st->WindowPadding.y : 8.0f;
			const float btnW         = cW * kActorConfirmModalStyle.buttonWidthFrac;
			const float btnH_actual  = (kActorConfirmModalStyle.buttonHeightPx > 0.0f)
				? kActorConfirmModalStyle.buttonHeightPx
				: fontSize + framePadY * 2.0f;
			const float winH         = ImGuiMCP::ImGui::GetWindowHeight();
			const float contentH     = winH - 2.0f * winPadY;
			const float btnTargetY   = contentH - kActorConfirmModalStyle.buttonPadBotPx - btnH_actual;
			const auto  actorName    = DescribeTESFormNameOnly(a_actionTargetID);

			// Header: actor name, vertically padded and horizontally centered.
			ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, kActorConfirmModalStyle.headerPadTopPx });
			CenterText(actorName.c_str(), cW);
			ImGuiMCP::ImGui::TextUnformatted(actorName.c_str());
			ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, kActorConfirmModalStyle.headerPadBotPx });
			ImGuiMCP::ImGui::Separator();

			// Body text: vertically centered in the slot between separator and button row.
			const float afterSepY  = ImGuiMCP::ImGui::GetCursorPosY();
			const float bodySlotH  = (btnTargetY - afterSepY) - kActorConfirmModalStyle.bodyPadTopPx;
			const float wrapW      = (cW - kActorConfirmModalStyle.bodyTextPadX * 2.0f > 0.0f)
				? cW - kActorConfirmModalStyle.bodyTextPadX * 2.0f : cW;
			const int   lineCount  = MeasureWrappedLines(a_bodyText, wrapW);
			const float bodyTextH  = lineCount > 0
				? static_cast<float>(lineCount) * fontSize
				  + static_cast<float>(lineCount - 1) * itemSpacingY
				: 0.0f;
			const float centerOff  = (bodySlotH > bodyTextH) ? (bodySlotH - bodyTextH) * 0.5f : 0.0f;
			ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, kActorConfirmModalStyle.bodyPadTopPx + centerOff });
			CenterWrappedText(a_bodyText, cW, kActorConfirmModalStyle.bodyTextPadX);

			// Jump cursor to button position (only forward).
			if (ImGuiMCP::ImGui::GetCursorPosY() < btnTargetY) {
				ImGuiMCP::ImGui::SetCursorPosY(btnTargetY);
			}

			// Buttons: Cancel (fixed) + confirm (caller-defined), centered as a pair.
			{
				const float totalW = btnW * 2.0f + itemSpacingX;
				ImGuiMCP::ImGui::SetCursorPosX(ImGuiMCP::ImGui::GetCursorPosX() + (cW - totalW) * 0.5f);
				if (ImGuiMCP::ImGui::Button(Localization::CStr("ui.common.cancel"),
						ImGuiMCP::ImVec2{ btnW, btnH_actual })) {
					a_actionTargetID = 0;
					ImGuiMCP::ImGui::CloseCurrentPopup();
				}
				ImGuiMCP::ImGui::SameLine();
				a_renderConfirmButton(btnW, btnH_actual);
			}
			ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, kActorConfirmModalStyle.buttonPadBotPx });

			ImGuiMCP::ImGui::EndPopup();
		}

		// -- 10d. RenderPreferenceCapture ------------------------------------------
		// Actor tabs, view toggle, preferences table, SpellListView dispatch, modals.
		// -------------------------------------------------------------------------
		void RenderPreferenceCapture(float reservedBottomPx, int* viewMode)
		{
			constexpr float kCategoryColWidthPx = kPrefCaptureTableStyle.categoryColWidthPx;

			// Capture the top of this region (below separator+ToolbarGap) for modal sizing.
			// Subtract toolbarGapPx so the modal top reaches the separator, not after the gap.
			ImGuiMCP::ImVec2 pageCaptureStart{};
			ImGuiMCP::ImGui::GetCursorScreenPos(&pageCaptureStart);
			pageCaptureStart.y -= kSpacingStyle.toolbarGapPx;

			const auto snapshot = CombatEquipPreference::SnapshotEntries();

			using EntryOpt = std::optional<CombatEquipPreference::Entry>;
			using EntryArray = std::array<EntryOpt, kCEPCategoryCount>;
			std::unordered_map<RE::FormID, EntryArray> byActor;
			byActor.reserve(snapshot.size());
			for (const auto& se : snapshot) {
				auto& arr = byActor[se.actorID];
				const auto idx = static_cast<std::size_t>(se.category);
				if (idx < arr.size()) {
					arr[idx] = se.entry;
				}
			}

			// Toggle: show dismissed (former) followers alongside active ones. Default off.
			static bool showDismissedFollowers = false;
			// Toggle: show summoned actors (kCommanded category).
			// Auto-ON when actors appear, auto-OFF when all gone; user may override in between.
			static bool showSummons = false;
			// Toggle: show inclusion-matched actors (kInclusion category).
			// Auto-ON when actors appear, auto-OFF when all gone; user may override in between.
			static bool showInclusionActors = false;
			// Toggle: show actors the user has excluded from mod scope.
			static bool showExcludedActors = false;
			// Toggle: show the cross-clear reference matrix instead of actor preferences.
			static bool showCrossClearTable = false;
			static bool crossClearJustOpened = false;
			// FormID of the actor targeted by the currently open action modal (exclude / remove).
			static RE::FormID s_actionTargetID = 0;

			// Build the persisted-follower set for dismissed-follower filtering.
			// Each toggle is strictly independent:
			//   - Active teammate  -> always shown.
			//   - Summoned actor   -> shown only when summon toggle is on.
			//   - Inclusion actor  -> shown only when inclusion toggle is on.
			//   - Former follower  -> persisted known follower, none of the above;
			//                        shown only when dismissed toggle is on.
			// The dismissed toggle never grants visibility to summons or inclusion actors.
			std::unordered_set<RE::FormID> knownFollowerSet;
			KnownFollowerState::ForEachKnownFollower([&](RE::FormID id) {
				if (id != 0) knownFollowerSet.insert(id);
			});

			std::vector<RE::FormID> actorIDs;
			// 0 = follower, 1 = summon, 2 = inclusion
			std::unordered_map<RE::FormID, std::uint8_t> tabCategoryMap;
			actorIDs.reserve(knownFollowerSet.size() + byActor.size());

			// Scan actor existence to drive auto-ON / auto-OFF transitions.
			// Only fires on transition (appeared / disappeared), so user overrides in between are preserved.
			{
				static bool prevAnySummons   = false;
				static bool prevAnyInclusion = false;
				bool anySummons   = false;
				bool anyInclusion = false;
				for (RE::FormID id : knownFollowerSet) {
					auto* form  = RE::TESForm::LookupByID(id);
					auto* actor = form ? form->As<RE::Actor>() : nullptr;
					if (!actor || actor->IsPlayerTeammate()) continue;
					if (!anySummons   && ActorScope::IsPlayerCommandedActor(actor)) anySummons   = true;
					if (!anyInclusion && ActorInclusion::MatchesAny(actor))         anyInclusion = true;
				}
				for (const auto& kvp : byActor) {
					auto* form  = RE::TESForm::LookupByID(kvp.first);
					auto* actor = form ? form->As<RE::Actor>() : nullptr;
					if (!actor || actor->IsPlayerTeammate()) continue;
					if (!anySummons   && ActorScope::IsPlayerCommandedActor(actor)) anySummons   = true;
					if (!anyInclusion && ActorInclusion::MatchesAny(actor))         anyInclusion = true;
				}
				if (anySummons   && !prevAnySummons)   showSummons         = true;   // actors appeared -> ON
				if (!anySummons  && prevAnySummons)    showSummons         = false;  // actors gone    -> OFF
				if (anyInclusion && !prevAnyInclusion) showInclusionActors = true;
				if (!anyInclusion && prevAnyInclusion) showInclusionActors = false;
				prevAnySummons   = anySummons;
				prevAnyInclusion = anyInclusion;
			}

			std::unordered_set<RE::FormID> seen;
			auto addIfVisible = [&](RE::FormID id) {
				if (id == 0 || !seen.insert(id).second) return;
				// Excluded actors supersede all other category checks.
				if (ActorScopeExcludeState::IsExcluded(id)) {
					if (showExcludedActors) { actorIDs.push_back(id); tabCategoryMap[id] = 3; }
					return;
				}
				auto* form = RE::TESForm::LookupByID(id);
				auto* actor = form ? form->As<RE::Actor>() : nullptr;
				if (actor) {
					if (actor->IsPlayerTeammate()) { actorIDs.push_back(id); tabCategoryMap[id] = 0; return; }
					if (showSummons && ActorScope::IsPlayerCommandedActor(actor)) { actorIDs.push_back(id); tabCategoryMap[id] = 1; return; }
					if (showInclusionActors && ActorInclusion::MatchesAny(actor)) { actorIDs.push_back(id); tabCategoryMap[id] = 2; return; }
					// Former follower: only if not currently a summon or inclusion actor.
					const bool isSummon    = ActorScope::IsPlayerCommandedActor(actor);
					const bool isInclusion = ActorInclusion::MatchesAny(actor);
					if (showDismissedFollowers && knownFollowerSet.count(id) && !isSummon && !isInclusion) { actorIDs.push_back(id); tabCategoryMap[id] = 0; return; }
				} else if (showDismissedFollowers && knownFollowerSet.count(id)) {
					// Form not loaded live (e.g. NPC in an unloaded cell).
					// Dynamic (0xFF*) entries here mean cleanup was missed; they are
					// handled by FormDeleteEventSink / TESDeathEvent.
					if ((id & 0xFF000000) != 0xFF000000) {
						actorIDs.push_back(id); tabCategoryMap[id] = 0;
					}
				}
			};

			for (RE::FormID id : knownFollowerSet) addIfVisible(id);
			for (const auto& kvp : byActor) addIfVisible(kvp.first);
			// Also visit actors that are only in the excluded state (not in knownFollowerSet / byActor).
			{
				std::vector<RE::FormID> excludedIDs;
				excludedIDs.reserve(8);
				ActorScopeExcludeState::ForEach([&](RE::FormID id) { excludedIDs.push_back(id); });
				for (const auto id : excludedIDs) addIfVisible(id);
			}
			// Sort: follower (0) first, then inclusion (2), then summon (1), then disabled (3).
			std::sort(actorIDs.begin(), actorIDs.end(), [&](RE::FormID a, RE::FormID b) {
				constexpr std::uint8_t kOrder[] = { 0, 2, 1, 3 };  // cat->sort-priority: follower=0, summon=1, inclusion=2, excluded=3
				const std::uint8_t ca = tabCategoryMap.count(a) ? kOrder[tabCategoryMap.at(a)] : 0;
				const std::uint8_t cb = tabCategoryMap.count(b) ? kOrder[tabCategoryMap.at(b)] : 0;
				if (ca != cb) return ca < cb;
				return a < b;
			});



			const auto sets = BuildCEPCategorySets();
			auto renderActorTable = [&](RE::FormID actorID, const EntryArray& actorEntries) {
				std::string tableId = "PreferenceCaptureTable##";
				tableId += Hex8(actorID);
				ImGuiMCP::ImVec2 tableStart{};
				ImGuiMCP::ImGui::GetCursorScreenPos(&tableStart);
				ImGuiMCP::ImVec2 tableAvail{};
				ImGuiMCP::ImGui::GetContentRegionAvail(&tableAvail);
				const float tableLeftX = tableStart.x;
				const float tableRightX = tableStart.x + tableAvail.x;
				if (ImGuiMCP::ImGui::BeginTable(
					tableId.c_str(),
					3,
					ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
					ImGuiMCP::ImGui::TableSetupColumn("##category", ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, kCategoryColWidthPx);
					ImGuiMCP::ImGui::TableSetupColumn("##item", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.0f);
					ImGuiMCP::ImGui::TableSetupColumn("##extra", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.0f);

					auto sfh = SetFrameHelper::Make(tableLeftX, tableRightX);
					SetFrameHelper::PushPaddedCellStyle();

					for (const auto& setCats : sets) {
						sfh.beginSetFrame();

						for (const auto catIdx : setCats) {
							if (catIdx >= actorEntries.size()) {
								continue;
							}
							const auto cat = static_cast<CEPCat>(catIdx);
							const auto& optEntry = actorEntries[catIdx];

							ImGuiMCP::ImGui::TableNextRow(0, sfh.minRowHeight);
							ImGuiMCP::ImGui::TableSetColumnIndex(0);
							sfh.cellText(CEPCategoryName(cat));

							ImGuiMCP::ImGui::TableSetColumnIndex(1);
							if (!optEntry.has_value()) {
								sfh.cellNone();
							} else {
								const auto itemText = DescribeTESFormNameOnly(optEntry->baseObjectID);
								sfh.cellText(itemText.c_str());
							}

							ImGuiMCP::ImGui::TableSetColumnIndex(2);
							if (!optEntry.has_value()) {
								sfh.cellNone();
							} else {
								const auto lines = BuildSignatureLines(optEntry->baseObjectID, optEntry->signature);
								if (lines.empty()) {
									sfh.cellNone();
								} else {
									for (const auto& ln : lines) {
										sfh.cellText(ln.c_str());
									}
								}
							}
						}

						sfh.endSetFrame();
					}

					ImGuiMCP::ImGui::PopStyleVar();
					ImGuiMCP::ImGui::EndTable();
				}
			};

			// Render actor tabs outside the scrollable region so they stay fixed.
			// Visual tuning knobs for the actor tab strip (aliased from centralized style):
			const float kActorTabRounding        = kActorTabStyle.rounding;
			const float kActorTabBorderSize      = kActorTabStyle.borderSize;
			const float kActorTabPadX            = kActorTabStyle.padX;
			const float kActorTabPadY            = kActorTabStyle.padY;
			const float kActorTabPreferredMaxW   = kActorTabStyle.preferredMaxWidthPx;
			const float kActorTabMinW            = kActorTabStyle.minWidthPx;

			static RE::FormID selectedActorID = 0;
			if (actorIDs.empty() || std::find(actorIDs.begin(), actorIDs.end(), selectedActorID) == actorIDs.end()) {
				selectedActorID = actorIDs.empty() ? 0 : actorIDs.front();
			}

			ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameRounding, kActorTabRounding);
			ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, kActorTabBorderSize);
			ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding, ImGuiMCP::ImVec2(kActorTabPadX, kActorTabPadY));

			const float tabH = (kActorTabStyle.tabHeightPx > 0.0f)
				? kActorTabStyle.tabHeightPx
				: ImGuiMCP::ImGui::GetFrameHeight();
			const auto* style = ImGuiMCP::ImGui::GetStyle();
			const float spacing = (kActorTabStyle.itemSpacingOverridePx >= 0.0f)
				? kActorTabStyle.itemSpacingOverridePx
				: style->ItemSpacing.x;
			const std::size_t n = actorIDs.size();

			// Build labels and compute natural (min) widths.
			std::vector<std::string> labels(n);
			std::vector<float> widths(n);
			float sumMin = 0.0f;
			for (std::size_t i = 0; i < n; ++i) {
				labels[i] = DescribeTESFormNameOnly(actorIDs[i]);
				ImGuiMCP::ImVec2 sz{};
				ImGuiMCP::ImGui::CalcTextSize(&sz, labels[i].c_str(), nullptr, false, 0.0f);
				widths[i] = (std::max)(sz.x + style->FramePadding.x * 2.0f, kActorTabMinW);
				sumMin += widths[i];
			}

			SettingsItemGap();

			// Distribute available extra space per row, up to preferred max per tab.
			// First: figure out which tabs land on which row (using min widths).
			ImGuiMCP::ImVec2 tabAvail{};
			ImGuiMCP::ImGui::GetContentRegionAvail(&tabAvail);
			const float lineW = tabAvail.x;

			// Reserve right-aligned space for the icon strip: showDisabled + summon + inclusion + dismissed.
			// Cross-clear and help have moved to the view toggle row.
			constexpr float kIconGap = 12.0f;  // Gap between adjacent icons (kept for other uses).
			const float kHelpPadLeft = kActorTabStyle.helpPadLeftPx;
			const float kHelpPadRight = kActorTabStyle.helpPadRightPx;
			const float toggleIconW = DismissedToggleIconWidth();
			const float summonIconW = SummonToggleIconWidth();
			const float inclusionIconW = InclusionToggleIconWidth();
			const float showExcludedIconW = ShowExcludedActorsIconWidth();
			const float excludeIconW = ActorExcludeIconWidth();
			const float removeIconW   = ActorRemoveIconWidth();
			// Icons are rendered at reduced scale in 2-row grids.
			// Left group (1 col x 2 rows): row1=exclude, row2=remove.
			// Right group (2 cols x 2 rows): col1=dismissed/inclusion, col2=excluded/summon.
			constexpr float kSmallIconScale = 0.72f;  // Font scale for 2-row icon grid.
			constexpr float kIconRowGap = 2.0f;        // Vertical gap between icon rows.
			constexpr float kIconColGap = 6.0f;        // Horizontal gap between right-group columns.
			const float colW_left   = (std::max)(excludeIconW, removeIconW) * kSmallIconScale;
			const float colW_right1 = (std::max)(toggleIconW, inclusionIconW) * kSmallIconScale;
			const float colW_right2 = (std::max)(showExcludedIconW, summonIconW) * kSmallIconScale;
			// Right group: [kHelpPadLeft][col_right1][kIconColGap][col_right2][kHelpPadRight]
			const float helpReservedW = kHelpPadLeft + colW_right1 + kIconColGap + colW_right2 + kHelpPadRight;
			// Left group: [kHelpPadLeft][col_left][kHelpPadLeft]
			const float leftReservedW = kHelpPadLeft + colW_left + kHelpPadLeft;
			const float tabLineW = lineW - helpReservedW - leftReservedW;

			ImGuiMCP::ImVec2 tabBlockStart{};
			ImGuiMCP::ImGui::GetCursorScreenPos(&tabBlockStart);

			std::vector<std::size_t> rowStart;  // index of first tab on each row
			rowStart.push_back(0);
			float rowUsed = 0.0f;
			for (std::size_t i = 0; i < n; ++i) {
				const float need = widths[i] + (i > rowStart.back() ? spacing : 0.0f);
				if (i > rowStart.back() && rowUsed + need > tabLineW) {
					rowStart.push_back(i);
					rowUsed = widths[i];
				} else {
					rowUsed += need;
				}
			}
			// Expand tabs within each row.
			for (std::size_t r = 0; r < rowStart.size(); ++r) {
				const std::size_t first = rowStart[r];
				const std::size_t last = (r + 1 < rowStart.size()) ? rowStart[r + 1] : n;
				const std::size_t count = last - first;
				float rowMin = 0.0f;
				for (std::size_t i = first; i < last; ++i) rowMin += widths[i];
				const float rowSpacing = count > 1 ? spacing * static_cast<float>(count - 1) : 0.0f;
				const float extra = tabLineW - rowMin - rowSpacing;
				if (extra > 0.0f) {
					const float share = extra / static_cast<float>(count);
					for (std::size_t i = first; i < last; ++i) {
						const float maxW = (std::max)(kActorTabPreferredMaxW, widths[i]);
						widths[i] += (std::min)(share, maxW - widths[i]);
					}
				}
			}

			// Override item spacing if requested so SameLine respects it.
			const bool overrideSpacing = (kActorTabStyle.itemSpacingOverridePx >= 0.0f);
			if (overrideSpacing) {
				ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_ItemSpacing, ImGuiMCP::ImVec2(spacing, style->ItemSpacing.y));
			}

			// Render tab buttons (wrap to next line when they don't fit).
			float cursorX = 0.0f;
			for (std::size_t i = 0; i < n; ++i) {
				if (i > 0) {
					if (cursorX + spacing + widths[i] <= tabLineW) {
						ImGuiMCP::ImGui::SameLine(0.0f, spacing);
						cursorX += spacing;
					} else {
						// Wrap: reset cursor to the left-group-offset start of next row.
						ImGuiMCP::ImVec2 wrapPos{};
						ImGuiMCP::ImGui::GetCursorScreenPos(&wrapPos);
						ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(tabBlockStart.x + leftReservedW, wrapPos.y));
						cursorX = 0.0f;
					}
				} else {
					// First tab on first row: shift right to leave room for left icon group.
					ImGuiMCP::ImVec2 startPos{};
					ImGuiMCP::ImGui::GetCursorScreenPos(&startPos);
					ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(tabBlockStart.x + leftReservedW, startPos.y));
				}
				const bool selected = (actorIDs[i] == selectedActorID);
				const auto catIt2 = tabCategoryMap.find(actorIDs[i]);
				const std::uint8_t cat = (catIt2 != tabCategoryMap.end()) ? catIt2->second : 0;
				const ActorTabColors& catColors = (cat == 1) ? kActorTabStyle.summon
				                               : (cat == 2) ? kActorTabStyle.inclusion
				                               : (cat == 3) ? kActorTabStyle.excluded
				                               :              kActorTabStyle.follower;
				auto themeScope = PushActorTabColors(catColors, selected);
				std::string btnId = labels[i] + "##ActorTab" + Hex8(actorIDs[i]);
				const bool tabClicked = ImGuiMCP::ImGui::Button(btnId.c_str(), ImGuiMCP::ImVec2(widths[i], tabH));

				// Category colour strip at the bottom of the button.
				// Active tab: 2x strip height, anchored to button bottom.
				{
					auto* dl = ImGuiMCP::ImGui::GetWindowDrawList();
					ImGuiMCP::ImVec2 bMin{}, bMax{};
					ImGuiMCP::ImGui::GetItemRectMin(&bMin);
					ImGuiMCP::ImGui::GetItemRectMax(&bMax);
					const float stripH = selected
						? kActorTabStyle.stripHeightPx * 2.0f
						: kActorTabStyle.stripHeightPx;
					const ImGuiMCP::ImVec4 sc = selected ? catColors.activeStrip : catColors.inactiveStrip;
					ImGuiMCP::ImGui::ImDrawListManager::AddRectFilled(dl,
						ImGuiMCP::ImVec2(bMin.x, bMax.y - stripH),
						ImGuiMCP::ImVec2(bMax.x, bMax.y),
						ImGuiMCP::ImGui::GetColorU32(sc),
						kActorTabRounding, ImGuiMCP::ImDrawFlags_RoundCornersBottom);
				}

				if (tabClicked) {
					selectedActorID = actorIDs[i];
				}

				cursorX += widths[i];
			}

			if (overrideSpacing) {
				ImGuiMCP::ImGui::PopStyleVar();
			}
			ImGuiMCP::ImGui::PopStyleVar(3);

			// When no tabs are drawn, emit an invisible placeholder so that
			// GetItemRectMax (used for icon positioning) has a valid reference.
			if (n == 0) {
				ImGuiMCP::ImVec2 dummyStart{};
				ImGuiMCP::ImGui::GetCursorScreenPos(&dummyStart);
				ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(tabBlockStart.x + leftReservedW, dummyStart.y));
				ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2(0.0f, tabH));
			}

			// 2-row small-icon grid strip, vertically centered in the tab block.
			float tabBlockH = 0.0f;
			{
				ImGuiMCP::ImVec2 lastTabMax{};
				ImGuiMCP::ImGui::GetItemRectMax(&lastTabMax);
				const float blockBottom = lastTabMax.y;
				const float fontSize = ImGuiMCP::ImGui::GetFontSize();
				tabBlockH = blockBottom - tabBlockStart.y;

				// Two rows of scaled icons, vertically centered inside the tab block.
				const float iconRowH = fontSize * kSmallIconScale;
				const float gridH    = iconRowH + kIconRowGap + iconRowH;
				const float iconGroupTopY = tabBlockStart.y + (tabBlockH - gridH) * 0.5f;
				const float row1Y = (std::max)(tabBlockStart.y, iconGroupTopY);
				const float row2Y = row1Y + iconRowH + kIconRowGap;

				// Right group column X positions (right-to-left: col2, then col1).
				const float rightGroupRight = tabBlockStart.x + lineW - kHelpPadRight;
				const float col2StartX = rightGroupRight - colW_right2;
				const float col1StartX = col2StartX - kIconColGap - colW_right1;
				// Left group column X.
				const float leftGroupX = tabBlockStart.x + kHelpPadLeft;

				// Renders one scaled FA-Solid icon at screen position (x, y) with the given colour.
				// Leaves ImGui's "last item" pointing at the TextUnformatted call so that
				// IsItemHovered / IsItemClicked work correctly in the caller.
				const auto RenderTabIcon = [&](float x, float y, const std::string& icon, const ImGuiMCP::ImVec4& col) {
					ImGuiMCP::ImGui::SetWindowFontScale(kSmallIconScale);
					ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(x, y));
					FontAwesome::PushSolid();
					ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, col);
					ImGuiMCP::ImGui::TextUnformatted(icon.c_str());
					ImGuiMCP::ImGui::PopStyleColor();
					FontAwesome::Pop();
					ImGuiMCP::ImGui::SetWindowFontScale(1.0f);
				};

				// Right group, row 1 col 1: dismissed-follower toggle (right-aligned within col1).
				{
					const auto& icon = showDismissedFollowers ? kIconAllFollowers : kIconActiveOnly;
					const ImGuiMCP::ImVec4& dismissedCol = showDismissedFollowers
						? kIconColors.dismissedOn : kIconColors.dismissedOff;
					RenderTabIcon(col1StartX + (colW_right1 - FaIconWidth(icon) * kSmallIconScale), row1Y, icon, dismissedCol);
					if (ImGuiMCP::ImGui::IsItemHovered()) ImGuiMCP::ImGui::SetMouseCursor(ImGuiMCP::ImGuiMouseCursor_Hand);
					TooltipOnHoverText(Localization::CStr(showDismissedFollowers
						? "ui.actor_management.filter.former_followers.hide"
						: "ui.actor_management.filter.former_followers.show"));
					if (ImGuiMCP::ImGui::IsItemClicked()) showDismissedFollowers = !showDismissedFollowers;
				}

				// Right group, row 1 col 2: show-excluded-actors toggle.
				{
					const ImGuiMCP::ImVec4& showExcludedCol = showExcludedActors
						? kIconColors.showExcludedOn : kIconColors.showExcludedOff;
					RenderTabIcon(col2StartX, row1Y, kIconExcludedShow, showExcludedCol);
					if (ImGuiMCP::ImGui::IsItemHovered()) ImGuiMCP::ImGui::SetMouseCursor(ImGuiMCP::ImGuiMouseCursor_Hand);
					TooltipOnHoverText(Localization::CStr(showExcludedActors
						? "ui.actor_management.filter.excluded_actors.hide"
						: "ui.actor_management.filter.excluded_actors.show"));
					if (ImGuiMCP::ImGui::IsItemClicked()) showExcludedActors = !showExcludedActors;
				}

				// Right group, row 2 col 1: inclusion-actor toggle.
				{
					const ImGuiMCP::ImVec4& inclusionCol = showInclusionActors
						? kIconColors.inclusionOn : kIconColors.inclusionOff;
					RenderTabIcon(col1StartX, row2Y, showInclusionActors ? kIconInclusionShow : kIconInclusionHide, inclusionCol);
					if (ImGuiMCP::ImGui::IsItemHovered()) ImGuiMCP::ImGui::SetMouseCursor(ImGuiMCP::ImGuiMouseCursor_Hand);
					TooltipOnHoverText(Localization::CStr(showInclusionActors
						? "ui.actor_management.filter.inclusion_actors.hide"
						: "ui.actor_management.filter.inclusion_actors.show"));
					if (ImGuiMCP::ImGui::IsItemClicked()) showInclusionActors = !showInclusionActors;
				}

				// Right group, row 2 col 2: summon-actor toggle.
				{
					const ImGuiMCP::ImVec4& summonCol = showSummons ? kIconColors.summonOn : kIconColors.summonOff;
					RenderTabIcon(col2StartX, row2Y, showSummons ? kIconSummonsShow : kIconSummonsHide, summonCol);
					if (ImGuiMCP::ImGui::IsItemHovered()) ImGuiMCP::ImGui::SetMouseCursor(ImGuiMCP::ImGuiMouseCursor_Hand);
					TooltipOnHoverText(Localization::CStr(showSummons
						? "ui.actor_management.filter.summons.hide"
						: "ui.actor_management.filter.summons.show"));
					if (ImGuiMCP::ImGui::IsItemClicked()) showSummons = !showSummons;
				}

				// Left group, row 1: actor exclude/include icon.
				// Shows kIconActorInclude (green) when the actor is currently excluded;
				// shows kIconActorExclude (red) when the actor is active.
				{
					const bool hasSelection = (selectedActorID != 0);
					const bool isExcluded   = hasSelection && ActorScopeExcludeState::IsExcluded(selectedActorID);
					const std::string& icon = isExcluded ? kIconActorInclude : kIconActorExclude;
					const ImGuiMCP::ImVec4& col = !hasSelection
						? (isExcluded ? kIconColors.includeInactive  : kIconColors.excludeInactive)
						: (isExcluded ? kIconColors.includeActive    : kIconColors.excludeActive);
					RenderTabIcon(leftGroupX, row1Y, icon, col);
					if (hasSelection && ImGuiMCP::ImGui::IsItemHovered()) ImGuiMCP::ImGui::SetMouseCursor(ImGuiMCP::ImGuiMouseCursor_Hand);
					if (hasSelection) TooltipOnHoverText(Localization::CStr(isExcluded
						? "ui.actor_management.include_confirm.tooltip"
						: "ui.actor_management.exclude_confirm.tooltip"));
					if (hasSelection && ImGuiMCP::ImGui::IsItemClicked()) {
						s_actionTargetID = selectedActorID;
						ImGuiMCP::ImGui::OpenPopup("##ActorExcludeConfirm");
					}
				}

				// Left group, row 2: actor remove icon.
				{
					const bool hasSelection = (selectedActorID != 0);
					const ImGuiMCP::ImVec4& col = hasSelection
						? kIconColors.removeActive : kIconColors.removeInactive;
					RenderTabIcon(leftGroupX, row2Y, kIconActorRemove, col);
					if (hasSelection && ImGuiMCP::ImGui::IsItemHovered()) ImGuiMCP::ImGui::SetMouseCursor(ImGuiMCP::ImGuiMouseCursor_Hand);
					if (hasSelection) TooltipOnHoverText(Localization::CStr("ui.actor_management.remove_confirm.tooltip"));
					if (hasSelection && ImGuiMCP::ImGui::IsItemClicked()) {
						s_actionTargetID = selectedActorID;
						ImGuiMCP::ImGui::OpenPopup("##ActorRemoveConfirm");
					}
				}

				const float sepGap = (kActorTabStyle.bottomSeparatorGapPx >= 0.0f)
					? kActorTabStyle.bottomSeparatorGapPx
					: style->ItemSpacing.y;
				ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(tabBlockStart.x, blockBottom + sepGap));
			}

			// Confirmation modal: disable / enable actor from mod scope.
			{
				const bool isCurrentlyExcluded = s_actionTargetID != 0 && ActorScopeExcludeState::IsExcluded(s_actionTargetID);
				const char* bodyText = isCurrentlyExcluded
					? Localization::CStr("ui.actor_management.include_confirm.body")
					: Localization::CStr("ui.actor_management.exclude_confirm.body");
				RenderActorConfirmModal("##ActorExcludeConfirm", bodyText, s_actionTargetID,
					[&](float btnW, float btnH) {
						if (isCurrentlyExcluded) {
							if (ImGuiMCP::ImGui::Button(
									Localization::CStr("ui.actor_management.include_confirm.confirm"),
									ImGuiMCP::ImVec2{ btnW, btnH })) {
								ActorScopeExcludeState::Include(s_actionTargetID);
								s_actionTargetID = 0;
								ImGuiMCP::ImGui::CloseCurrentPopup();
							}
						} else {
							if (ImGuiMCP::ImGui::Button(
									Localization::CStr("ui.actor_management.exclude_confirm.confirm"),
									ImGuiMCP::ImVec2{ btnW, btnH })) {
								ActorScopeExcludeState::Exclude(s_actionTargetID);
								showExcludedActors = true;
								s_actionTargetID = 0;
								ImGuiMCP::ImGui::CloseCurrentPopup();
							}
						}
					});
			}

			// Confirmation modal: reset all actor mod state.
			{
				const char* bodyText = (s_actionTargetID != 0 && ActorScopeExcludeState::IsExcluded(s_actionTargetID))
					? Localization::CStr("ui.actor_management.remove_confirm.body_excluded")
					: Localization::CStr("ui.actor_management.remove_confirm.body");
				RenderActorConfirmModal("##ActorRemoveConfirm", bodyText, s_actionTargetID,
					[&](float btnW, float btnH) {
						if (ImGuiMCP::ImGui::Button(
								Localization::CStr("ui.actor_management.remove_confirm.confirm"),
								ImGuiMCP::ImVec2{ btnW, btnH })) {
							ActorStateCleanup::PurgeActorFromAllStores(s_actionTargetID);
							if (selectedActorID == s_actionTargetID) selectedActorID = 0;
							s_actionTargetID = 0;
							ImGuiMCP::ImGui::CloseCurrentPopup();
						}
					});
			}

			// Saved screen positions for cross-clear modal.
			// Filled inside the view toggle block; used by the modal below.
			float ccHelpScreenX  = 0.0f;  // X of outer Preferences [?] icon.
			float ccHelpScreenY  = 0.0f;  // Y of icon row (modal top anchor).
			float ccSpellsHelpX  = 0.0f;  // X of outer Spell List [?] icon.
			float ccCrossScreenX = 0.0f;  // X of outer kIconCrossClear icon.
			float ccModalX       = 0.0f;  // Modal outer-left X (= window left edge).
			float ccModalW       = 0.0f;  // Modal outer width  (= full window width).
			float ccSepBelowY    = 0.0f;  // Y of separator below view toggle row.

			// Capture the Y of the separator above the view toggle row.
			// Used as the modal's top edge so the modal starts exactly here.
			ImGuiMCP::ImVec2 ccSepTopPos{};
			ImGuiMCP::ImGui::GetCursorScreenPos(&ccSepTopPos);

			ImGuiMCP::ImGui::Separator();

			// View toggle row with right-aligned view-specific icons.
			// Preferences view: cross-clear + help icons on the right.
			// Spell List view: help icon on the right (same X as preferences help).
			if (viewMode != nullptr) {
				const auto* vSt = ImGuiMCP::ImGui::GetStyle();

			// Height from style knobs; width is computed after vAvail is known (scales with page width).
				float       vBW = 0.0f;  // Set in the row-split block below.
				const float vBH = kViewToggleStyle.buttonHeightPx;

				const float vFontSz    = ImGuiMCP::ImGui::GetFontSize();
				const float vFramePadY = (vBH - vFontSz) * 0.5f;

				// Capture row origin and available width before any cursor movement.
				ImGuiMCP::ImVec2 vRowOrigin{};
				ImGuiMCP::ImGui::GetCursorScreenPos(&vRowOrigin);
				ImGuiMCP::ImVec2 vAvail{};
				ImGuiMCP::ImGui::GetContentRegionAvail(&vAvail);
				// Save full outer window width for modal sizing (content + 2 x window padding).
				{
					const float padX = vSt ? vSt->WindowPadding.x : 0.0f;
					ccModalX = vRowOrigin.x - padX;
					ccModalW = vAvail.x + padX * 2.0f;
				}

				const char* vLbl0 = Localization::CStr("ui.actor_management.view.preferences");
				const char* vLbl1 = Localization::CStr("ui.actor_management.view.spell_list");

				// Draws one view-toggle button: icon left-aligned, text centered in full width.
				// Colors come from the per-button ViewToggleButtonColors; frame shape from kViewToggleStyle.
				auto ViewToggleButton = [&](const char* btnId, const char* label, const std::string& icon,
											bool active, const ViewToggleButtonColors& C, bool iconRight = false) -> bool {
					const auto& V = kViewToggleStyle;
					ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button,        active ? C.activeButton   : C.inactiveButton);
					ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, active ? C.activeHovered  : C.inactiveHovered);
					ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive,  active ? C.activePressed  : C.inactivePressed);
					ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Border,        active ? C.activeBorder   : C.inactiveBorder);
					ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FramePadding,
						ImGuiMCP::ImVec2(vSt ? vSt->FramePadding.x : 8.0f, vFramePadY));
					ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameRounding,   V.rounding);
					ImGuiMCP::ImGui::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, V.borderSize);

					std::string hiddenId = "##"; hiddenId += btnId;
					const bool pressed = ImGuiMCP::ImGui::Button(hiddenId.c_str(), ImGuiMCP::ImVec2(vBW, vBH));

					ImGuiMCP::ImGui::PopStyleVar(3);
					ImGuiMCP::ImGui::PopStyleColor(4);

					auto* dl = ImGuiMCP::ImGui::GetWindowDrawList();
					ImGuiMCP::ImVec2 rMin{};
					ImGuiMCP::ImGui::GetItemRectMin(&rMin);

					ImGuiMCP::ImVec2 iconSz{};
					FontAwesome::PushSolid();
					ImGuiMCP::ImGui::CalcTextSize(&iconSz, icon.c_str(), nullptr, false, 0.0f);
					FontAwesome::Pop();

					ImGuiMCP::ImVec2 lblSz{};
					ImGuiMCP::ImGui::CalcTextSize(&lblSz, label, nullptr, false, 0.0f);

					const float iPadX = vSt ? vSt->FramePadding.x : 8.0f;

					// Text position computed first so icon can reference it.
					const float lblX = rMin.x + (vBW - lblSz.x) * 0.5f;
					const float lblY = rMin.y + (vBH - lblSz.y) * 0.5f;

					// Icon: immediately left of text (default) or immediately right of text (iconRight).
					// Gap between icon and text = iPadX in both cases.
					const float icoX = iconRight
						? (lblX + lblSz.x + iPadX)
						: (lblX - iconSz.x - iPadX);
					const float icoY = rMin.y + (vBH - iconSz.y) * 0.5f;
					FontAwesome::PushSolid();
					ImGuiMCP::ImGui::ImDrawListManager::AddText(dl, ImGuiMCP::ImVec2(icoX, icoY),
						ImGuiMCP::ImGui::GetColorU32(active ? C.activeIcon : C.inactiveIcon), icon.c_str());
					FontAwesome::Pop();

					ImGuiMCP::ImGui::ImDrawListManager::AddText(dl, ImGuiMCP::ImVec2(lblX, lblY),
						ImGuiMCP::ImGui::GetColorU32(active ? C.activeText : C.inactiveText), label);

					return pressed;
				};

				// Row split: Prefs right-aligned in left half, Spells left-aligned in right half.
				// Button width scales with available space (floored at buttonMinWidthPx).
				const float halfW      = vAvail.x * 0.5f;
				const float halfGap    = kViewToggleStyle.halfCenterGapPx;
				vBW = (std::max)(kViewToggleStyle.buttonMinWidthPx, halfW - halfGap);
				const float prefsBtnX  = vRowOrigin.x + halfW - halfGap - vBW;
				const float spellsBtnX = vRowOrigin.x + halfW + halfGap;

				ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(prefsBtnX, vRowOrigin.y));
				if (ViewToggleButton("AMViewPrefs", vLbl0, kIconViewPreferences, *viewMode == 0, kViewToggleStyle.prefs)) {
					*viewMode = 0;
				}
				ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(spellsBtnX, vRowOrigin.y));
				if (ViewToggleButton("AMViewSpells", vLbl1, kIconViewSpellList, *viewMode == 1, kViewToggleStyle.spells, true)) {
					*viewMode = 1;
				}

				// Save X positions for the dedicated icon row below (Y captured there).
				const float vHelpIcoW    = HelpIconWidth();
				const float vPrefsHelpX  = vRowOrigin.x + kHelpPadLeft;
				const float vSpellsHelpX = vRowOrigin.x + vAvail.x - kHelpPadRight - vHelpIcoW;
				ccHelpScreenX  = vPrefsHelpX;
				ccSpellsHelpX  = vSpellsHelpX;
				if (*viewMode == 0) {
					ccCrossScreenX = vPrefsHelpX + vHelpIcoW + kIconGap;
				}

				// Restore cursor to below button row.
				const float vSpacingY = vSt ? vSt->ItemSpacing.y * 0.5f : 0.0f;
				ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(vRowOrigin.x, vRowOrigin.y + vBH + vSpacingY));
			}

			// Dedicated icon row below the view toggle buttons.
			// Icons are drawn on the window draw list.
			// Hidden while the cross-clear modal is open (the modal draws its own icons).
			if (viewMode != nullptr) {
				ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2(0.0f, 12.0f));

				ImGuiMCP::ImVec2 icoRowPos{};
				ImGuiMCP::ImGui::GetCursorScreenPos(&icoRowPos);
				ccHelpScreenY = icoRowPos.y;  // Icon row Y = modal top anchor.

				const float iFontSz   = ImGuiMCP::ImGui::GetFontSize();
				const float iHelpIcoW = HelpIconWidth();

				if (*viewMode == 0) {
					// Preferences help icon - InvisibleButton always runs to keep layout height.
					{
						ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(ccHelpScreenX, icoRowPos.y));
						ImGuiMCP::ImGui::InvisibleButton("##PrefsHelp", ImGuiMCP::ImVec2(iHelpIcoW, iFontSz));
						if (!showCrossClearTable) {
							auto* fdl = ImGuiMCP::ImGui::GetWindowDrawList();
							FontAwesome::PushSolid();
							ImGuiMCP::ImGui::ImDrawListManager::AddText(fdl, ImGuiMCP::ImVec2(ccHelpScreenX, icoRowPos.y),
								ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), kHelpIcon.c_str());
							FontAwesome::Pop();
							TooltipOnHoverText(Localization::CStr("ui.preference_capture.help"));
						}
					}
					// Cross-clear icon - InvisibleButton always runs to keep layout height.
					{
						ImGuiMCP::ImVec2 ccIconSz{};
						FontAwesome::PushSolid();
						ImGuiMCP::ImGui::CalcTextSize(&ccIconSz, kIconCrossClear.c_str(), nullptr, false, 0.0f);
						FontAwesome::Pop();
						ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(ccCrossScreenX, icoRowPos.y));
						ImGuiMCP::ImGui::InvisibleButton("##CrossClearToggle", ImGuiMCP::ImVec2(ccIconSz.x, iFontSz));
						if (!showCrossClearTable) {
							const float ccAlpha = ImGuiMCP::ImGui::IsItemHovered() ? 1.0f : 0.60f;
							auto* fdl = ImGuiMCP::ImGui::GetWindowDrawList();
							FontAwesome::PushSolid();
							ImGuiMCP::ImGui::ImDrawListManager::AddText(fdl, ImGuiMCP::ImVec2(ccCrossScreenX, icoRowPos.y),
								ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImVec4(1.0f, 1.0f, 1.0f, ccAlpha)), kIconCrossClear.c_str());
							FontAwesome::Pop();
							if (ImGuiMCP::ImGui::IsItemHovered()) {
								ImGuiMCP::ImGui::SetMouseCursor(ImGuiMCP::ImGuiMouseCursor_Hand);
							}
							TooltipOnHoverText(Localization::CStr("ui.preference_capture.cross_clear.show"));
							if (ImGuiMCP::ImGui::IsItemClicked()) {
								showCrossClearTable = !showCrossClearTable;
								if (showCrossClearTable) {
									ImGuiMCP::ImGui::OpenPopup("##CrossClearModal");
									crossClearJustOpened = true;
								}
							}
						}
					}
				} else {
					// Spell List help icon - InvisibleButton always runs to keep layout height.
					{
						ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2(ccSpellsHelpX, icoRowPos.y));
						ImGuiMCP::ImGui::InvisibleButton("##SpellsHelp", ImGuiMCP::ImVec2(iHelpIcoW, iFontSz));
						if (!showCrossClearTable) {
							auto* fdl = ImGuiMCP::ImGui::GetWindowDrawList();
							FontAwesome::PushSolid();
							ImGuiMCP::ImGui::ImDrawListManager::AddText(fdl, ImGuiMCP::ImVec2(ccSpellsHelpX, icoRowPos.y),
								ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), kHelpIcon.c_str());
							FontAwesome::Pop();
							TooltipOnHoverText(Localization::CStr("ui.spell_list.help"));
						}
					}
				}

				// Advance cursor past icon row.
				ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2(0.0f, 2.0f));
			}

			// Capture Y of separator below view toggle for modal separator overlay.
			{
				ImGuiMCP::ImVec2 sepBelowPos{};
				ImGuiMCP::ImGui::GetCursorScreenPos(&sepBelowPos);
				ccSepBelowY = sepBelowPos.y;
			}
			ImGuiMCP::ImGui::Separator();

			// Capture scroll-child start for cross-clear modal alignment.
			ImGuiMCP::ImVec2 ccScrollOrigin{};
			ImGuiMCP::ImGui::GetCursorScreenPos(&ccScrollOrigin);

			// Scrollable content area.  A negative height reserves space for fixed widgets below.
			const float scrollChildH = (reservedBottomPx > 0.0f) ? -reservedBottomPx : 0.0f;

			// When Spell List view is active, show placeholder and skip preferences content.
			const bool showPreferencesView = (viewMode == nullptr || *viewMode == 0);
			if (!showPreferencesView) {
				if (selectedActorID != 0 && ActorScopeExcludeState::IsExcluded(selectedActorID)) {
					ImGuiMCP::ImGui::BeginChild(
						"SpellListScrollExcluded",
						ImGuiMCP::ImVec2(0.0f, scrollChildH),
						ImGuiMCP::ImGuiChildFlags_None,
						ImGuiMCP::ImGuiWindowFlags_None);
					ImGuiMCP::ImGui::Spacing();
					ImGuiMCP::ImGui::TextDisabled("%s", Localization::CStr("ui.actor_management.excluded_placeholder"));
					ImGuiMCP::ImGui::EndChild();
				} else {
					SpellListView::Render(selectedActorID, scrollChildH);
				}
				return;
			}

			// Cross-clear reference modal.
			if (showCrossClearTable) {
				// Ensure the popup is open (OpenPopup is safe to call every frame;
				// it's a no-op when the popup is already open).
				if (!ImGuiMCP::ImGui::IsPopupOpen("##CrossClearModal")) {
					ImGuiMCP::ImGui::OpenPopup("##CrossClearModal");
				}

				// Modal: starts at the icon row (below view toggle buttons), spans full width to page bottom.
				ImGuiMCP::ImVec2 curNow{};
				ImGuiMCP::ImGui::GetCursorScreenPos(&curNow);
				ImGuiMCP::ImVec2 availNow{};
				ImGuiMCP::ImGui::GetContentRegionAvail(&availNow);
				const float pageBottomY = curNow.y + availNow.y;

				ImGuiMCP::ImGui::SetNextWindowPos(
					ImGuiMCP::ImVec2{ ccModalX, ccHelpScreenY - 16.0f },
					ImGuiMCP::ImGuiCond_Always,
					ImGuiMCP::ImVec2{ 0.0f, 0.0f });
				ImGuiMCP::ImGui::SetNextWindowSize(
					ImGuiMCP::ImVec2{ ccModalW, pageBottomY - (ccHelpScreenY - 16.0f) },
					ImGuiMCP::ImGuiCond_Always);

				bool modalOpen = true;
				if (ImGuiMCP::ImGui::BeginPopupModal(
						"##CrossClearModal", &modalOpen,
						ImGuiMCP::ImGuiWindowFlags_NoTitleBar
						| ImGuiMCP::ImGuiWindowFlags_NoResize
						| ImGuiMCP::ImGuiWindowFlags_NoMove
						| ImGuiMCP::ImGuiWindowFlags_NoScrollbar
						| ImGuiMCP::ImGuiWindowFlags_NoScrollWithMouse)) {

					// --- Help icon: same screen position as outer Preferences [?] ---
					// Advance layout cursor past icon row header to where BeginChild will be placed.
					ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2(0.0f, ccScrollOrigin.y - ccHelpScreenY));

					// Measure icon sizes and font.
					const float mFontSz   = ImGuiMCP::ImGui::GetFontSize();
					const float mHelpIcoW = HelpIconWidth();
					ImGuiMCP::ImVec2 closeIconSz{};
					FontAwesome::PushSolid();
					ImGuiMCP::ImGui::CalcTextSize(&closeIconSz, kIconCrossClearClose.c_str(), nullptr, false, 0.0f);
					FontAwesome::Pop();

					// Help icon - drawn on the window draw list.
					{
						ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2{ ccHelpScreenX, ccHelpScreenY });
						ImGuiMCP::ImGui::InvisibleButton("##ModalHelp", ImGuiMCP::ImVec2(mHelpIcoW, mFontSz));
						auto* fdl = ImGuiMCP::ImGui::GetWindowDrawList();
						FontAwesome::PushSolid();
						ImGuiMCP::ImGui::ImDrawListManager::AddText(fdl,
							ImGuiMCP::ImVec2(ccHelpScreenX, ccHelpScreenY),
							ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImVec4(0.80f, 0.80f, 0.80f, 1.0f)),
							kHelpIcon.c_str());
						FontAwesome::Pop();
						TooltipOnHoverText(Localization::CStr("ui.preference_capture.cross_clear.help"));
					}

					// Close icon - window draw list, hover brightens.
					{
						ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2{ ccCrossScreenX, ccHelpScreenY });
						ImGuiMCP::ImGui::InvisibleButton("##ModalClose", ImGuiMCP::ImVec2(closeIconSz.x, mFontSz));
						const float closeAlpha = ImGuiMCP::ImGui::IsItemHovered() ? 1.0f : 0.80f;
						auto* fdl = ImGuiMCP::ImGui::GetWindowDrawList();
						FontAwesome::PushSolid();
						ImGuiMCP::ImGui::ImDrawListManager::AddText(fdl,
							ImGuiMCP::ImVec2(ccCrossScreenX, ccHelpScreenY),
							ImGuiMCP::ImGui::GetColorU32(ImGuiMCP::ImVec4(0.80f, 0.80f, 0.80f, closeAlpha)),
							kIconCrossClearClose.c_str());
						FontAwesome::Pop();
						if (ImGuiMCP::ImGui::IsItemHovered()) {
							ImGuiMCP::ImGui::SetMouseCursor(ImGuiMCP::ImGuiMouseCursor_Hand);
						}
						if (ImGuiMCP::ImGui::IsItemClicked()) {
							showCrossClearTable = false;
							ImGuiMCP::ImGui::CloseCurrentPopup();
						}
					}

					// Separator: same position as the outer separator below the view toggle row.
					ImGuiMCP::ImVec2 winPos{};
					ImGuiMCP::ImGui::GetWindowPos(&winPos);
					ImGuiMCP::ImGui::SetCursorScreenPos(ImGuiMCP::ImVec2{ winPos.x, ccSepBelowY });
					ImGuiMCP::ImGui::Separator();

					// Jump to the exact scroll-child start position and draw the table there.
					ImGuiMCP::ImGui::SetCursorScreenPos(ccScrollOrigin);

					// --- Scrollable cross-clear table area (same screen region as preferences scroll) ---
					ImGuiMCP::ImGui::BeginChild(
						"CrossClearScroll", ImGuiMCP::ImVec2(0.0f, 0.0f),
						ImGuiMCP::ImGuiChildFlags_None,
						ImGuiMCP::ImGuiWindowFlags_None);

				constexpr auto Bit = [](CEPCat c) constexpr -> std::uint16_t {
					return static_cast<std::uint16_t>(1u << static_cast<std::uint8_t>(c));
				};
				constexpr auto Mask = [](std::initializer_list<CEPCat> cats) constexpr -> std::uint16_t {
					std::uint16_t m = 0;
					for (auto c : cats) m = static_cast<std::uint16_t>(m | (1u << static_cast<std::uint8_t>(c)));
					return m;
				};

				struct CCGroup {
					const char*          tableId;
					const CEPCat*        cats;
					const std::uint16_t* masks;
					std::size_t          n;
				};

				constexpr CEPCat kMeleeCats[] = {
					CEPCat::kOneHandRight, CEPCat::kOneHandLeft, CEPCat::kShieldLeft, CEPCat::kTwoHand };
				constexpr std::uint16_t kMeleeMasks[] = {
					Mask({ CEPCat::kTwoHand }),
					Mask({ CEPCat::kShieldLeft, CEPCat::kTwoHand }),
					Mask({ CEPCat::kOneHandLeft, CEPCat::kTwoHand }),
					Mask({ CEPCat::kOneHandRight, CEPCat::kOneHandLeft, CEPCat::kShieldLeft }) };

				constexpr CEPCat kRangedCats[] = { CEPCat::kBow, CEPCat::kCrossbow };
				constexpr std::uint16_t kRangedMasks[] = {
					Mask({ CEPCat::kCrossbow }),
					Mask({ CEPCat::kBow }) };

				constexpr CEPCat kAmmoCats[] = { CEPCat::kArrow, CEPCat::kBolt };
				constexpr std::uint16_t kAmmoMasks[] = { 0, 0 };

				constexpr CEPCat kStaffCats[] = { CEPCat::kStaffRight, CEPCat::kStaffLeft };
				constexpr std::uint16_t kStaffMasks[] = { 0, 0 };

				constexpr CEPCat kScrollCats[] = {
					CEPCat::kScrollRight, CEPCat::kScrollLeft, CEPCat::kScrollBoth };
				constexpr std::uint16_t kScrollMasks[] = {
					Mask({ CEPCat::kScrollBoth }),
					Mask({ CEPCat::kScrollBoth }),
					Mask({ CEPCat::kScrollRight, CEPCat::kScrollLeft }) };

				const CCGroup kGroups[] = {
					{ "CCMelee",  kMeleeCats,  kMeleeMasks,  std::size(kMeleeCats) },
					{ "CCRanged", kRangedCats, kRangedMasks, std::size(kRangedCats) },
					{ "CCAmmo",   kAmmoCats,   kAmmoMasks,   std::size(kAmmoCats) },
					{ "CCStaff",  kStaffCats,  kStaffMasks,  std::size(kStaffCats) },
					{ "CCScroll", kScrollCats, kScrollMasks, std::size(kScrollCats) },
				};

				ImGuiMCP::ImVec2 areaStart{};
				ImGuiMCP::ImGui::GetCursorScreenPos(&areaStart);
				ImGuiMCP::ImVec2 areaAvail{};
				ImGuiMCP::ImGui::GetContentRegionAvail(&areaAvail);
				const float ccLeftX = areaStart.x;
				const float ccRightX = areaStart.x + areaAvail.x;

				for (const auto& grp : kGroups) {
					const int colCount = 1 + static_cast<int>(grp.n);
					if (ImGuiMCP::ImGui::BeginTable(
							grp.tableId, colCount,
							ImGuiMCP::ImGuiTableFlags_SizingStretchProp
								| ImGuiMCP::ImGuiTableFlags_BordersInner)) {

						ImGuiMCP::ImGui::TableSetupColumn("##rowheader",
							ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, kCategoryColWidthPx);
						for (std::size_t c = 0; c < grp.n; ++c) {
							ImGuiMCP::ImGui::TableSetupColumn(
								CEPCategoryName(grp.cats[c]),
								ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.0f);
						}

						auto sfh = SetFrameHelper::Make(ccLeftX, ccRightX);
						SetFrameHelper::PushPaddedCellStyle();

						// Header row - category names as column headers (centered).
						ImGuiMCP::ImGui::TableNextRow(0, sfh.minRowHeight);
						ImGuiMCP::ImGui::TableSetColumnIndex(0);
						ImGuiMCP::ImGui::Dummy(ImGuiMCP::ImVec2{ 0.0f, 0.0f });
						sfh.updateY1();
						for (std::size_t c = 0; c < grp.n; ++c) {
							ImGuiMCP::ImGui::TableSetColumnIndex(static_cast<int>(c) + 1);
							const char* hdrText = CEPCategoryName(grp.cats[c]);
							ImGuiMCP::ImVec2 hdrSz{};
							ImGuiMCP::ImGui::CalcTextSize(&hdrSz, hdrText, nullptr, false, 0.0f);
							const float colW = ImGuiMCP::ImGui::GetColumnWidth(-1);
							const float padX = (colW - hdrSz.x) * 0.5f;
							if (padX > 0.0f) {
								ImGuiMCP::ImGui::SetCursorPosX(ImGuiMCP::ImGui::GetCursorPosX() + padX);
							}
							ImGuiMCP::ImGui::TextUnformatted(hdrText);
							sfh.updateY1();
						}

						// Data rows.
						for (std::size_t row = 0; row < grp.n; ++row) {
							ImGuiMCP::ImGui::TableNextRow(0, sfh.minRowHeight);
							ImGuiMCP::ImGui::TableSetColumnIndex(0);
							sfh.cellText(CEPCategoryName(grp.cats[row]));

							for (std::size_t col = 0; col < grp.n; ++col) {
								ImGuiMCP::ImGui::TableSetColumnIndex(static_cast<int>(col) + 1);
								if (row != col && (grp.masks[row] & Bit(grp.cats[col]))) {
									ImGuiMCP::ImVec2 xSz{};
									ImGuiMCP::ImGui::CalcTextSize(&xSz, "x", nullptr, false, 0.0f);
									const float colW2 = ImGuiMCP::ImGui::GetColumnWidth(-1);
									const float padX2 = (colW2 - xSz.x) * 0.5f;
									if (padX2 > 0.0f) {
										ImGuiMCP::ImGui::SetCursorPosX(ImGuiMCP::ImGui::GetCursorPosX() + padX2);
									}
									ImGuiMCP::ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text,
										ImGuiMCP::ImVec4(1.0f, 0.45f, 0.35f, 1.0f));
									ImGuiMCP::ImGui::TextUnformatted("x");
									ImGuiMCP::ImGui::PopStyleColor();
									sfh.updateY1();
								}
							}
						}

						ImGuiMCP::ImGui::PopStyleVar();
						ImGuiMCP::ImGui::EndTable();

						// Draw frame from the table's actual bounding box so it doesn't
						// stack with inner cell borders (no padding rows added).
						ImGuiMCP::ImVec2 tblMin{}, tblMax{};
						ImGuiMCP::ImGui::GetItemRectMin(&tblMin);
						ImGuiMCP::ImGui::GetItemRectMax(&tblMax);
						ImGuiMCP::ImGui::ImDrawListManager::AddRect(
							sfh.drawList,
							ImGuiMCP::ImVec2{ ccLeftX, tblMin.y },
							ImGuiMCP::ImVec2{ ccRightX, tblMax.y },
							sfh.borderCol,
							kPrefCaptureTableStyle.setFrameRounding,
							ImGuiMCP::ImDrawFlags_RoundCornersAll,
							kPrefCaptureTableStyle.setBorderThickness);
					}
				}

					ImGuiMCP::ImGui::EndChild();  // CrossClearScroll

					// --- Click outside scroll area -> close modal ---
					if (crossClearJustOpened) {
						crossClearJustOpened = false;
					} else {
						ImGuiMCP::ImVec2 scrollMin{}, scrollMax{};
						ImGuiMCP::ImGui::GetItemRectMin(&scrollMin);
						ImGuiMCP::ImGui::GetItemRectMax(&scrollMax);
						if (ImGuiMCP::ImGui::IsMouseClicked(0)) {
							ImGuiMCP::ImVec2 mouse{};
							ImGuiMCP::ImGui::GetMousePos(&mouse);

							const bool insideScroll =
								mouse.x >= scrollMin.x && mouse.x <= scrollMax.x &&
								mouse.y >= scrollMin.y && mouse.y <= scrollMax.y;

							if (!insideScroll) {
								showCrossClearTable = false;
								ImGuiMCP::ImGui::CloseCurrentPopup();
							}
						}
					}

					ImGuiMCP::ImGui::EndPopup();
				}
				if (!modalOpen) {
					showCrossClearTable = false;
				}
			}

			if (!showCrossClearTable) {
				ImGuiMCP::ImGui::BeginChild(
					"PreferenceCaptureTableScroll",
					ImGuiMCP::ImVec2(0.0f, scrollChildH),
					ImGuiMCP::ImGuiChildFlags_None,
					ImGuiMCP::ImGuiWindowFlags_None);
				if (selectedActorID != 0 && ActorScopeExcludeState::IsExcluded(selectedActorID)) {
					ImGuiMCP::ImGui::Spacing();
					ImGuiMCP::ImGui::TextDisabled("%s", Localization::CStr("ui.actor_management.excluded_placeholder"));
				} else if (selectedActorID != 0) {
					static const EntryArray kEmptyEntries{};
					const auto it = byActor.find(selectedActorID);
					const auto& entries = (it != byActor.end()) ? it->second : kEmptyEntries;
					renderActorTable(selectedActorID, entries);
				} else {
					ImGuiMCP::ImGui::Spacing();
					ImGuiMCP::ImGui::TextDisabled("%s", Localization::CStr("ui.preference_capture.empty"));
				}
				ImGuiMCP::ImGui::EndChild();
			}

		}
	}

	//=============================================================================
	// [SECTION 11] Install / Uninstall
	//=============================================================================

	void MenuFrameworkSettings::Install()
	{
		if (g_registered) {
			return;
		}

		if (!SKSEMenuFramework::IsInstalled()) {
			return;
		}

		// Upstream header caches `menuFramework = GetModuleHandle(...)` at static init.
		// If the DLL loads after our plugin, that cached handle would be null and registration would no-op.
		// Without modifying upstream, we can force-load and refresh the cached handle in this TU.
		if (!menuFramework) {
			(void)::LoadLibraryW(L"Data\\SKSE\\Plugins\\SKSEMenuFramework.dll");
			menuFramework = ::GetModuleHandleW(L"SKSEMenuFramework");
			if (!menuFramework) {
				menuFramework = ::GetModuleHandleW(L"SKSEMenuFramework.dll");
			}
		}
		if (!menuFramework) {
			return;
		}

		Localization::Load();

		{
			const std::string kTitle{ Localization::CStr("ui.title") };

			SKSEMenuFramework::SetSection(kTitle + "/" + Localization::CStr("ui.section.actor_management"));
			SKSEMenuFramework::AddSectionItem(Localization::CStr("ui.section.preferences_and_spells"), RenderActorManagement);

			SKSEMenuFramework::SetSection(kTitle + "/" + Localization::CStr("ui.section.settings"));
			SKSEMenuFramework::AddSectionItem(Localization::CStr("ui.section.system"), RenderSystem);
			SKSEMenuFramework::AddSectionItem(Localization::CStr("ui.section.scope_and_access"), RenderScopeAndAccess);
			SKSEMenuFramework::AddSectionItem(Localization::CStr("ui.section.controls_and_ui"), RenderControlsAndUI);
			SKSEMenuFramework::AddSectionItem(Localization::CStr("ui.section.equip_mode"), RenderEquipMode);
			SKSEMenuFramework::AddSectionItem(Localization::CStr("ui.section.combat_equip"), RenderCombatEquip);
			SKSEMenuFramework::AddSectionItem(Localization::CStr("ui.section.equip_stability"), RenderEquipStability);
			SKSEMenuFramework::AddSectionItem(Localization::CStr("ui.section.extra_features"), RenderExtraFeatures);
			SKSEMenuFramework::AddSectionItem(Localization::CStr("ui.section.fixes"), RenderFixes);
		}
		g_registered = true;
	}

	void MenuFrameworkSettings::Uninstall()
	{
		// The framework header doesn't currently expose an unregister API for section items.
		// We intentionally keep the callback registered for the session.
	}
}
