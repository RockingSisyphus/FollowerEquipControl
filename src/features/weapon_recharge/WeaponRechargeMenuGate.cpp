#include "WeaponRechargeMenuGate.h"

#include "ContainerMenuUtil.h"
#include "KnownFollowerState.h"
#include "PluginSettings.h"
#include "WeaponRechargeGfxUtil.h"

namespace FEC::WeaponRecharge
{
	bool ShouldAffectMenu(RE::ContainerMenu* a_menu)
	{
		if (!PluginSettings::Get().weaponEnchantmentRecharge.enableRecharge) {
			return false;
		}
		if (!a_menu) {
			return false;
		}
		auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
		if (!target) {
			return false;
		}
		auto& root = a_menu->GetRuntimeData().root;
		if (!root.IsObject()) {
			return false;
		}
		if (!WeaponRecharge::GfxUtil::IsSkyUiPresent(root)) {
			return false;
		}
		return true;
	}
}
