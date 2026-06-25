#include "EquipGateCore.h"

#include "ActorScope.h"
#include "ContainerMenuUtil.h"
#include "KnownFollowerState.h"
#include "WeaponBound.h"

#include <atomic>
#include <vector>

namespace FEC::EquipGate
{
	namespace
	{
		// Master toggles for deny-by-default equip and unequip gates.
		std::atomic_bool g_blockEquip{ true };
		std::atomic_bool g_blockUnequip{ true };
		std::atomic_bool g_neverBlockTorchEquip{ true };

		thread_local std::uint32_t g_bypassDepth = 0;

		struct Permit
		{
			RE::FormID actor{ 0 };
			RE::FormID object{ 0 };
			Operation op{ Operation::kEquip };
		};

		thread_local std::vector<Permit> g_permits;
	}

	namespace Core
	{
		std::uint32_t GetBypassDepth() noexcept
		{
			return g_bypassDepth;
		}

		// Safety cap for unexpected recursive bypass nesting.
		static constexpr std::uint32_t kMaxBypassDepth = 8;

		bool IncrementBypassDepth() noexcept
		{
			if (g_bypassDepth >= kMaxBypassDepth) {
				logger::error("EquipGate: bypass depth limit reached ({}), refusing increment", g_bypassDepth);
				return false;
			}
			++g_bypassDepth;
			return true;
		}

		void DecrementBypassDepth() noexcept
		{
			if (g_bypassDepth > 0) {
				--g_bypassDepth;
			}
		}

		bool ConsumePermit(const RE::Actor* a_actor, const RE::TESBoundObject* a_object, Operation a_op)
		{
			if (!a_actor || !a_object) {
				return false;
			}

			const auto actorID = a_actor->GetFormID();
			const auto objectID = a_object->GetFormID();
			if (actorID == 0 || objectID == 0) {
				return false;
			}

			for (auto it = g_permits.begin(); it != g_permits.end(); ++it) {
				if (it->op == a_op && it->actor == actorID && it->object == objectID) {
					g_permits.erase(it);
					return true;
				}
			}
			return false;
		}

		bool ShouldBlock(Operation a_op, RE::Actor* a_actor, RE::TESBoundObject* a_object, bool a_ignoreCombatSafety)
		{
			// Plugin bypass: never block our own operations.
			if (g_bypassDepth > 0) {
				return false;
			}

			// Never block while ContainerMenu is open.
			if (ContainerMenuUtil::IsContainerMenuOpen()) {
				return false;
			}

			if (!a_actor || a_actor->IsPlayerRef()) {
				return false;
			}
			if (!a_object) {
				return false;
			}

			// Only known in-scope followers use the deny-by-default gate.
			if (!ActorScope::IsAffectedFollower(a_actor)) {
				return false;
			}

			if (a_op == Operation::kEquip && !g_blockEquip.load(std::memory_order_relaxed)) {
				return false;
			}
			if (a_op == Operation::kUnequip && !g_blockUnequip.load(std::memory_order_relaxed)) {
				return false;
			}

			// Combat or drawn-state equip blocks can leave followers stuck unless a higher-level policy overrides the guard.
			auto* st = a_actor->AsActorState();
			const bool drawnReady = (st && st->IsWeaponDrawn());
			const bool inCombat = a_actor->IsInCombat();
			if (!a_ignoreCombatSafety && (drawnReady || inCombat)) {
				return false;
			}

			// Ammo equip is handled separately and should never be blocked here.
			if (a_object->GetFormType() == RE::FormType::Ammo) {
				return false;
			}

			// Bound weapons are transient; blocking them can interfere with conjuration cleanup.
			if (WeaponBound::IsWeaponAndBound(a_object)) {
				return false;
			}

			// Torch equip remains optionally exempt for playable Light items.
			if (g_neverBlockTorchEquip.load(std::memory_order_relaxed) && a_object->GetFormType() == RE::FormType::Light) {
				auto* light = a_object->As<RE::TESObjectLIGH>();
				if (light && light->GetPlayable()) {
					return false;
				}
			}

			return true;
		}
	}

	void SetEquipBlockingEnabled(bool a_enabled)
	{
		g_blockEquip.store(a_enabled, std::memory_order_relaxed);
	}

	void SetUnequipBlockingEnabled(bool a_enabled)
	{
		g_blockUnequip.store(a_enabled, std::memory_order_relaxed);
	}

	void SetNeverBlockTorchEquipEnabled(bool a_enabled)
	{
		g_neverBlockTorchEquip.store(a_enabled, std::memory_order_relaxed);
	}

	bool IsEquipBlockingEnabled()
	{
		return g_blockEquip.load(std::memory_order_relaxed);
	}

	bool IsUnequipBlockingEnabled()
	{
		return g_blockUnequip.load(std::memory_order_relaxed);
	}

	bool IsNeverBlockTorchEquipEnabled()
	{
		return g_neverBlockTorchEquip.load(std::memory_order_relaxed);
	}

	ScopedBypass::ScopedBypass()
		: _incremented(Core::IncrementBypassDepth())
	{
	}

	ScopedBypass::~ScopedBypass()
	{
		if (_incremented) {
			Core::DecrementBypassDepth();
		}
	}

	ScopedPermit::ScopedPermit(RE::FormID a_actorID, RE::FormID a_objectID, Operation a_op)
	{
		if (a_actorID == 0 || a_objectID == 0) {
			return;
		}

		_actorID = a_actorID;
		_objectID = a_objectID;
		_op = a_op;
		_active = true;

		g_permits.push_back(Permit{ a_actorID, a_objectID, a_op });
	}

	ScopedPermit::~ScopedPermit()
	{
		if (!_active) {
			return;
		}

		if (!g_permits.empty()) {
			const auto& back = g_permits.back();
			if (back.actor == _actorID && back.object == _objectID && back.op == _op) {
				g_permits.pop_back();
				return;
			}
		}

		for (auto it = g_permits.begin(); it != g_permits.end(); ++it) {
			if (it->actor == _actorID && it->object == _objectID && it->op == _op) {
				g_permits.erase(it);
				return;
			}
		}
	}

	void RequestOneShotPermit(RE::FormID a_actorID, RE::FormID a_objectID, Operation a_op)
	{
		if (a_actorID == 0 || a_objectID == 0) {
			return;
		}
		g_permits.push_back(Permit{ a_actorID, a_objectID, a_op });
	}
}
