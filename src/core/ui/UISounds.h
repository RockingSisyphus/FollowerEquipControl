// Crash-safe UI audio feedback using vanilla sounds.

#pragma once

#include "PCH.h"

namespace FEC::UISounds
{
	[[nodiscard]] bool Enabled();

	// Stable base-game sound FormIDs used with BSAudioManager::Play.
	namespace SoundFormID
	{
		inline constexpr RE::FormID kSpellLearned = 0x000ECF93;
		inline constexpr RE::FormID kAlchemyLearnEffect = 0x000C8C75;
		inline constexpr RE::FormID kEnchantRecharge = 0x00000EE5;
		inline constexpr RE::FormID kActivateFail = 0x0006D1C6;
	}

	enum class Action : std::uint8_t
	{
		kPickup,
		kPutdown,
		kUse
	};

	// Best-effort playback by sound FormID.
	void PlaySoundByFormID(RE::FormID a_soundFormID);

	// Uses BSAudioManager::Play directly; avoids BSSoundHandle/output-model setup.
	void PlayForObject(RE::TESBoundObject* a_object, Action a_action);
}
