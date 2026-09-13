#include "Router.h"

#include "EquipModeAction.h"
#include "SyntheticXList.h"

#include "ConsumableMode.h"
#include "PoisonMode.h"
#include "SpellTomeMode.h"

#include "QuantityMenuBlocker.h"
#include "StaleWeightCacheFix.h"
#include "ZeroWeightTakeAllFix.h"

#include "ContainerMenuTransferHook.h"
#include "ContainerMenuUtil.h"
#include "Controls.h"
#include "KnownFollowerState.h"
#include "Notifications.h"
#include "PluginSettings.h"

#include "ActorScope.h"
#include "CombatEquipPreference.h"
#include "CombatEquipPreferencePolicy.h"
#include "InstanceSignature.h"
#include "UISounds.h"

#include <optional>

#include <RE/I/InventoryChanges.h>
#include <RE/I/InventoryEntryData.h>
#include <RE/I/ItemList.h>

namespace FEC::EquipMode::Core
{
	namespace
	{
		bool g_installed{ false };
		ContainerMenuTransferHook::ListenerHandle g_transferHandle{ 0 };
		ContainerMenuTransferHook::ListenerHandle g_preHandle{ 0 };
		ContainerMenuTransferHook::ListenerHandle g_postHandle{ 0 };

		struct PendingEquip
		{
			RE::FormID actorID{ 0 };
			RE::FormID objectID{ 0 };
			Hand hand{ Hand::kRight };
			std::uint8_t mode{ 0 };
			RE::ExtraDataList* xList{ nullptr };
			bool corpse{ false };
		};

		thread_local std::optional<PendingEquip> g_pending;

		[[nodiscard]] Hand GetHandIntent() noexcept
		{
			// Consume AttemptEquip/handleInput hand override before falling back to Skyrim's left-hand binding.
			auto override_ = Controls::ConsumeHandOverride();
			if (override_) {
				return *override_ ? Hand::kLeft : Hand::kRight;
			}
			return Controls::IsLeftHandKeyDown() ? Hand::kLeft : Hand::kRight;
		}

		[[nodiscard]] const char* HandToStr(Hand a_hand) noexcept
		{
			return a_hand == Hand::kLeft ? "left" : "right";
		}

		[[nodiscard]] bool ShouldLogEquipModeTrace() noexcept
		{
			return spdlog::should_log(spdlog::level::debug);
		}

		[[nodiscard]] RE::FormID SafeFormID(const RE::TESForm* a_form) noexcept
		{
			return a_form ? a_form->GetFormID() : 0;
		}

		[[nodiscard]] const char* SafeName(const RE::TESForm* a_form) noexcept
		{
			if (!a_form) {
				return "";
			}

			const auto* name = a_form->GetName();
			return name ? name : "";
		}

		void LogActorHands(RE::Actor* a_actor, std::string_view a_tag)
		{
			if (!ShouldLogEquipModeTrace() || !a_actor) {
				return;
			}

			auto* left = a_actor->GetEquippedObject(true);
			auto* right = a_actor->GetEquippedObject(false);
			logger::debug(
				"EquipModeTrace: {} actor={:08X} name='{}' equippedL={:08X} '{}' equippedR={:08X} '{}'",
				a_tag,
				SafeFormID(a_actor),
				SafeName(a_actor),
				SafeFormID(left),
				SafeName(left),
				SafeFormID(right),
				SafeName(right));
		}

