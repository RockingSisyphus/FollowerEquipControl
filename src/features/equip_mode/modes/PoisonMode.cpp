#include "PoisonMode.h"

#include "ContainerMenuUtil.h"
#include "EquipGate.h"
#include "InventoryUtil.h"
#include "PluginSettings.h"
#include "Notifications.h"
#include "UISounds.h"

#include <RE/B/BGSEntryPointFunctionData.h>
#include <RE/B/BGSEntryPointFunctionDataOneValue.h>
#include <RE/B/BGSEntryPointPerkEntry.h>
#include <RE/B/BGSEntryPoint.h>
#include <RE/B/BSContainer.h>
#include <RE/E/ExtraPoison.h>
#include <RE/P/PerkEntryVisitor.h>

namespace FEC::EquipMode::Modes
{
	namespace
	{
		bool g_installed{ false };
		PoisonMode::Mode g_mode{ PoisonMode::Mode::kApplyToWeapon };

		struct WeaponPoisonState
		{
			RE::AlchemyItem* poison{ nullptr };
			std::uint32_t count{ 0 };
		};

		[[nodiscard]] WeaponPoisonState GetWeaponPoisonState(RE::InventoryEntryData* a_weapon)
		{
			WeaponPoisonState state{};
			if (!a_weapon || !a_weapon->extraLists) {
				return state;
			}
			for (auto* xList : *a_weapon->extraLists) {
				if (!xList) {
					continue;
				}
				auto* xPoison = xList->GetByType<RE::ExtraPoison>();
				if (xPoison && xPoison->poison) {
					state.poison = xPoison->poison;
					state.count = xPoison->count;
					return state;
				}
			}
			return state;
		}

		[[nodiscard]] bool IsTwoHandedWeapon(const RE::TESObjectWEAP& a_weap)
		{
			return a_weap.IsTwoHandedSword() || a_weap.IsTwoHandedAxe() || a_weap.IsBow() || a_weap.IsCrossbow();
		}

		// Iterate perk entries directly; HandleEntryPoint conflicts with PEPE hooks.
		[[nodiscard]] std::uint32_t GetPoisonDoseCount(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return 1u;
			}

			class PoisonDoseVisitor final : public RE::PerkEntryVisitor
			{
			public:
				float result{ 1.0f };

				RE::BSContainer::ForEachResult Visit(RE::BGSPerkEntry* a_perkEntry) override
				{
					if (!a_perkEntry || a_perkEntry->GetType() != RE::PERK_ENTRY_TYPE::kEntryPoint) {
						return RE::BSContainer::ForEachResult::kContinue;
					}

					auto* ep = static_cast<RE::BGSEntryPointPerkEntry*>(a_perkEntry);
					auto* funcData = ep->functionData;
					if (!funcData || funcData->GetType() != RE::BGSEntryPointFunctionData::FunctionType::kOneValue) {
						return RE::BSContainer::ForEachResult::kContinue;
					}

					float value = static_cast<RE::BGSEntryPointFunctionDataOneValue*>(funcData)->data;

					using Fn = RE::BGSEntryPointPerkEntry::EntryData::Function;
					switch (ep->entryData.function.get()) {
					case Fn::kSetValue:
						result = value;
						break;
					case Fn::kAddValue:
						result += value;
						break;
					case Fn::kMultiplyValue:
						result *= value;
						break;
					default:
						break;
					}
					return RE::BSContainer::ForEachResult::kContinue;
				}
			};

			PoisonDoseVisitor visitor;
			a_actor->ForEachPerkEntry(
				RE::BGSEntryPoint::ENTRY_POINT::kModPoisonDoseCount, visitor);

			return (visitor.result >= 1.0f)
				? static_cast<std::uint32_t>(visitor.result)
				: 1u;
		}

		[[nodiscard]] RE::ExtraPoison* GetExtraPoison(RE::InventoryEntryData* a_weapon)
		{
			if (!a_weapon || !a_weapon->extraLists) {
				return nullptr;
			}
			for (auto* xList : *a_weapon->extraLists) {
				if (!xList) {
					continue;
				}
				if (auto* xPoison = xList->GetByType<RE::ExtraPoison>()) {
					return xPoison;
				}
			}
			return nullptr;
		}

