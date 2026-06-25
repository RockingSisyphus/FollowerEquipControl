#include "Controls.h"

#include "PluginSettings.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <RE/B/BSInputDeviceManager.h>
#include <RE/B/ButtonEvent.h>
#include <SKSE/InputMap.h>

#include <array>
#include <atomic>
#include <optional>
#include <string>

namespace FEC::Controls
{
	namespace
	{
		std::atomic_uint16_t g_gamepadButtonState{ 0 };
		bool g_gamepadSinkInstalled{ false };

		// Hand override set by Scaleform-side equip triggers and consumed on the game thread.
		// Values: -1 = no override, 0 = right hand, 1 = left hand.
		std::atomic<std::int8_t> g_handOverride{ -1 };

		class GamepadStateSink final : public RE::BSTEventSink<RE::InputEvent*>
		{
		public:
			static GamepadStateSink* GetSingleton()
			{
				static GamepadStateSink s;
				return std::addressof(s);
			}

			RE::BSEventNotifyControl ProcessEvent(
				RE::InputEvent* const* a_event,
				RE::BSTEventSource<RE::InputEvent*>* /*a_eventSource*/) override
			{
				if (!a_event) {
					return RE::BSEventNotifyControl::kContinue;
				}
				for (auto ev = *a_event; ev; ev = ev->next) {
					auto* btn = ev->AsButtonEvent();
					if (!btn) {
						continue;
					}
					if (btn->GetDevice() != RE::INPUT_DEVICE::kGamepad) {
						continue;
					}
					// GetIDCode() returns raw gamepad masks; convert to SKSE keycodes before indexing.
					const auto rawMask = btn->GetIDCode();
					const auto skseKey = SKSE::InputMap::GamepadMaskToKeycode(rawMask);
					if (skseKey < kGamepadOffset || skseKey >= kGamepadOffset + 16) {
						continue;
					}
					const auto idx = skseKey - kGamepadOffset;
					const auto mask = static_cast<std::uint16_t>(1u << idx);
					if (btn->IsDown()) {
						const auto prev = g_gamepadButtonState.fetch_or(mask);
						if (!(prev & mask) && spdlog::should_log(spdlog::level::trace)) {
							logger::trace("GamepadSink: DOWN raw=0x{:04X} skse={} idx={}", rawMask, skseKey, idx);
						}
					} else if (btn->IsUp()) {
						const auto prev = g_gamepadButtonState.fetch_and(static_cast<std::uint16_t>(~mask));
						if ((prev & mask) && spdlog::should_log(spdlog::level::trace)) {
							logger::trace("GamepadSink: UP   raw=0x{:04X} skse={} idx={}", rawMask, skseKey, idx);
						}
					}
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};
	}

	std::uint32_t GetModKeyDik()
	{
		const auto configured = PluginSettings::Get().keyboardControls.modKeyDik;
		return configured != 0 ? configured : kModKeyDik;
	}

	bool IsDikKeyDown(std::uint32_t a_dikScanCode)
	{
		const auto vk = ::MapVirtualKeyA(a_dikScanCode, MAPVK_VSC_TO_VK_EX);
		if (vk == 0) {
			return false;
		}
		return (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
	}

	std::string GetDikKeyName(std::uint32_t a_dikScanCode)
	{
		if (a_dikScanCode == 0) {
			return {};
		}

		const std::uint32_t baseScan = a_dikScanCode & 0x7Fu;
		const bool extended = (a_dikScanCode & 0x80u) != 0;

		LONG lParam = static_cast<LONG>(baseScan << 16);
		if (extended) {
			lParam |= (1L << 24);
		}

		std::array<wchar_t, 64> wbuf{};
		const int wlen = ::GetKeyNameTextW(lParam, wbuf.data(), static_cast<int>(wbuf.size()));
		if (wlen <= 0) {
			return {};
		}

		const int bytes = ::WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, nullptr, 0, nullptr, nullptr);
		if (bytes <= 0) {
			return {};
		}

		std::string out(static_cast<std::size_t>(bytes), '\0');
		const int written = ::WideCharToMultiByte(CP_UTF8, 0, wbuf.data(), wlen, out.data(), bytes, nullptr, nullptr);
		if (written <= 0) {
			return {};
		}
		return out;
	}

	void Install()
	{
		if (g_gamepadSinkInstalled) {
			return;
		}
		auto* input = RE::BSInputDeviceManager::GetSingleton();
		if (!input) {
			return;
		}
		input->AddEventSink(GamepadStateSink::GetSingleton());
		g_gamepadSinkInstalled = true;
		logger::info("Controls: installed gamepad state tracking");
	}

