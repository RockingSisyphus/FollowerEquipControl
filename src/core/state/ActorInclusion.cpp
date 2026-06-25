#include "ActorInclusion.h"

#include "Plugin.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

namespace FEC::ActorInclusion
{
	namespace
	{
		static constexpr std::size_t kModeCount = 4;

		[[nodiscard]] std::size_t ModeIndex(RE::ContainerMenu::ContainerMode a_mode)
		{
			return static_cast<std::size_t>(a_mode);
		}

		// Pending entries stay as strings until lazy pointer resolution.
		struct PendingEditorIDEntry
		{
			std::string editorID;
			RE::ContainerMenu::ContainerMode mode;
		};

		struct PendingFormIDEntry
		{
			std::string pluginName;
			RE::FormID rawID;
			RE::ContainerMenu::ContainerMode mode;
		};

		// BGSKeyword preserves EditorIDs at runtime, so both plain EditorID and Plugin|0xID tokens are supported.
		std::vector<PendingEditorIDEntry> g_pendingKeywordEditorIDs;
		std::vector<PendingFormIDEntry>   g_pendingKeywordFormIDs;
		// TESNPC and TESFaction do not retain EditorIDs in vanilla Skyrim, so NPC/Faction entries require Plugin.esp|0xLocalID tokens.
		std::vector<PendingFormIDEntry>   g_pendingNPCFormIDs;
		std::vector<PendingFormIDEntry>   g_pendingFactionFormIDs;

		std::vector<RE::BGSKeyword*>  g_perModeKeywords[kModeCount];
		std::vector<RE::FormID>       g_perModeFormIDs[kModeCount];  // full runtime FormIDs
		std::vector<RE::TESFaction*>  g_perModeFactions[kModeCount];

		bool g_pointersResolved{ false };

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

		[[nodiscard]] std::string ToLower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}

		[[nodiscard]] const char* ModeLabel(RE::ContainerMenu::ContainerMode a_mode)
		{
			switch (a_mode) {
			case RE::ContainerMenu::ContainerMode::kLoot:       return "Loot";
			case RE::ContainerMenu::ContainerMode::kSteal:      return "Steal";
			case RE::ContainerMenu::ContainerMode::kPickpocket: return "Pickpocket";
			case RE::ContainerMenu::ContainerMode::kNPCMode:    return "NPCTrade";
			default:                                            return "Unknown";
			}
		}

		struct ModeSuffix
		{
			const char* suffix;
			RE::ContainerMenu::ContainerMode mode;
		};

		static constexpr ModeSuffix kModeSuffixes[] = {
			{ "loot",        RE::ContainerMenu::ContainerMode::kLoot },
			{ "steal",       RE::ContainerMenu::ContainerMode::kSteal },
			{ "pickpocket",  RE::ContainerMenu::ContainerMode::kPickpocket },
			{ "npctrade",    RE::ContainerMenu::ContainerMode::kNPCMode },
		};

		[[nodiscard]] std::optional<RE::ContainerMenu::ContainerMode> SuffixToMode(std::string_view a_suffix)
		{
			const auto lower = ToLower(std::string(a_suffix));
			for (const auto& def : kModeSuffixes) {
				if (lower == def.suffix) {
					return def.mode;
				}
			}
			return std::nullopt;
		}

		enum class SectionType { kKeyword, kNPC, kFaction };

		struct ParsedSection
		{
			SectionType type;
			RE::ContainerMenu::ContainerMode mode;
		};

		[[nodiscard]] std::optional<ParsedSection> ClassifySection(const std::string& a_section)
		{
			const auto lower = ToLower(a_section);

			struct PrefixEntry
			{
				std::string_view base;
				SectionType      type;
			};
			static constexpr PrefixEntry kPrefixes[] = {
				{ "includebykeyword",  SectionType::kKeyword  },
				{ "includebynpc",      SectionType::kNPC      },
				{ "includebyfaction",  SectionType::kFaction  },
			};

			for (const auto& p : kPrefixes) {
				// No suffix -> default to NPCTrade.
				if (lower == p.base) {
					return ParsedSection{ p.type, RE::ContainerMenu::ContainerMode::kNPCMode };
				}
				if (lower.size() > p.base.size() + 1 &&
					lower.starts_with(p.base) &&
					lower[p.base.size()] == '.') {
					const auto suffix = lower.substr(p.base.size() + 1);
					if (auto mode = SuffixToMode(suffix)) {
						return ParsedSection{ p.type, *mode };
					}
				}
			}
			return std::nullopt;
		}

