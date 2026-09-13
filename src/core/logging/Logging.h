// Logging setup and runtime log-level control.

#pragma once

#include "PCH.h"

namespace FEC::Logging
{
	// Initializes spdlog and SKSE sinks.
	// Reads [Logging] LogLevel from <SkyrimData>\SKSE\Plugins\<PluginName>.ini if present.
	// Valid values: trace, debug, info, warn, error, critical, off.
	void Initialize(spdlog::level::level_enum a_defaultLevel = spdlog::level::info);

	// Updates the active log level after Initialize().
	void SetLevel(spdlog::level::level_enum a_level);
	void SetLevel(std::string_view a_level);
}