	void Uninstall()
	{
		if (!g_gamepadSinkInstalled) {
			return;
		}
		auto* input = RE::BSInputDeviceManager::GetSingleton();
		if (input) {
			input->RemoveEventSink(GamepadStateSink::GetSingleton());
		}
		g_gamepadButtonState.store(0, std::memory_order_relaxed);
		g_gamepadSinkInstalled = false;
		logger::info("Controls: uninstalled gamepad state tracking");
	}

	std::uint32_t GetGamepadModKey()
	{
		const auto configured = PluginSettings::Get().gamepadControls.gamepadModKey;
		return (configured >= kGamepadOffset && configured < kGamepadOffset + 16) ? configured : 280;
	}

	std::uint32_t GetGamepadRightHandKey()
	{
		const auto configured = PluginSettings::Get().gamepadControls.gamepadRightHandKey;
		return (configured >= kGamepadOffset && configured < kGamepadOffset + 16) ? configured : 276;
	}

	std::uint32_t GetGamepadLeftHandKey()
	{
		const auto configured = PluginSettings::Get().gamepadControls.gamepadLeftHandKey;
		return (configured >= kGamepadOffset && configured < kGamepadOffset + 16) ? configured : 278;
	}

	std::uint32_t GetGamepadRechargeKey()
	{
		const auto configured = PluginSettings::Get().weaponEnchantmentRecharge.gamepadRechargeKey;
		return (configured >= kGamepadOffset && configured < kGamepadOffset + 16) ? configured : 279;
	}

	bool IsGamepadButtonDown(std::uint32_t a_skseKeycode)
	{
		if (a_skseKeycode < kGamepadOffset || a_skseKeycode >= kGamepadOffset + 16) {
			return false;
		}
		const auto idx = a_skseKeycode - kGamepadOffset;
		return (g_gamepadButtonState.load(std::memory_order_relaxed) & (1u << idx)) != 0;
	}

	std::string GetGamepadButtonName(std::uint32_t a_skseKeycode)
	{
		switch (a_skseKeycode) {
		case 266: return "D-pad Up";
		case 267: return "D-pad Down";
		case 268: return "D-pad Left";
		case 269: return "D-pad Right";
		case 270: return "Start";
		case 271: return "Back";
		case 272: return "L3";
		case 273: return "R3";
		case 274: return "LB";
		case 275: return "RB";
		case 276: return "A";
		case 277: return "B";
		case 278: return "X";
		case 279: return "Y";
		case 280: return "LT";
		case 281: return "RT";
		default: return {};
		}
	}

	bool IsModKeyDown()
	{
		return IsDikKeyDown(GetModKeyDik()) || IsGamepadButtonDown(GetGamepadModKey());
	}

	bool IsLeftHandKeyDown()
	{
		auto override_ = GetHandOverrideIsLeft();
		if (override_) {
			return *override_;
		}

		auto* controlMap = RE::ControlMap::GetSingleton();
		if (!controlMap) {
			return (::GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
		}

		const auto kbKey = controlMap->GetMappedKey(
			"Left Attack/Block", RE::INPUT_DEVICE::kKeyboard);
		if (kbKey != RE::ControlMap::kInvalid && IsDikKeyDown(kbKey)) {
			return true;
		}

		const auto mouseKey = controlMap->GetMappedKey(
			"Left Attack/Block", RE::INPUT_DEVICE::kMouse);
		if (mouseKey != RE::ControlMap::kInvalid) {
			int vk = 0;
			switch (mouseKey) {
			case 0: vk = VK_LBUTTON; break;
			case 1: vk = VK_RBUTTON; break;
			case 2: vk = VK_MBUTTON; break;
			case 3: vk = VK_XBUTTON1; break;
			case 4: vk = VK_XBUTTON2; break;
			default: break;
			}
			if (vk != 0 && (::GetAsyncKeyState(vk) & 0x8000) != 0) {
				return true;
			}
		}

		if (IsGamepadButtonDown(GetGamepadLeftHandKey())) {
			return true;
		}

		return false;
	}

	void SetHandOverrideLeft()
	{
		g_handOverride.store(1, std::memory_order_release);
	}

	void SetHandOverrideRight()
	{
		g_handOverride.store(0, std::memory_order_release);
	}

	void ClearHandOverride()
	{
		g_handOverride.store(-1, std::memory_order_release);
	}

	std::optional<bool> GetHandOverrideIsLeft()
	{
		const auto val = g_handOverride.load(std::memory_order_acquire);
		if (val < 0) return std::nullopt;
		return val != 0;
	}

	std::optional<bool> ConsumeHandOverride()
	{
		const auto val = g_handOverride.exchange(-1, std::memory_order_acq_rel);
		if (val < 0) return std::nullopt;
		return val != 0;
	}
}