		[[nodiscard]] std::optional<std::filesystem::path> GetThisDllDirectory()
		{
			HMODULE module = nullptr;
			if (!GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&FEC::ActorInclusion::Load),
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

		// Actor inclusion INI directory: <dll_dir>/FollowerEquipControl/Actors/
		[[nodiscard]] std::optional<std::filesystem::path> GetActorInclusionDir()
		{
			auto dllDir = GetThisDllDirectory();
			if (!dllDir) {
				return std::nullopt;
			}
			return *dllDir / Plugin::NAME / "Actors";
		}

		[[nodiscard]] std::vector<std::string> SplitCSV(const std::string& value)
		{
			std::vector<std::string> result;
			std::istringstream stream(value);
			std::string token;
			while (std::getline(stream, token, ',')) {
				auto trimmed = Trim(token);
				if (!trimmed.empty()) {
					result.push_back(std::move(trimmed));
				}
			}
			return result;
		}

		// Parses Plugin.esp|0xLocalID tokens used by keyword, NPC, and faction entries.
		struct FormIDToken
		{
			std::string pluginName;
			RE::FormID rawID;
		};

		[[nodiscard]] std::optional<FormIDToken> ParseFormIDToken(
			const std::string& token, std::string_view a_context = "FormID")
		{
			const auto sep = token.find('|');
			if (sep == std::string::npos) {
				logger::warn("ActorInclusion: {} token '{}' missing '|' separator -- expected format: Plugin.esp|0x1234", a_context, token);
				return std::nullopt;
			}

			const auto pluginName = Trim(token.substr(0, sep));
			const auto idStr      = Trim(token.substr(sep + 1));
			if (pluginName.empty() || idStr.empty()) {
				logger::warn("ActorInclusion: malformed {} token '{}'", a_context, token);
				return std::nullopt;
			}

			// Require 0x prefix; base-0 parsing would treat a leading zero as octal.
			if (idStr.size() < 3 || idStr[0] != '0' || (idStr[1] != 'x' && idStr[1] != 'X')) {
				logger::warn(
					"ActorInclusion: {} value '{}' in token '{}' must use 0x prefix (e.g. 0x002B74)",
					a_context, idStr, token);
				return std::nullopt;
			}

			try {
				const RE::FormID rawID = static_cast<RE::FormID>(std::stoul(idStr, nullptr, 16));
				// Local IDs must exclude the mod index byte.
				// Regular plugins use the bottom 3 bytes; ESL plugins use the bottom 12 bits.
				if (rawID > 0x00FFFFFF) {
					logger::warn(
						"ActorInclusion: {} {:#010x} in token '{}' is larger than 0x00FFFFFF -- "
						"it likely includes the mod index byte. Provide only the local record number "
						"(up to 6 hex digits for regular plugins, up to 3 hex digits for ESL plugins).",
						a_context, rawID, token);
					return std::nullopt;
				}
				return FormIDToken{ pluginName, rawID };
			} catch (...) {
				logger::warn("ActorInclusion: could not parse {} value '{}' in token '{}'", a_context, idStr, token);
				return std::nullopt;
			}
		}

		void LoadOneFile(const std::filesystem::path& a_path)
		{
			std::ifstream file(a_path);
			if (!file.is_open()) {
				logger::warn("ActorInclusion: failed to open {}", a_path.string());
				return;
			}

			std::optional<ParsedSection> currentSection;
			std::string line;
			while (std::getline(file, line)) {
				line = Trim(std::move(line));
				if (line.empty() || line.front() == '#' || line.front() == ';') {
					continue;
				}

				if (line.front() == '[' && line.back() == ']') {
					const auto secName = Trim(line.substr(1, line.size() - 2));
					currentSection = ClassifySection(secName);
					continue;
				}

				if (!currentSection) {
					continue;
				}

				const auto eq = line.find('=');
				if (eq == std::string::npos) {
					continue;
				}

				const auto key = ToLower(Trim(line.substr(0, eq)));
				const auto val = Trim(line.substr(eq + 1));
				if (val.empty()) {
					continue;
				}

				// Plain EditorIDs are supported only where a_edList is non-null, currently only Keyword.
				auto dispatchTokens = [&](std::string_view a_context,
					std::vector<PendingEditorIDEntry>* a_edList,
					std::vector<PendingFormIDEntry>&   a_fmList)
				{
					for (const auto& token : SplitCSV(val)) {
						if (token.find('|') != std::string::npos) {
							if (auto parsed = ParseFormIDToken(token, a_context)) {
								logger::debug("ActorInclusion: queued {}{}|{:#010x} -> {}",
									a_context, parsed->pluginName, parsed->rawID,
									ModeLabel(currentSection->mode));
								a_fmList.push_back({ std::move(parsed->pluginName), parsed->rawID, currentSection->mode });
							}
						} else if (a_edList) {
							logger::debug("ActorInclusion: queued {} EditorID '{}' -> {}",
								a_context, token, ModeLabel(currentSection->mode));
							a_edList->push_back({ token, currentSection->mode });
						} else {
							logger::warn("ActorInclusion: {} token '{}' must use Plugin.esp|0xLocalID format "
								"(plain EditorIDs are not supported for {} -- the runtime does not "
								"retain EditorIDs for this form type)", a_context, token, a_context);
						}
					}
				};

				if (currentSection->type == SectionType::kKeyword && key == "keyword") {
					dispatchTokens("keyword", &g_pendingKeywordEditorIDs, g_pendingKeywordFormIDs);
				} else if (currentSection->type == SectionType::kNPC && key == "npc") {
					dispatchTokens("npc", nullptr, g_pendingNPCFormIDs);
				} else if (currentSection->type == SectionType::kFaction && key == "faction") {
					dispatchTokens("faction", nullptr, g_pendingFactionFormIDs);
				}
			}
		}

		void EnsureResolved()
		{
			if (g_pointersResolved) {
				return;
			}
			g_pointersResolved = true;
			for (auto& v : g_perModeKeywords)  v.clear();
			for (auto& v : g_perModeFormIDs)   v.clear();
			for (auto& v : g_perModeFactions)  v.clear();

			const bool anyPending =
				!g_pendingKeywordEditorIDs.empty() || !g_pendingKeywordFormIDs.empty() ||
				!g_pendingNPCFormIDs.empty()     ||
				!g_pendingFactionFormIDs.empty();
			if (!anyPending) {
				return;
			}

			auto* dh = RE::TESDataHandler::GetSingleton();
			if (!dh) {
				logger::warn("ActorInclusion: TESDataHandler unavailable -- cannot resolve entries");
				return;
			}

			// Keyword EditorIDs require scanning the BGSKeyword form array.
			if (!g_pendingKeywordEditorIDs.empty()) {
				auto& allKeywords = dh->GetFormArray<RE::BGSKeyword>();
				for (const auto& entry : g_pendingKeywordEditorIDs) {
					RE::BGSKeyword* kw = nullptr;
					for (auto* form : allKeywords) {
						if (form) {
							const char* edID = form->GetFormEditorID();
							if (edID && entry.editorID == edID) {
								kw = form;
								break;
							}
						}
					}
					if (kw) {
						const auto idx = ModeIndex(entry.mode);
						if (idx < kModeCount) g_perModeKeywords[idx].push_back(kw);
						logger::info("ActorInclusion: resolved keyword '{}' -> {}", entry.editorID,
							ModeLabel(entry.mode));
					} else {
						logger::warn("ActorInclusion: keyword '{}' not found -- skipped", entry.editorID);
					}
				}
			}

			for (const auto& entry : g_pendingKeywordFormIDs) {
				auto* kw = dh->LookupForm<RE::BGSKeyword>(entry.rawID, entry.pluginName);
				if (kw) {
					const auto idx = ModeIndex(entry.mode);
					if (idx < kModeCount) g_perModeKeywords[idx].push_back(kw);
					logger::info("ActorInclusion: resolved keyword {}|{:#010x} [{}] -> {}",
						entry.pluginName, entry.rawID, kw->GetFormEditorID() ? kw->GetFormEditorID() : "?",
						ModeLabel(entry.mode));
				} else {
					logger::warn("ActorInclusion: keyword {}|{:#010x} not found -- skipped (plugin not loaded?)",
						entry.pluginName, entry.rawID);
				}
			}

			// NPC entries are matched against both the actor reference FormID and base NPC FormID.
			for (const auto& entry : g_pendingNPCFormIDs) {
				auto* form = dh->LookupForm(entry.rawID, entry.pluginName);
				if (form) {
					const auto idx = ModeIndex(entry.mode);
					if (idx < kModeCount) g_perModeFormIDs[idx].push_back(form->GetFormID());
					const char* edID = form->GetFormEditorID();
					logger::info("ActorInclusion: resolved NPC {}|{:#010x} [{}] -> {}",
						entry.pluginName, entry.rawID, edID ? edID : "?", ModeLabel(entry.mode));
				} else {
					logger::warn("ActorInclusion: NPC {}|{:#010x} not found -- skipped (plugin not loaded?)",
						entry.pluginName, entry.rawID);
				}
			}

			for (const auto& entry : g_pendingFactionFormIDs) {
				auto* fac = dh->LookupForm<RE::TESFaction>(entry.rawID, entry.pluginName);
				if (fac) {
					const auto idx = ModeIndex(entry.mode);
					if (idx < kModeCount) g_perModeFactions[idx].push_back(fac);
					const char* edID = fac->GetFormEditorID();
					logger::info("ActorInclusion: resolved faction {}|{:#010x} [{}] -> {}",
						entry.pluginName, entry.rawID, edID ? edID : "?", ModeLabel(entry.mode));
				} else {
					logger::warn("ActorInclusion: faction {}|{:#010x} not found -- skipped (plugin not loaded?)",
						entry.pluginName, entry.rawID);
				}
			}

			std::size_t kwCount  = 0; for (const auto& v : g_perModeKeywords)  kwCount  += v.size();
			std::size_t idCount  = 0; for (const auto& v : g_perModeFormIDs)   idCount  += v.size();
			std::size_t facCount = 0; for (const auto& v : g_perModeFactions)  facCount += v.size();
			logger::info("ActorInclusion: {} keyword(s), {} NPC(s), {} faction(s) resolved",
				kwCount, idCount, facCount);
		}
	}

