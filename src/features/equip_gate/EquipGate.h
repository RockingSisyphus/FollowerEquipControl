// Deny-by-default equip/unequip gate with scoped bypass and one-shot permits.

#pragma once

#include "PCH.h"

namespace FEC::EquipGate
{
	enum class Operation : std::uint8_t
	{
		kEquip,
		kUnequip
	};

	// Hooks ActorEquipManager::EquipObject and UnequipObject.
	void Install();
	void Uninstall();

	// Independent master toggles; enabled means deny-by-default for known followers.
	void SetEquipBlockingEnabled(bool a_enabled);
	void SetUnequipBlockingEnabled(bool a_enabled);
	[[nodiscard]] bool IsEquipBlockingEnabled();
	[[nodiscard]] bool IsUnequipBlockingEnabled();

	// Safety exception: never block equipping playable Light items such as torches.
	void SetNeverBlockTorchEquipEnabled(bool a_enabled);
	[[nodiscard]] bool IsNeverBlockTorchEquipEnabled();

	// Plugin-initiated equip/unequip bypass; all operations are allowed while in scope.
	class ScopedBypass
	{
	public:
		ScopedBypass();
		~ScopedBypass();

		ScopedBypass(const ScopedBypass&) = delete;
		ScopedBypass& operator=(const ScopedBypass&) = delete;

	private:
		bool _incremented{ false };
	};

	// Allows exactly one actor/item/op combination while in scope. No wildcards.
	class ScopedPermit
	{
	public:
		ScopedPermit(RE::FormID a_actorID, RE::FormID a_objectID, Operation a_op);
		~ScopedPermit();

		ScopedPermit(const ScopedPermit&) = delete;
		ScopedPermit& operator=(const ScopedPermit&) = delete;

	private:
		RE::FormID _actorID{ 0 };
		RE::FormID _objectID{ 0 };
		Operation _op{ Operation::kEquip };
		bool _active{ false };
	};

	// Current-thread one-shot allow for a specific vanilla equip/unequip attempt.
	void RequestOneShotPermit(RE::FormID a_actorID, RE::FormID a_objectID, Operation a_op);
}
