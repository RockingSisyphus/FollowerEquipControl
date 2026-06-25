// Input bindings and key/button queries for keyboard and gamepad.

#pragma once

#include "PCH.h"

#include <cstdint>
#include <optional>
#include <string>

namespace FEC::Controls
{
	// DirectInput scan code used by SkyUI/SKSE. Default: 29 = Left Ctrl.
	inline constexpr std::uint32_t kModKeyDik = 29;

	// SKSE gamepad keycodes are raw button IDs plus this offset.
	inline constexpr std::uint32_t kGamepadOffset = 266;

	[[nodiscard]] std::uint32_t GetModKeyDik();
	[[nodiscard]] bool IsDikKeyDown(std::uint32_t a_dikScanCode);
	[[nodiscard]] std::string GetDikKeyName(std::uint32_t a_dikScanCode);

	// Installs the BSInputDeviceManager event sink once.
	void Install();

	// Removes the gamepad state tracking sink. Safe to call if not installed.
	void Uninstall();

	// Configured gamepad keycodes use SKSE's unified range, 266-281.
	[[nodiscard]] std::uint32_t GetGamepadModKey();
	[[nodiscard]] std::uint32_t GetGamepadRightHandKey();
	[[nodiscard]] std::uint32_t GetGamepadLeftHandKey();
	[[nodiscard]] std::uint32_t GetGamepadRechargeKey();

	// True if the specified gamepad SKSE keycode is currently held.
	[[nodiscard]] bool IsGamepadButtonDown(std::uint32_t a_skseKeycode);

	// Best-effort display name for a gamepad SKSE keycode.
	[[nodiscard]] std::string GetGamepadButtonName(std::uint32_t a_skseKeycode);

	[[nodiscard]] bool IsModKeyDown();

	// True when Skyrim's "Left Attack/Block" binding is held, or when the gamepad hand override selects left hand.
	[[nodiscard]] bool IsLeftHandKeyDown();

	// Programmatic equip triggers set a hand choice; Scaleform-side code writes it and the game thread consumes it.
	void SetHandOverrideLeft();
	void SetHandOverrideRight();
	void ClearHandOverride();
	[[nodiscard]] std::optional<bool> GetHandOverrideIsLeft();

	// Reads and clears the hand override atomically.
	[[nodiscard]] std::optional<bool> ConsumeHandOverride();
}