	void Load()
	{
		g_pendingKeywordEditorIDs.clear(); g_pendingKeywordFormIDs.clear();
		g_pendingNPCFormIDs.clear();
		g_pendingFactionFormIDs.clear();
		for (auto& v : g_perModeKeywords)  v.clear();
		for (auto& v : g_perModeFormIDs)   v.clear();
		for (auto& v : g_perModeFactions)  v.clear();
		g_pointersResolved = false;

		const auto dir = GetActorInclusionDir();
		if (!dir) {
			logger::debug("ActorInclusion: could not determine inclusion directory");
			return;
		}

		if (!std::filesystem::exists(*dir)) {
			logger::debug("ActorInclusion: directory not found: {}", dir->string());
			return;
		}

		std::size_t fileCount = 0;
		std::error_code ec;
		for (const auto& entry : std::filesystem::directory_iterator(*dir, ec)) {
			if (ec) {
				logger::warn("ActorInclusion: directory iteration error: {}", ec.message());
				break;
			}
			bool isFile = entry.is_regular_file(ec);
			if (ec) { ec.clear(); continue; }
			if (!isFile) continue;
			if (ToLower(entry.path().extension().string()) != ".ini") continue;
			LoadOneFile(entry.path());
			++fileCount;
		}

		if (fileCount == 0) {
			logger::info("ActorInclusion: no .ini files found in {}", dir->string());
			return;
		}

		const auto kwTotal  = g_pendingKeywordEditorIDs.size() + g_pendingKeywordFormIDs.size();
		const auto npcTotal = g_pendingNPCFormIDs.size();
		const auto facTotal = g_pendingFactionFormIDs.size();
		if (kwTotal == 0 && npcTotal == 0 && facTotal == 0) {
			logger::info("ActorInclusion: {} file(s) parsed, no entries configured", fileCount);
		} else {
			logger::info("ActorInclusion: {} file(s) parsed, {} keyword(s), {} NPC(s), {} faction(s) pending",
				fileCount, kwTotal, npcTotal, facTotal);
		}
	}

