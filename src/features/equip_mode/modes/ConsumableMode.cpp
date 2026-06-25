#include "ConsumableMode.h"

#include "ContainerMenuUtil.h"
#include "EquipGate.h"
#include "InventoryUtil.h"
#include "Notifications.h"
#include "PluginSettings.h"
#include "UISounds.h"

namespace FEC::EquipMode::Modes
{
	namespace
	{
		bool g_installed{ false };
		bool g_enablePotions{ true };
		bool g_enableFoods{ true };
		bool g_enableDrinks{ true };
		bool g_enableIngredients{ true };
		bool g_enableIngredientDiscoveryForPlayer{ false };

		enum class Kind : std::uint8_t
		{
			kPotion,
			kFood,
			kDrink,
			kIngredient,
			kUnknown
		};

		[[nodiscard]] bool LooksLikeDrinkByPickupPutdownSounds(const RE::AlchemyItem* a_item)
		{
			if (!a_item) {
				return false;
			}

			// Drinks commonly use the potion pickup/putdown sounds.
			constexpr RE::FormID kPotionPickupSoundID = 0x0003EDBD;   // ITMPotionUpSD
			constexpr RE::FormID kPotionPutdownSoundID = 0x0003EDC0;  // ITMPotionDownSD

			const auto* pickup = a_item->pickupSound;
			const auto* putdown = a_item->putdownSound;
			const bool pickupMatches = pickup && pickup->GetFormID() == kPotionPickupSoundID;
			const bool putdownMatches = putdown && putdown->GetFormID() == kPotionPutdownSoundID;
			return pickupMatches || putdownMatches;
		}

		[[nodiscard]] Kind Classify(RE::TESBoundObject* a_object)
		{
			if (!a_object) {
				return Kind::kUnknown;
			}
			if (auto* alch = a_object->As<RE::AlchemyItem>()) {
				if (alch->IsPoison()) {
					return Kind::kUnknown;
				}
				if (alch->IsFood()) {
					return LooksLikeDrinkByPickupPutdownSounds(alch) ? Kind::kDrink : Kind::kFood;
				}
				return Kind::kPotion;
			}
			if (a_object->As<RE::IngredientItem>()) {
				return Kind::kIngredient;
			}
			return Kind::kUnknown;
		}

		[[nodiscard]] bool IsKindEnabled(Kind a_kind)
		{
			switch (a_kind) {
			case Kind::kPotion:
				return g_enablePotions;
			case Kind::kFood:
				return g_enableFoods;
			case Kind::kDrink:
				return g_enableDrinks;
			case Kind::kIngredient:
				return g_enableIngredients;
			default:
				return false;
			}
		}

		void MaybeLearnIngredientEffectForPlayer(RE::Actor* a_follower, RE::IngredientItem* a_ingr)
		{
			if (!g_enableIngredientDiscoveryForPlayer || !a_ingr || !a_follower) {
				return;
			}

			// Vanilla-aligned: consuming an ingredient reveals only the first effect.
			const bool alreadyKnown = (a_ingr->gamedata.knownEffectFlags & (std::uint16_t{ 1 } << 0)) != 0;
			if (alreadyKnown) {
				return;
			}

			const bool learned = a_ingr->LearnEffect(std::uint32_t{ 0 });
			if (learned) {
				const char* effectName = nullptr;
				if (!a_ingr->effects.empty()) {
					if (const auto* effect = a_ingr->effects[0]; effect && effect->baseEffect) {
						effectName = effect->baseEffect->GetName();
					}
				}

				{
					Notifications::ToastOptions opt;
					opt.severity = Notifications::Severity::kInfo;
					opt.runOnMainThread = false;
					Notifications::ToastKeyFmt(
						"feedback.consumable.discovery_first_effect",
						opt,
						effectName ? effectName : "First effect",
						a_follower->GetName());
				}

				FEC::UISounds::PlaySoundByFormID(FEC::UISounds::SoundFormID::kAlchemyLearnEffect);
			}
		}