		void ApplyPoisonNow(RE::FormID a_targetActorID, RE::FormID a_sourceActorID, RE::FormID a_poisonID, FEC::EquipMode::Core::Hand a_hand)
		{
			auto* target = RE::TESForm::LookupByID<RE::Actor>(a_targetActorID);
			auto* source = RE::TESForm::LookupByID<RE::Actor>(a_sourceActorID);
			auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(a_poisonID);
			if (!target || !source || !obj) {
				return;
			}
			if (target->IsPlayerRef() || target->IsDead()) {
				return;
			}

			auto menu = ContainerMenuUtil::GetOpenContainerMenu();
			if (!menu) {
				return;
			}
			auto currentTarget = ContainerMenuUtil::GetAffectedTarget(menu.get());
			if (!currentTarget || currentTarget.get() != target) {
				return;
			}

			auto* poison = obj->As<RE::AlchemyItem>();
			if (!poison || !poison->IsPoison()) {
				return;
			}
			const auto countBefore = InventoryUtil::GetTotalCount(source, poison);
			if (countBefore <= 0) {
				return;
			}

			// If left is requested but right holds a two-handed weapon, poison right.
			bool left = (a_hand == FEC::EquipMode::Core::Hand::kLeft);
			if (left) {
				if (auto* rightEntry = target->GetEquippedEntryData(false)) {
					if (auto* rightWeap = rightEntry->GetObject() ? rightEntry->GetObject()->As<RE::TESObjectWEAP>() : nullptr) {
						if (IsTwoHandedWeapon(*rightWeap)) {
							left = false;
						}
					}
				}
			}

			auto* equipped = target->GetEquippedEntryData(left);
			auto* equippedWeap = equipped && equipped->GetObject() ? equipped->GetObject()->As<RE::TESObjectWEAP>() : nullptr;
			// Staves cannot be poisoned. Bound weapons are allowed because vanilla permits them.
			if (!equippedWeap || equippedWeap->IsStaff()) {
				Notifications::ToastOptions opt;
				opt.severity = Notifications::Severity::kInfo;
				opt.runOnMainThread = false;
				Notifications::ToastKeyFmt("feedback.poison.no_valid_weapon", opt, target->GetName(), poison->GetName(), "Weapon", left ? "Left" : "Right");
				FEC::UISounds::PlaySoundByFormID(FEC::UISounds::SoundFormID::kActivateFail);
				return;
			}

			auto* equippedObject = equipped->GetObject();
			const char* weaponName = equippedObject ? equippedObject->GetName() : nullptr;
			if (!weaponName || weaponName[0] == '\0') {
				weaponName = "Weapon";
			}

			const auto beforeState = GetWeaponPoisonState(equipped);
			const auto& cfg = PluginSettings::Get().equipModePoisonMode;
			// maxCharges == 100 means unlimited.
			const bool stacking = cfg.enableStacking;
			const std::uint32_t effectiveCap = (cfg.maxCharges == 100u) ? 0u : cfg.maxCharges;

			if (beforeState.poison && beforeState.count > 0) {
				if (!stacking) {
					Notifications::ToastOptions opt;
					opt.severity = Notifications::Severity::kInfo;
					opt.runOnMainThread = false;
					Notifications::ToastKeyFmt("feedback.poison.already_poisoned", opt, target->GetName(), poison->GetName(), weaponName);
					FEC::UISounds::PlaySoundByFormID(FEC::UISounds::SoundFormID::kActivateFail);
					return;
				}
				if (beforeState.poison != poison) {
					Notifications::ToastOptions opt;
					opt.severity = Notifications::Severity::kInfo;
					opt.runOnMainThread = false;
					Notifications::ToastKeyFmt("feedback.poison.different_poison", opt, target->GetName(), poison->GetName(), weaponName);
					FEC::UISounds::PlaySoundByFormID(FEC::UISounds::SoundFormID::kActivateFail);
					return;
				}
				if (effectiveCap > 0 && beforeState.count >= effectiveCap) {
					Notifications::ToastOptions opt;
					opt.severity = Notifications::Severity::kInfo;
					opt.runOnMainThread = false;
					Notifications::ToastKeyFmt("feedback.poison.max_charges", opt, target->GetName(), poison->GetName(), weaponName, std::to_string(effectiveCap).c_str());
					FEC::UISounds::PlaySoundByFormID(FEC::UISounds::SoundFormID::kActivateFail);
					return;
				}
			}

			const std::uint32_t dosesPerClick = GetPoisonDoseCount(target);
			const std::uint32_t desiredTotal = beforeState.count + dosesPerClick;
			const std::uint32_t newTotal = (stacking && effectiveCap > 0) ? std::min(desiredTotal, effectiveCap) : desiredTotal;

			EquipGate::ScopedBypass bypass;
			bool applied = false;

			if (beforeState.poison == poison && beforeState.count > 0) {
				// For stacking, update ExtraPoison directly; PoisonObject may replace the entry.
				if (auto* xPoison = GetExtraPoison(equipped)) {
					xPoison->count = newTotal;
					applied = true;
				}
			} else {
				// For fresh applications, use the engine API so PoisonedWeapon::Event fires.
				equipped->PoisonObject(poison, newTotal);
				const auto afterState = GetWeaponPoisonState(equipped);
				applied = (afterState.poison == poison) && (afterState.count > 0);
			}

			if (!applied) {
				return;
			}

			{
				Notifications::ToastOptions opt;
				opt.severity = Notifications::Severity::kInfo;
				opt.runOnMainThread = false;
				Notifications::ToastKeyFmt("feedback.poison.applied", opt, target->GetName(), poison->GetName(), weaponName);
			}
			FEC::UISounds::PlayForObject(poison, FEC::UISounds::Action::kUse);

			// Best effort: consume one if the engine did not consume from the source side.
			const auto countAfter = InventoryUtil::GetTotalCount(source, poison);
			if (countAfter >= countBefore && countAfter > 0) {
				source->RemoveItem(poison, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}

			ContainerMenuUtil::Refresh3DAndMenu(target);
		}

		void ConsumePoisonNow(RE::FormID a_targetActorID, RE::FormID a_sourceActorID, RE::FormID a_poisonID)
		{
			auto* target = RE::TESForm::LookupByID<RE::Actor>(a_targetActorID);
			auto* source = RE::TESForm::LookupByID<RE::Actor>(a_sourceActorID);
			auto* obj = RE::TESForm::LookupByID<RE::TESBoundObject>(a_poisonID);
			if (!target || !source || !obj) {
				return;
			}
			if (target->IsPlayerRef() || target->IsDead()) {
				return;
			}

			auto menu = ContainerMenuUtil::GetOpenContainerMenu();
			if (!menu) {
				return;
			}
			auto currentTarget = ContainerMenuUtil::GetAffectedTarget(menu.get());
			if (!currentTarget || currentTarget.get() != target) {
				return;
			}

			auto* poison = obj->As<RE::AlchemyItem>();
			if (!poison || !poison->IsPoison()) {
				return;
			}
			if (InventoryUtil::GetTotalCount(source, poison) <= 0) {
				return;
			}

			// Player->NPC consume stages one poison to the target first.
			const bool stagedFromSource = (source != target);
			if (stagedFromSource) {
				source->RemoveItem(poison, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, target);
			}
			const auto targetCountBefore = InventoryUtil::GetTotalCount(target, poison);
			if (targetCountBefore <= 0) {
				return;
			}

			EquipGate::ScopedBypass bypass;
			// Let the engine apply effects and consume the poison.
			target->DrinkPotion(poison, nullptr);

			const auto targetCountAfter = InventoryUtil::GetTotalCount(target, poison);
			const bool consumed = (targetCountAfter < targetCountBefore);
			if (!consumed) {
				// Revert the staged move if DrinkPotion did not consume it.
				if (stagedFromSource) {
					target->RemoveItem(poison, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, source);
				}
				return;
			}

			{
				Notifications::ToastOptions opt;
				opt.severity = Notifications::Severity::kInfo;
				opt.runOnMainThread = false;
				Notifications::ToastKeyFmt("feedback.poison.consumed", opt, target->GetName(), poison->GetName());
			}
			FEC::UISounds::PlayForObject(poison, FEC::UISounds::Action::kUse);
			ContainerMenuUtil::Refresh3DAndMenu(target);
		}
	}

