// SKSE entry point: initializes logging, settings, and serialization, then installs features on engine lifecycle messages.

#include "PCH.h"

#include "Plugin.h"

#include "FeatureRegistry.h"
#include "ActorInclusion.h"
#include "PostLoadStateScan.h"
#include "PluginSettings.h"
#include "Logging.h"
#include "Serialization.h"

#include <memory>

namespace
{
  void OnSkseMessage(SKSE::MessagingInterface::Message* a_message) noexcept
  {
    if (!a_message) {
      return;
    }

    switch (a_message->type) {
    case SKSE::MessagingInterface::kNewGame:
      logger::debug("kNewGame");
      FEC::FeatureRegistry::InstallAllFeatures();
      break;
    case SKSE::MessagingInterface::kPostLoadGame:
      logger::debug("kPostLoadGame");
      FEC::FeatureRegistry::InstallAllFeatures();
      FEC::PostLoadStateScan::Run();
      break;
    case SKSE::MessagingInterface::kDataLoaded:
      logger::debug("kDataLoaded");
      FEC::ActorInclusion::Load();
      FEC::FeatureRegistry::InstallAllFeatures();
      break;
    case SKSE::MessagingInterface::kInputLoaded:
      logger::debug("kInputLoaded");
      break;
    default:
      break;
    }
  }
}

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Load(const SKSE::LoadInterface* a_skse)
{
  FEC::Logging::Initialize(spdlog::level::info);

#ifndef SKYRIMVR
  logger::info("Loaded plugin {} {}", Plugin::NAME, Plugin::VERSION.string());
#else
  logger::error("Loaded plugin {} {} for Skyrim VR", Plugin::NAME, Plugin::VERSION.string());
#ifndef FEC_ENABLE_UNSUPPORTED_VR
  logger::error("Skyrim VR is currently not supported. The plugin will not load.");
  logger::error("(For development only) Rebuild with -DFEC_ENABLE_UNSUPPORTED_VR=ON to force load.");
  return false;
#else
  logger::warn("Skyrim VR is currently unsupported; proceeding due to FEC_ENABLE_UNSUPPORTED_VR.");
  logger::warn("VR Address Library may be required depending on hooks used: https://www.nexusmods.com/skyrimspecialedition/mods/58101");
#endif
#endif

  SKSE::Init(a_skse);

  FEC::PluginSettings::Load();

  FEC::Serialization::Install();

  if (const auto messaging = SKSE::GetMessagingInterface()) {
    messaging->RegisterListener(OnSkseMessage);
  }

  return true;
}

extern "C" DLLEXPORT constinit auto SKSEPlugin_Version = []() noexcept {
  SKSE::PluginVersionData v;
  v.PluginName(Plugin::NAME.data());
  v.PluginVersion(Plugin::VERSION);
  v.UsesAddressLibrary(true);
  v.HasNoStructUse();
  return v;
}();

extern "C" DLLEXPORT bool SKSEAPI SKSEPlugin_Query(const SKSE::QueryInterface*, SKSE::PluginInfo* a_info)
{
  a_info->name = SKSEPlugin_Version.pluginName;
  a_info->infoVersion = SKSE::PluginInfo::kVersion;
  a_info->version = SKSEPlugin_Version.pluginVersion;
  return true;
}
