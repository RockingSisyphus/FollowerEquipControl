#include "SpellTomeMode.h"

#include "ContainerMenuUtil.h"
#include "InventoryUtil.h"
#include "Notifications.h"
#include "PluginSettings.h"
#include "UISounds.h"

#include <chrono>
#include <format>

namespace FEC::EquipMode::Modes
{
	namespace
	{
		bool g_installed{ false };

		void LearnNow(RE::FormID a_targetActorID, RE::FormID a_sourceActorID, RE::FormID a_bookID, bool a_doNotConsumeSpellTome)
		{
			auto* target = RE::TESForm::LookupByID<RE::Actor>(a_targetActorID);
			auto* source = RE::TESForm::LookupByID<RE::Actor>(a_sourceActorID);
			auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(a_bookID);
			if (!target || !source || !object) {
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

			auto* book = object->As<RE::TESObjectBOOK>();
			if (!book || !book->TeachesSpell()) {
				return;
			}
			auto* spell = book->GetSpell();
			if (!spell) {
				return;
			}
			if (InventoryUtil::GetTotalCount(source, book) <= 0) {
				return;
			}

			if (target->HasSpell(spell)) {
				return;
			}

			if (!target->AddSpell(spell)) {
				return;
			}
			FEC::UISounds::PlaySoundByFormID(FEC::UISounds::SoundFormID::kSpellLearned);

			if (!a_doNotConsumeSpellTome) {
				source->RemoveItem(book, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, nullptr);
			}
			{
				Notifications::ToastOptions opt;
				opt.severity = Notifications::Severity::kInfo;
				opt.runOnMainThread = false;
				Notifications::ToastKeyFmt("feedback.spell_tome.learned", opt, target->GetName(), spell->GetName());
			}
			ContainerMenuUtil::Refresh3DAndMenu(target);
		}
	}

	void SpellTomeMode::Install()
	{
		if (g_installed) {
			return;
		}
		if (!PluginSettings::Get().equipModeSpellTomeMode.enableSpellTomeMode) {
			return;
		}
		g_installed = true;
	}

	void SpellTomeMode::Uninstall()
	{
		g_installed = false;
	}

	bool SpellTomeMode::HandleTransfer(const ContainerMenuTransferHook::Context& ctx, RE::Actor* a_target)
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

		auto* book = ctx.object->As<RE::TESObjectBOOK>();
		if (!book || !book->TeachesSpell() || !book->GetSpell()) {
			return false;
		}
		auto* spell = book->GetSpell();

		// Block transfer when the follower already knows the spell.
		if (a_target->HasSpell(spell)) {
			Notifications::ToastOptions opt;
			opt.severity = Notifications::Severity::kInfo;
			opt.throttle = std::chrono::milliseconds(1500);
			opt.runOnMainThread = false;
			const auto throttleKey = std::format(
				"feedback.spell_tome.already_known.{:08X}.{:08X}",
				a_target->GetFormID(),
				spell->GetFormID());
			Notifications::ToastKeyFmtThrottled(
				"feedback.spell_tome.already_known",
				opt,
				throttleKey,
				a_target->GetName(),
				spell->GetName());
			FEC::UISounds::PlaySoundByFormID(FEC::UISounds::SoundFormID::kActivateFail);
			return true;
		}

		const auto targetID = a_target->GetFormID();
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto sourceID = (ctx.mode == 0x00U && player) ? player->GetFormID() : targetID;
		const auto bookID = ctx.object->GetFormID();
		if (targetID == 0 || sourceID == 0 || bookID == 0) {
			return false;
		}

		const bool doNotConsumeSpellTome = PluginSettings::Get().equipModeSpellTomeMode.doNotConsumeSpellTomes;
		if (auto* taskInterface = SKSE::GetTaskInterface()) {
			taskInterface->AddTask([targetID, sourceID, bookID, doNotConsumeSpellTome]() {
				LearnNow(targetID, sourceID, bookID, doNotConsumeSpellTome);
			});
		}

		// Own the click; learn instead of transferring. Consumption is controlled by settings.
		return true;
	}
}