	void PoisonMode::Install()
	{
		if (g_installed) {
			return;
		}
		const auto& cfg = PluginSettings::Get().equipModePoisonMode;
		if (cfg.mode == PluginSettings::PoisonMode::kDisable) {
			return;
		}
		g_mode = (cfg.mode == PluginSettings::PoisonMode::kConsume) ? Mode::kConsume : Mode::kApplyToWeapon;
		g_installed = true;
	}

	void PoisonMode::Uninstall()
	{
		g_installed = false;
	}

	bool PoisonMode::HandleTransfer(const ContainerMenuTransferHook::Context& ctx, RE::Actor* a_target, Core::Hand a_hand)
	{
		if (!g_installed) {
			return false;
		}
		if (!ctx.menu || !ctx.object || ctx.count == 0 || !a_target) {
			return false;
		}
		if (a_target->IsPlayerRef() || a_target->IsDead()) {
			return false;
		}

		auto* poison = ctx.object->As<RE::AlchemyItem>();
		if (!poison || !poison->IsPoison()) {
			return false;
		}

		const auto targetID = a_target->GetFormID();
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto sourceID = (ctx.mode == 0x00U && player) ? player->GetFormID() : targetID;
		const auto poisonID = ctx.object->GetFormID();
		if (targetID == 0 || sourceID == 0 || poisonID == 0) {
			return false;
		}

		const auto mode = g_mode;
		if (auto* taskInterface = SKSE::GetTaskInterface()) {
			taskInterface->AddTask([targetID, sourceID, poisonID, a_hand, mode]() {
				if (mode == PoisonMode::Mode::kConsume) {
					ConsumePoisonNow(targetID, sourceID, poisonID);
				} else {
					ApplyPoisonNow(targetID, sourceID, poisonID, a_hand);
				}
			});
		}

		// Own the click; do not transfer.
		return true;
	}

	void PoisonMode::SetMode(Mode a_mode)
	{
		g_mode = a_mode;
	}

	PoisonMode::Mode PoisonMode::GetMode()
	{
		return g_mode;
	}
}
