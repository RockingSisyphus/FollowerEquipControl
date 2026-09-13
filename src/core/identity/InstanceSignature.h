// Captures per-instance extra-data traits at selection time for later re-identification without raw pointers.

#pragma once

#include <cstdint>
#include <string>

namespace RE
{
	class ExtraDataList;
	class TESBoundObject;
}

namespace FEC
{
	struct InstanceSignature
	{
		enum class EquipState : std::uint8_t
		{
			kUnknown,
			kNotWorn,
			kWornRight,
			kWornLeft
		};

		// Presence flags are constraints; false means the candidate must not have that extra.
		bool hasTextDisplayData{ false };
		bool hasCustomName{ false };
		std::string customName{};
		bool hasTemperFactor{ false };
		float temperFactor{ 0.0f };

		bool hasPoisonExtra{ false };
		bool hasPoisonFormID{ false };
		std::uint32_t poisonFormID{ 0 };
		bool hasPoisonCount{ false };
		std::uint32_t poisonCount{ 0 };

		bool hasEnchantmentExtra{ false };
		bool hasEnchantmentFormID{ false };
		std::uint32_t enchantmentFormID{ 0 };
		bool hasEnchantmentCharge{ false };
		std::uint16_t enchantmentCharge{ 0 };
		bool hasRemoveOnUnequip{ false };
		bool removeOnUnequip{ false };

		bool hasChargeExtra{ false };
		float charge{ 0.0f };
		bool hasHealthExtra{ false };
		float health{ 0.0f };

		// Set when a UI row had multiple same-name xLists and the first was kept deterministically.
		// Equip state is not unique enough for swap-vs-dual-wield decisions in that case.
		bool capturedFromNonUniqueRow{ false };

		EquipState equipState{ EquipState::kUnknown };

		[[nodiscard]] bool IsMeaningful() const noexcept;
		[[nodiscard]] bool HasStableIdentity() const noexcept;
	};

	[[nodiscard]] InstanceSignature BuildInstanceSignature(const RE::ExtraDataList& a_extraList, RE::TESBoundObject* a_baseObject);

	// Keep this in sync with SignatureResolve's stable-identity subset:
	// text/custom name, temper factor, and enchantment identity/charge.
	// Volatile runtime state is cleared.
	[[nodiscard]] InstanceSignature NormalizeStableIdentity(const InstanceSignature& a_src);

	// Inputs are expected to be normalized with NormalizeStableIdentity.
	[[nodiscard]] bool StableIdentityEquals(const InstanceSignature& a_lhs, const InstanceSignature& a_rhs) noexcept;
}