		void LogSelectedRow(RE::ContainerMenu* a_menu, RE::TESBoundObject* a_ctxObject)
		{
			if (!ShouldLogEquipModeTrace()) {
				return;
			}

			if (!a_menu) {
				logger::debug("EquipModeTrace: selectedRow menu=null");
				return;
			}
			auto* itemList = a_menu->GetRuntimeData().itemList;
			if (!itemList) {
				logger::debug("EquipModeTrace: selectedRow itemList=null");
				return;
			}
			auto* selected = itemList->GetSelectedItem();
			if (!selected) {
				logger::debug("EquipModeTrace: selectedRow selected=null");
				return;
			}
			auto* entry = selected->data.objDesc;
			auto* obj = entry ? entry->GetObject() : nullptr;
			const bool ctxMatch = (obj && a_ctxObject) ? (obj == a_ctxObject) : false;
			// Do not call InventoryEntryData helpers on the UI clone; they walk extraLists and
			// can dereference stale pointers after strict/base-fallback equip cycles. Log only
			// object identity, countDelta, and pointer values.
			logger::debug(
				"EquipModeTrace: selectedRow ctxMatch={} obj={:08X} '{}' countDelta={} extraListsPtr={:p}",
				ctxMatch,
				SafeFormID(obj),
				SafeName(obj),
				entry ? entry->countDelta : 0,
				static_cast<void*>(entry ? entry->extraLists : nullptr));

			if (!entry || !entry->extraLists) {
				return;
			}

			std::uint32_t nonNull = 0;
			for (auto* x : *entry->extraLists) {
				if (x) {
					++nonNull;
				}
			}
			if (spdlog::should_log(spdlog::level::trace)) {
				logger::trace("EquipModeTrace: selectedRow extraLists nonNullCount={}", nonNull);
				std::uint32_t i = 0;
				for (auto* x : *entry->extraLists) {
					if (!x) {
						++i;
						continue;
					}
					logger::trace(
						"EquipModeTrace: selectedRow xList[{}]={:p}",
						i,
						static_cast<void*>(x));
					++i;
				}
			}
		}

		[[nodiscard]] bool IsSupportedEquipObject(RE::TESBoundObject* a_object)
		{
			if (!a_object) {
				return false;
			}
			const auto formType = a_object->GetFormType();
			const bool isWeapon = a_object->IsWeapon();
			const bool isArmor = a_object->IsArmor();
			const bool isAmmo = formType == RE::FormType::Ammo;
			const bool isTorch = formType == RE::FormType::Light;
			const bool isScroll = formType == RE::FormType::Scroll;
			if (!isWeapon && !isArmor && !isAmmo && !isTorch && !isScroll) {
				return false;
			}

			const auto& filter = PluginSettings::Get().equipModeItems;
			if (!filter.enable) {
				return false;
			}
			if (isWeapon && !filter.enableWeapons) {
				return false;
			}
			if (isArmor && !filter.enableArmor) {
				return false;
			}
			if (isAmmo && !filter.enableAmmo) {
				return false;
			}
			if (isTorch && !filter.enableTorches) {
				return false;
			}
			if (isScroll && !filter.enableScrolls) {
				return false;
			}

			return true;
		}

		[[nodiscard]] bool ShouldAffectTransfer(const ContainerMenuTransferHook::Context& ctx, RE::Actor*& a_outTarget)
		{
			a_outTarget = nullptr;
			if (!ctx.menu || !ctx.object || ctx.count == 0) {
				return false;
			}
			if (!Controls::IsModKeyDown()) {
				return false;
			}

			auto target = ContainerMenuUtil::GetAffectedTarget(ctx.menu);
			if (!target) {
				return false;
			}

			a_outTarget = target.get();
			return true;
		}

		// Corpse equip mode only handles dead actor targets and armor, weapon, or ammo.
		[[nodiscard]] RE::Actor* TryGetCorpseEquipTarget(const ContainerMenuTransferHook::Context& ctx)
		{
			if (!ctx.menu || !ctx.object || ctx.count == 0) {
				return nullptr;
			}
			if (!Controls::IsModKeyDown()) {
				return nullptr;
			}
			if (!ctx.object->IsArmor() && !ctx.object->IsWeapon() && ctx.object->GetFormType() != RE::FormType::Ammo) {
				return nullptr;
			}
			const auto& corpseEquip = PluginSettings::Get().corpseEquipMode;
			if (!corpseEquip.enable) {
				return nullptr;
			}

			auto target = ContainerMenuUtil::ResolveActorHandle(ctx.menu->GetTargetRefHandle());
			if (!target || target->IsPlayerRef() || !target->IsDead()) {
				return nullptr;
			}

			return target.get();
		}

		[[nodiscard]] RE::ExtraDataList* GetOrCreateActorEntryXList(RE::Actor* a_actor, RE::TESBoundObject* a_object)
		{
			if (!a_actor || !a_object) {
				return nullptr;
			}

			if (auto* changes = a_actor->GetInventoryChanges(); changes && changes->entryList) {
				for (auto* realEntry : *changes->entryList) {
					if (!realEntry || realEntry->GetObject() != a_object) {
						continue;
					}

					if ((!realEntry->extraLists || realEntry->extraLists->empty())) {
						if (auto* synth = SyntheticXList::EnsureXList(a_actor, realEntry)) {
							return synth;
						}
					}

					if (realEntry->extraLists) {
						for (auto* x : *realEntry->extraLists) {
							if (x) {
								return x;
							}
						}
					}

					return nullptr;
				}
			}

			return nullptr;
		}

