// Precompiled header: common, low-churn includes shared across the plugin.

#pragma once

#include <RE/Skyrim.h>
#include <REL/Relocation.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include <format>
#include <string_view>
#include <vector>

using namespace std::literals;

namespace logger = SKSE::log;
namespace util = SKSE::stl;

#define DLLEXPORT __declspec(dllexport)