	bool MatchesForMode(RE::Actor* a_actor, RE::ContainerMenu::ContainerMode a_mode) noexcept
	{
		EnsureResolved();

		const auto idx = ModeIndex(a_mode);
		if (!a_actor || idx >= kModeCount) {
			return false;
		}

		for (auto* kw : g_perModeKeywords[idx]) {
			if (a_actor->HasKeyword(kw)) {
				return true;
			}
		}

		if (!g_perModeFormIDs[idx].empty()) {
			const auto refID  = a_actor->GetFormID();
			auto* base = a_actor->GetActorBase();
			const auto baseID = base ? base->GetFormID() : 0;
			for (const auto id : g_perModeFormIDs[idx]) {
				if (refID == id || baseID == id) {
					return true;
				}
			}
		}

		for (auto* faction : g_perModeFactions[idx]) {
			if (a_actor->IsInFaction(faction)) {
				return true;
			}
		}

		return false;
	}

	bool MatchesAny(RE::Actor* a_actor) noexcept
	{
		EnsureResolved();

		if (!a_actor) {
			return false;
		}

		const auto refID  = a_actor->GetFormID();
		auto* base = a_actor->GetActorBase();
		const auto baseID = base ? base->GetFormID() : 0;

		for (std::size_t idx = 0; idx < kModeCount; ++idx) {
			for (auto* kw : g_perModeKeywords[idx]) {
				if (a_actor->HasKeyword(kw)) {
					return true;
				}
			}
			for (const auto id : g_perModeFormIDs[idx]) {
				if (refID == id || baseID == id) {
					return true;
				}
			}
			for (auto* faction : g_perModeFactions[idx]) {
				if (a_actor->IsInFaction(faction)) {
					return true;
				}
			}
		}

		return false;
	}
}
