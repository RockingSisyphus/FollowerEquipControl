#include "EquipGate.h"

#include "PluginSettings.h"

#include "EquipGateHooks.h"

namespace FEC::EquipGate
{
	void Install()
	{
		Hooks::Install();

		SetEquipBlockingEnabled(PluginSettings::Get().equipGate.enableEquipBlocking);
		SetUnequipBlockingEnabled(PluginSettings::Get().equipGate.enableUnequipBlocking);
		SetNeverBlockTorchEquipEnabled(PluginSettings::Get().equipGate.enableNeverBlockTorchEquip);
		logger::info(
			"EquipGate: config toggles equipBlocking={} unequipBlocking={} neverBlockTorchEquip={} ",
			IsEquipBlockingEnabled(),
			IsUnequipBlockingEnabled(),
			IsNeverBlockTorchEquipEnabled());
	}

	void Uninstall()
	{
		Hooks::Uninstall();
	}
}
