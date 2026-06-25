// Hooks ActivateHandler::ProcessButton to open ContainerMenu in NPC mode when quick-trade input is held.
// Actor eligibility is owned by ActorScope::ShouldAdmitForQuickTrade.
// Key value 0 falls back to the engine Sprint binding; cache it on the game thread because ControlMap::GetMappedKey is not input-thread safe.
// Non-player mounts can only qualify while actively ridden; the engine has no last-ridden tracking for them.

#pragma once

namespace FEC::QuickTrade
{
	void Install();
	void Uninstall();
}
