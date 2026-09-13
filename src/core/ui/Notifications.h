// Centralized, throttled, thread-safe HUD notifications.

#pragma once

#include "PCH.h"

#include "Localization.h"

#include <chrono>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace FEC::Notifications
{
	namespace detail
	{
		[[nodiscard]] inline bool HasFormatFields(std::string_view a_fmt)
		{
			for (std::size_t i = 0; i < a_fmt.size(); ++i) {
				if (a_fmt[i] == '{') {
					if ((i + 1) < a_fmt.size() && a_fmt[i + 1] == '{') {
						++i;
						continue;
					}
					return true;
				}
				if (a_fmt[i] == '}') {
					if ((i + 1) < a_fmt.size() && a_fmt[i + 1] == '}') {
						++i;
						continue;
					}
				}
			}
			return false;
		}
	}

	enum class Severity
	{
		kInfo,
		kWarning,
		kError
	};

	struct ToastOptions
	{
		Severity severity{ Severity::kInfo };
		std::chrono::milliseconds throttle{ 0 };

		// Passed to RE::DebugNotification.
		const char* soundToPlay{ nullptr };
		bool cancelIfAlreadyQueued{ true };

		// Queue through SKSE's task interface when called off the game thread.
		bool runOnMainThread{ true };
	};

	void Toast(std::string_view a_message, const ToastOptions& a_options = {}, std::string_view a_throttleKey = {});

	void ToastKey(std::string_view a_localizationKey, const ToastOptions& a_options = {}, std::string_view a_throttleKey = {});

	// Formats the localized string only when it contains std::format-style fields.
	// This lets translators omit placeholders without breaking the toast.
	template <class... Args>
	void ToastKeyFmtThrottled(std::string_view a_localizationKey, const ToastOptions& a_options, std::string_view a_throttleKey, Args&&... a_args)
	{
		const auto& fmt = FEC::Localization::Get(a_localizationKey);
		if (!detail::HasFormatFields(std::string_view(fmt))) {
			ToastKey(a_localizationKey, a_options, a_throttleKey);
			return;
		}
		std::string msg;
		try {
			auto stored = std::tuple<std::decay_t<Args>...>(std::forward<Args>(a_args)...);
			msg = std::apply(
				[&](auto&... xs) {
					return std::vformat(std::string_view(fmt), std::make_format_args(xs...));
				},
				stored);
		} catch (const std::format_error&) {
			// Translator-edited braces should not prevent the notification from showing.
			msg = std::string(fmt);
		}

		Toast(msg, a_options, a_throttleKey);
	}

	// Uses the localization key as the throttle key.
	template <class... Args>
	void ToastKeyFmt(std::string_view a_localizationKey, const ToastOptions& a_options, Args&&... a_args)
	{
		ToastKeyFmtThrottled(a_localizationKey, a_options, a_localizationKey, std::forward<Args>(a_args)...);
	}
}
