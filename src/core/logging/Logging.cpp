#include "Logging.h"

#include "Plugin.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

namespace FEC::Logging
{
	namespace
	{
			[[nodiscard]] std::optional<std::filesystem::path> GetThisDllDirectory()
			{
				HMODULE module = nullptr;
				if (!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&FEC::Logging::Initialize),
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

				std::filesystem::path iniPath = *dllDir / (std::string(Plugin::NAME) + ".ini");
				return iniPath;
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

		[[nodiscard]] spdlog::level::level_enum ParseLogLevel(std::string s, spdlog::level::level_enum a_fallback)
		{
			s = Trim(std::move(s));
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			if (s == "trace") {
				return spdlog::level::trace;
			}
			if (s == "debug") {
				return spdlog::level::debug;
			}
			if (s == "info") {
				return spdlog::level::info;
			}
			if (s == "warn" || s == "warning") {
				return spdlog::level::warn;
			}
			if (s == "err" || s == "error") {
				return spdlog::level::err;
			}
			if (s == "critical") {
				return spdlog::level::critical;
			}
			if (s == "off") {
				return spdlog::level::off;
			}

			return a_fallback;
		}

		[[nodiscard]] spdlog::level::level_enum LoadConfiguredLogLevel(spdlog::level::level_enum a_default)
		{
			// SKSE convention: read config next to the DLL; fall back to the compiled-in default.
			auto iniPath = GetIniPathNearDll();
			if (!iniPath) {
				return a_default;
			}

			std::ifstream in(*iniPath);
			if (!in.is_open()) {
				return a_default;
			}

			std::string currentSection;
			std::string line;
			while (std::getline(in, line)) {
				line = Trim(std::move(line));
				if (line.empty() || line.starts_with('#') || line.starts_with(';')) {
					continue;
				}

				if (line.front() == '[' && line.back() == ']') {
					currentSection = Trim(line.substr(1, line.size() - 2));
					std::transform(currentSection.begin(), currentSection.end(), currentSection.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					continue;
				}

				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}

				auto key = Trim(line.substr(0, eq));
				auto val = Trim(line.substr(eq + 1));
				std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

				if (currentSection == "logging" && key == "loglevel") {
					return ParseLogLevel(std::move(val), a_default);
				}
			}

			return a_default;
		}
	}

	void Initialize(spdlog::level::level_enum a_defaultLevel)
	{
		const auto level = LoadConfiguredLogLevel(a_defaultLevel);

#ifdef NDEBUG
		auto path = logger::log_directory();
		if (!path) {
			util::report_and_fail("Failed to find standard logging directory"sv);
		}

		*path /= std::format("{}.log"sv, Plugin::NAME);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
		auto log = std::make_shared<spdlog::logger>("global log"s, std::move(sink));
#else
		std::vector<spdlog::sink_ptr> sinks;
		sinks.push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());

		if (auto path = logger::log_directory(); path) {
			*path /= std::format("{}.log"sv, Plugin::NAME);
			sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true));
		}

		auto log = std::make_shared<spdlog::logger>("global log"s, sinks.begin(), sinks.end());
#endif

		log->set_level(level);

		// Sync-flush warn+ so important messages survive crashes.
		// Trace/debug rely on the 1s periodic flush; per-message fsync contributed
		// to stack overflow in hook-heavy combat under usvfs.
		log->flush_on(spdlog::level::warn);
		spdlog::flush_every(std::chrono::seconds(1));

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

		logger::info("Log level: {}", spdlog::level::to_string_view(level));
	}

	void SetLevel(spdlog::level::level_enum a_level)
	{
		if (auto* log = spdlog::default_logger_raw(); log) {
			log->set_level(a_level);
			// Trace/debug sessions flush info+; normal runs keep warn for performance.
			const auto flushLevel = (a_level <= spdlog::level::debug)
				? spdlog::level::info
				: spdlog::level::warn;
			log->flush_on(flushLevel);
		}
		spdlog::set_level(a_level);

		logger::info("Log level set to: {}", spdlog::level::to_string_view(a_level));
	}

	void SetLevel(std::string_view a_level)
	{
		auto s = std::string(a_level);
		const auto parsed = ParseLogLevel(std::move(s), spdlog::level::info);
		SetLevel(parsed);
	}
}
