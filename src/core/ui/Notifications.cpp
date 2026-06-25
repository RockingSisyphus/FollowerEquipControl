#include "Notifications.h"

#include "Localization.h"
#include "PluginSettings.h"

#include <RE/M/Misc.h>

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace FEC::Notifications
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		std::mutex g_mutex;
		std::unordered_map<std::string, Clock::time_point> g_lastShown;

		[[nodiscard]] bool ShouldShow(std::string_view a_throttleKey, std::chrono::milliseconds a_throttle)
		{
			if (a_throttle.count() <= 0 || a_throttleKey.empty()) {
				return true;
			}

			const auto now = Clock::now();
			std::scoped_lock lock(g_mutex);

			auto it = g_lastShown.find(std::string(a_throttleKey));
			if (it != g_lastShown.end()) {
				if ((now - it->second) < a_throttle) {
					return false;
				}
				it->second = now;
				return true;
			}

			g_lastShown.emplace(std::string(a_throttleKey), now);
			return true;
		}

		void DoToast(std::string a_message, ToastOptions a_options)
		{
			if (a_message.empty()) {
				return;
			}

			RE::DebugNotification(a_message.c_str(), a_options.soundToPlay, a_options.cancelIfAlreadyQueued);
		}

		void SubmitToast(std::string a_message, ToastOptions a_options)
		{
			if (!a_options.runOnMainThread) {
				DoToast(std::move(a_message), a_options);
				return;
			}

			if (auto* taskInterface = SKSE::GetTaskInterface()) {
				taskInterface->AddTask([msg = std::move(a_message), opt = a_options]() mutable {
					DoToast(std::move(msg), opt);
				});
				return;
			}

			// Fall back: best effort.
			DoToast(std::move(a_message), a_options);
		}
	}

	void Toast(std::string_view a_message, const ToastOptions& a_options, std::string_view a_throttleKey)
	{
		if (!FEC::PluginSettings::Get().uiFeedback.enableNotifications) {
			return;
		}

		if (!ShouldShow(a_throttleKey, a_options.throttle)) {
			return;
		}

		SubmitToast(std::string(a_message), a_options);
	}

	void ToastKey(std::string_view a_localizationKey, const ToastOptions& a_options, std::string_view a_throttleKey)
	{
		Toast(FEC::Localization::Get(a_localizationKey), a_options, a_throttleKey.empty() ? a_localizationKey : a_throttleKey);
	}
}