		bool OnTransferAttempt(const ContainerMenuTransferHook::Context& ctx)
		{
			RE::Actor* target = nullptr;
			if (!ShouldAffectTransfer(ctx, target)) {
				auto* corpse = TryGetCorpseEquipTarget(ctx);
				if (corpse) {
					if (ctx.mode == 0x01U) {
						// NPC->Player: block the transfer and toggle equip immediately.
						if (ShouldLogEquipModeTrace()) {
							logger::debug(
								"EquipModeTrace: corpse equip toggle actor={:08X} '{}' object={:08X} '{}'",
								SafeFormID(corpse), SafeName(corpse),
								SafeFormID(ctx.object), SafeName(ctx.object));
						}
						const auto hand = GetHandIntent();
						const bool ok = PerformCorpseEquipToggleFromMenu(ctx.menu, corpse, hand);
						if (ShouldLogEquipModeTrace()) {
							logger::debug("EquipModeTrace: corpse equip toggle result={}", ok);
						}
						return ok;
					}
					if (ctx.mode == 0x00U) {
						// Player->NPC: allow transfer, then equip post-transfer.
						const auto hand = GetHandIntent();
						g_pending = PendingEquip{ corpse->GetFormID(), ctx.object->GetFormID(), hand, ctx.mode, nullptr, true };
						if (ShouldLogEquipModeTrace()) {
							logger::debug("EquipModeTrace: corpse mode=Player->NPC pending armed actor={:08X} '{}' object={:08X} '{}'",
								SafeFormID(corpse), SafeName(corpse),
								SafeFormID(ctx.object), SafeName(ctx.object));
						}
						return false;
					}
				}
				return false;
			}

			const auto hand = GetHandIntent();
			const char* handKey = (hand == Hand::kLeft) ? "LeftHand" : "RightHand";
			const char* dir = (ctx.mode == 0x00U) ? "Player->NPC" : (ctx.mode == 0x01U) ? "NPC->Player" : "Unknown";

			if (ShouldLogEquipModeTrace()) {
				logger::debug(
					"EquipModeTrace: click begin dir={} mode=0x{:02X} key={} hand={} count={} object={:08X} '{}'",
					dir,
					ctx.mode,
					handKey,
					HandToStr(hand),
					ctx.count,
					SafeFormID(ctx.object),
					SafeName(ctx.object));
			}
			LogSelectedRow(ctx.menu, ctx.object);
			LogActorHands(target, "pre");

			// Special modes own the click and block vanilla transfer.
			if (Modes::PoisonMode::HandleTransfer(ctx, target, hand)) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: click handled by PoisonMode (transfer blocked)");
				}
				return true;
			}
			if (Modes::ConsumableMode::HandleTransfer(ctx, target)) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: click handled by ConsumableMode (transfer blocked)");
				}
				return true;
			}
			if (Modes::SpellTomeMode::HandleTransfer(ctx, target)) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: click handled by SpellTomeMode (transfer blocked)");
				}
				return true;
			}

			// Equip-mode action: Player->NPC transfers first and equips post-transfer;
			// NPC->Player blocks transfer and toggles the selected row immediately.
			if (!IsSupportedEquipObject(ctx.object)) {
				if (ctx.mode == 0x01U) {
					if (ShouldLogEquipModeTrace()) {
						logger::debug("EquipModeTrace: NPC->Player blocked (unsupported object type)");
					}
					Notifications::ToastKeyFmt("feedback.equip_mode.unsupported_item", {}, target->GetName());
					UISounds::PlaySoundByFormID(UISounds::SoundFormID::kActivateFail);
					return true;
				}
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: click ignored (unsupported object type)");
				}
				return false;
			}

			const auto actorID = target->GetFormID();
			const auto objectID = ctx.object->GetFormID();
			if (actorID == 0 || objectID == 0) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: click ignored (actorID/objectID missing)");
				}
				return false;
			}

			if (ctx.mode == 0x00U) {
				// Arm equip-only and require the engine-selected transferred instance in post.
				g_pending = PendingEquip{ actorID, objectID, hand, ctx.mode, nullptr };
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: mode=Player->NPC pending armed (await transfer-captured xList)");
				}
				return false;
			}
			if (ctx.mode == 0x01U) {
				// Block transfer and toggle the selected row. Non-unique selections fail closed;
				// plain-stack synthetic xLists are handled inside GetStrictEntryForSelection.

				// When Headgear Auto-Equip is enabled, left-hand click on headgear saves or clears
				// the combat headgear preference without equipping.
				auto* headgearArmor = ctx.object ? ctx.object->As<RE::TESObjectARMO>() : nullptr;
				if (PluginSettings::Get().combatEquipRestore.enableHeadgearAutoEquip && hand == Hand::kLeft &&
					headgearArmor && ActorScope::ArmorEquipAllowed(target, headgearArmor)) {
					const auto cepCat = FEC::CombatEquip::Preference::Policy::TryClassifyCEPCategory(ctx.object, false);
					if (cepCat.has_value() && *cepCat == CombatEquipPreference::Category::kHeadgear) {
						const auto existing = CombatEquipPreference::GetEntry(actorID, CombatEquipPreference::Category::kHeadgear);
						if (existing.has_value() && existing->baseObjectID == objectID) {
							CombatEquipPreference::ClearCategoryEntry(actorID, CombatEquipPreference::Category::kHeadgear);
							UISounds::PlayForObject(ctx.object, UISounds::Action::kPutdown);
							if (ShouldLogEquipModeTrace()) {
								logger::debug("EquipModeTrace: headgear preference CLEARED actor={:08X} object={:08X} '{}'",
									actorID, objectID, SafeName(ctx.object));
							}
						} else {
							// Resolve from live inventory; selected-row xLists may be stale after base-fallback equip cycles.
							RE::ExtraDataList* xList = GetOrCreateActorEntryXList(target, ctx.object);
							CombatEquipPreference::CaptureUserEquip(target, ctx.object, false, xList, xList != nullptr);
							UISounds::PlayForObject(ctx.object, UISounds::Action::kPickup);
							if (ShouldLogEquipModeTrace()) {
								logger::debug("EquipModeTrace: headgear preference SAVED actor={:08X} object={:08X} '{}'",
									actorID, objectID, SafeName(ctx.object));
							}
						}
						return true;
					}
				}

				const bool strictOk = PerformEquipModeActionStrictFromMenu(ctx.menu, target, hand, false);
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: mode=NPC->Player strictToggleOk={}", strictOk);
				}
				LogActorHands(target, "post");
				return true;
			}

			return false;
		}

		void OnTransferPre(const ContainerMenuTransferHook::Context& ctx)
		{
			// If the mod key was released before transfer, discard the pending post-transfer equip.
			if (!Controls::IsModKeyDown()) {
				if (g_pending) {
					if (ShouldLogEquipModeTrace()) {
						logger::debug("EquipModeTrace: pre transfer (mod key released) pending cleared");
					}
				}
				g_pending.reset();
				return;
			}

			// Pending equip must still match the incoming transfer.
			if (g_pending && (!ctx.object || ctx.object->GetFormID() != g_pending->objectID || ctx.mode != g_pending->mode)) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: pre transfer (pending mismatch) pending cleared");
				}
				g_pending.reset();
			}
		}

		void OnTransferPost(const ContainerMenuTransferHook::Context& ctx)
		{
			if (!g_pending) {
				return;
			}

			auto pending = *g_pending;
			g_pending.reset();

			if (!ctx.object || ctx.mode != pending.mode || ctx.object->GetFormID() != pending.objectID) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: post transfer ignored (ctx mismatch)");
				}
				return;
			}

			auto* actor = RE::TESForm::LookupByID<RE::Actor>(pending.actorID);
			auto* object = RE::TESForm::LookupByID<RE::TESBoundObject>(pending.objectID);
			if (!actor || !object) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: post transfer ignored (actor/object lookup failed)");
				}
				return;
			}
			LogActorHands(actor, "postTransfer-pre");

			// Corpse post-transfer path equips directly on the dead actor.
			if (pending.corpse) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: corpse post transfer equip actor={:08X} '{}' object={:08X} '{}'",
						SafeFormID(actor), SafeName(actor),
						SafeFormID(object), SafeName(object));
				}

				RE::ExtraDataList* xList = nullptr;
				if (ctx.transferredXList && !ctx.transferredXListNonUnique) {
					xList = ctx.transferredXList;
				} else {
					if (auto* changes = actor->GetInventoryChanges(); changes && changes->entryList) {
						for (auto* entry : *changes->entryList) {
							if (entry && entry->GetObject() == object) {
								xList = SyntheticXList::EnsureXList(actor, entry);
								break;
							}
						}
					}
				}

				const bool ok = PerformCorpseEquipOnly(actor, object, xList, pending.hand);
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: corpse post transfer equip result={}", ok);
				}
				LogActorHands(actor, "postTransfer-post");
				return;
			}

			// Post-transfer equip-only requires a unique captured xList owned by the target.

			if (ctx.transferredXListNonUnique) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: post transfer equipOnly skipped (transferred xList non-unique)");
				}
				LogActorHands(actor, "postTransfer-post");
				return;
			}
			if (ctx.transferredToActor && ctx.transferredToActor->GetFormID() != pending.actorID) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: post transfer ignored (transferredToActor mismatch)");
				}
				LogActorHands(actor, "postTransfer-post");
				return;
			}

			if (ctx.transferredXList && ctx.transferredToActor) {
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: post transfer equipOnly strict path (transfer-captured xList)");
				}
				if (spdlog::should_log(spdlog::level::trace)) {
					logger::trace("EquipModeTrace: transferred xListPtr={:p}", static_cast<void*>(ctx.transferredXList));
				}
				(void)PerformEquipOnlyStrictByXList(actor, object, ctx.transferredXList, pending.hand);
			} else {
				// No engine-captured xList; inject a synthetic xList into the actor-owned entry.
				if (ShouldLogEquipModeTrace()) {
					logger::debug("EquipModeTrace: post transfer no captured xList, attempting synthetic injection");
				}
				RE::ExtraDataList* injected = nullptr;
				if (auto* changes = actor->GetInventoryChanges(); changes && changes->entryList) {
					for (auto* entry : *changes->entryList) {
						if (entry && entry->GetObject() == object) {
							injected = SyntheticXList::EnsureXList(actor, entry);
							break;
						}
					}
				}
				if (injected) {
					if (ShouldLogEquipModeTrace()) {
						logger::debug("EquipModeTrace: post transfer injection ok, using strict path");
					}
					(void)PerformEquipOnlyStrictByXList(actor, object, injected, pending.hand);
				} else {
					if (ShouldLogEquipModeTrace()) {
						logger::debug("EquipModeTrace: post transfer injection FAILED, equip skipped (fail-closed)");
					}
				}
			}
			LogActorHands(actor, "postTransfer-post");
		}
	}

	void Router::Install()
	{
		if (g_installed) {
			return;
		}

		// Transfer-owning modes must be active before ApplyRuntime can run.
		Modes::ConsumableMode::Install();
		Modes::PoisonMode::Install();
		Modes::SpellTomeMode::Install();
		Fixes::StaleWeightCacheFix::Install();
		Fixes::ZeroWeightTakeAllFix::Install();
		Fixes::QuantityMenuBlocker::Install();

		ContainerMenuTransferHook::Install();
		g_transferHandle = ContainerMenuTransferHook::AddHandler(OnTransferAttempt);
		g_preHandle = ContainerMenuTransferHook::AddPreListener(OnTransferPre);
		g_postHandle = ContainerMenuTransferHook::AddPostListener(OnTransferPost);
		g_installed = true;
		logger::info("EquipMode::Router: installed");
	}

	void Router::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_transferHandle != 0) {
			ContainerMenuTransferHook::RemoveListener(g_transferHandle);
			g_transferHandle = 0;
		}
		if (g_preHandle != 0) {
			ContainerMenuTransferHook::RemoveListener(g_preHandle);
			g_preHandle = 0;
		}
		if (g_postHandle != 0) {
			ContainerMenuTransferHook::RemoveListener(g_postHandle);
			g_postHandle = 0;
		}

		g_installed = false;
		Fixes::QuantityMenuBlocker::Uninstall();
		Fixes::ZeroWeightTakeAllFix::Uninstall();
		Fixes::StaleWeightCacheFix::Uninstall();
		Modes::SpellTomeMode::Uninstall();
		Modes::PoisonMode::Uninstall();
		Modes::ConsumableMode::Uninstall();
		logger::info("EquipMode::Router: uninstalled");
	}
}