		void ConsumeNow(RE::FormID a_targetActorID, RE::FormID a_sourceActorID, RE::FormID a_objectID)
		{
			auto* target = RE::TESForm::LookupByID<RE::Actor>(a_targetActorID);
			auto* source = RE::TESForm::LookupByID<RE::Actor>(a_sourceActorID);
			auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(a_objectID);
			if (!target || !source || !object) {
				return;
			}
			if (target->IsPlayerRef() || target->IsDead()) {
				return;
			}

			// Only act while ContainerMenu is open and still targeting this actor.
			auto menu = ContainerMenuUtil::GetOpenContainerMenu();
			if (!menu) {
				return;
			}
			auto menuTarget = ContainerMenuUtil::GetAffectedTarget(menu.get());
			if (!menuTarget || menuTarget.get() != target) {
				return;
			}

			auto* equipMan = RE::ActorEquipManager::GetSingleton();
			if (!equipMan) {
				return;
			}

			EquipGate::ScopedBypass gateBypass;

			const auto kind = Classify(object);
			if (kind == Kind::kUnknown || !IsKindEnabled(kind)) {
				return;
			}

			// Player->NPC consume attempts stage one item to the follower first.
			const bool stagedFromSource = (source != target);
			if (stagedFromSource) {
				const auto have = InventoryUtil::GetTotalCount(source, object);
				if (have <= 0) {
					return;
				}
				// Move from the source side so the source pays the cost.
				source->RemoveItem(object, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, target);
			}

			const auto targetCountBefore = InventoryUtil::GetTotalCount(target, object);
			if (targetCountBefore <= 0) {
				return;
			}

			equipMan->EquipObject(target, object, nullptr, 1, nullptr, false, true, true, true);
			const auto targetCountAfter = InventoryUtil::GetTotalCount(target, object);
			const bool consumed = (targetCountAfter < targetCountBefore);
			if (!consumed) {
				if (stagedFromSource) {
					target->RemoveItem(object, 1, RE::ITEM_REMOVE_REASON::kRemove, nullptr, source);
					ContainerMenuUtil::QueueRefreshForActor(target->GetFormID());
				}
				return;
			}

			FEC::UISounds::PlayForObject(object, FEC::UISounds::Action::kUse);
			{
				Notifications::ToastOptions opt;
				opt.severity = Notifications::Severity::kInfo;
				opt.runOnMainThread = false;
				std::string_view key = "feedback.consumable.potion";
				switch (kind) {
				case Kind::kPotion:
					key = "feedback.consumable.potion";
					break;
				case Kind::kFood:
					key = "feedback.consumable.food";
					break;
				case Kind::kDrink:
					key = "feedback.consumable.drink";
					break;
				case Kind::kIngredient:
					key = "feedback.consumable.ingredient";
					break;
				default:
					break;
				}
				Notifications::ToastKeyFmt(key, opt, target->GetName(), object->GetName());
			}

			if (auto* ingr = object->As<RE::IngredientItem>()) {
				MaybeLearnIngredientEffectForPlayer(target, ingr);
			}

			ContainerMenuUtil::Refresh3DAndMenu(target);
		}
	}

	void ConsumableMode::Install()
	{
		if (g_installed) {
			return;
		}

		const auto& cfg = PluginSettings::Get();
		if (!cfg.equipModeConsumableMode.enableConsumableMode) {
			logger::info("ConsumableMode: disabled by config");
			return;
		}

		g_enablePotions = cfg.equipModeConsumableMode.enablePotions;
		g_enableFoods = cfg.equipModeConsumableMode.enableFoods;
		g_enableDrinks = cfg.equipModeConsumableMode.enableDrinks;
		g_enableIngredients = cfg.equipModeConsumableMode.enableIngredients;
		g_enableIngredientDiscoveryForPlayer = cfg.equipModeConsumableMode.enableIngredientDiscoveryForPlayer;
		g_installed = true;
		logger::info("ConsumableMode: installed");
	}

	void ConsumableMode::Uninstall()
	{
		if (!g_installed) {
			return;
		}
		g_installed = false;
		logger::info("ConsumableMode: uninstalled");
	}

	bool ConsumableMode::HandleTransfer(const ContainerMenuTransferHook::Context& ctx, RE::Actor* a_target)
	{
		if (!g_installed) {
			return false;
		}
		if (!ctx.menu || !ctx.object || ctx.count == 0) {
			return false;
		}
		if (!a_target || a_target->IsPlayerRef() || a_target->IsDead()) {
			return false;
		}

		const auto kind = Classify(ctx.object);
		if (kind == Kind::kUnknown || !IsKindEnabled(kind)) {
			return false;
		}

		const auto actorID = a_target->GetFormID();
		auto* player = RE::PlayerCharacter::GetSingleton();
		const auto sourceID = (ctx.mode == 0x00U && player) ? player->GetFormID() : actorID;
		const auto objectID = ctx.object->GetFormID();
		if (actorID == 0 || sourceID == 0 || objectID == 0) {
			return false;
		}

		if (auto* taskInterface = SKSE::GetTaskInterface()) {
			taskInterface->AddTask([actorID, sourceID, objectID]() {
				ConsumeNow(actorID, sourceID, objectID);
			});
		}

		return true;
	}

	void ConsumableMode::SetPotionsEnabled(bool a_enabled) { g_enablePotions = a_enabled; }
	void ConsumableMode::SetFoodsEnabled(bool a_enabled) { g_enableFoods = a_enabled; }
	void ConsumableMode::SetDrinksEnabled(bool a_enabled) { g_enableDrinks = a_enabled; }
	void ConsumableMode::SetIngredientsEnabled(bool a_enabled) { g_enableIngredients = a_enabled; }
	void ConsumableMode::SetIngredientDiscoveryForPlayerEnabled(bool a_enabled) { g_enableIngredientDiscoveryForPlayer = a_enabled; }

	bool ConsumableMode::ArePotionsEnabled() { return g_enablePotions; }
	bool ConsumableMode::AreFoodsEnabled() { return g_enableFoods; }
	bool ConsumableMode::AreDrinksEnabled() { return g_enableDrinks; }
	bool ConsumableMode::AreIngredientsEnabled() { return g_enableIngredients; }
	bool ConsumableMode::IsIngredientDiscoveryForPlayerEnabled() { return g_enableIngredientDiscoveryForPlayer; }
}
