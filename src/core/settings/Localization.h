// Localized UI strings with built-in defaults and optional external overrides.

#pragma once

#include <string>
#include <string_view>

namespace FEC::Localization
{
	// Loads built-in English defaults, then applies an external language file if present.
	// Safe to call multiple times.
	void Load();

	// Reloads defaults and external overrides from disk.
	void Reload();

	// Returns the key itself when no localized string exists.
	[[nodiscard]] const std::string& Get(std::string_view key);

	[[nodiscard]] const char* CStr(std::string_view key);
}
