#include "NonPlayableItemSanitizer.h"

#include "ActorScope.h"
#include "ContainerMenuDisplayHook.h"
#include "ContainerMenuUtil.h"
#include "Logging.h"
#include "PluginSettings.h"
#include "WeaponBound.h"

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "RE/B/BGSDefaultObjectManager.h"
#include "RE/T/TESWeightForm.h"

namespace FEC
{
	namespace
	{
		bool g_installed{ false };
		ContainerMenuDisplayHook::ListenerHandle g_listenerHandle{ 0 };
		ContainerMenuUtil::ListenerHandle g_closeListenerHandle{ 0 };
		bool g_processed{ false };

		struct Removal
		{
			RE::TESBoundObject* obj{ nullptr };
			RE::ExtraDataList* xList{ nullptr };
			std::int32_t count{ 0 };
		};

		[[nodiscard]] bool ShouldRemoveNonPlayableWeaponArmorAmmo(RE::TESBoundObject& a_obj)
		{
			if (a_obj.GetPlayable()) {
				return false;
			}

			if (a_obj.IsWeapon()) {
				auto* weap = a_obj.As<RE::TESObjectWEAP>();
				if (!weap) {
					return false;
				}
				// Bound weapons are transient; removing them can end the conjuration.
				if (WeaponBound::IsBoundWeapon(weap)) {
					return false;
				}
				// Never remove the engine's hand-to-hand fallback.
				if (weap->IsHandToHandMelee()) {
					return false;
				}
				return true;
			}

			if (a_obj.IsArmor()) {
				return true;
			}

			// CommonLibSSE-NG does not consistently expose IsAmmo() on all wrappers.
			if (auto* ammo = a_obj.As<RE::TESAmmo>(); ammo) {
				// Bound arrows are transient; removing them can end the conjuration.
				if (auto* dom = RE::BGSDefaultObjectManager::GetSingleton(); dom) {
					auto* boundArrowKW = dom->GetObject<RE::BGSKeyword>(RE::DEFAULT_OBJECT::kKeywordWeaponTypeBoundArrow);
					if (boundArrowKW && ammo->HasKeyword(boundArrowKW)) {
						return false;
					}
				}
				return true;
			}

			return false;
		}

		// Runtime-created forms and model-less armor can be invisible technical items.
		// HDT-SMP physics objects and softbody data should never be removed.
		[[nodiscard]] bool IsTechnicalItem(RE::TESBoundObject* a_obj)
		{
			if (a_obj->IsDynamicForm()) {
				return true;
			}

			auto* armo = a_obj->As<RE::TESObjectARMO>();
			if (armo) {
				// GetModel() returns a static empty string (never nullptr) when no model is set.
				const bool noModel = armo->worldModels[RE::TESBipedModelForm::Sexes::kMale].GetModel()[0] == '\0' &&
				                     armo->worldModels[RE::TESBipedModelForm::Sexes::kFemale].GetModel()[0] == '\0';
				if (noModel) {
					return true;
				}
			}

			return false;
		}

		[[nodiscard]] bool RemoveNonPlayableItems(RE::Actor* a_actor)
		{
			if (!a_actor || a_actor->IsPlayerRef() || a_actor->IsDead()) {
				return false;
			}
			if (!ActorScope::IsAffectedFollower(a_actor)) {
				return false;
			}

			const bool isSummonActor = ActorScope::IsPlayerCommandedActor(a_actor);
			if (isSummonActor) {
				return false;
			}

			const auto& hidden = PluginSettings::Get().hiddenItems;
			if (!hidden.enableNonPlayableItems) {
				return false;
			}
			if (!hidden.removeHiddenWeapon && !hidden.removeHiddenArmor && !hidden.removeHiddenAmmo) {
				return false;
			}

			std::vector<Removal> removals;
			removals.reserve(64);

			auto inv = a_actor->GetInventory([](RE::TESBoundObject& a_obj) {
				return ShouldRemoveNonPlayableWeaponArmorAmmo(a_obj);
			});

			for (auto& [obj, data] : inv) {
				if (!obj) {
					continue;
				}

				if (IsTechnicalItem(obj)) {
					continue;
				}

				// RemoveItem bypasses normal quest-item drop prevention.
				// Do not delete items tracked by quest aliases.
				if (data.second && data.second->IsQuestObject()) {
					continue;
				}

				auto* asArmo = obj->As<RE::TESObjectARMO>();
				const bool isShield = asArmo && asArmo->IsShield();

				if (obj->IsWeapon() || isShield) {
					auto* weightForm = obj->As<RE::TESWeightForm>();
					if (!weightForm || weightForm->weight <= 0.0f) {
						continue;
					}
					if (!hidden.removeHiddenWeapon) {
						continue;
					}
				}
				else if (asArmo) {
					auto* weightForm = obj->As<RE::TESWeightForm>();
					if (!weightForm || weightForm->weight <= 0.0f) {
						continue;
					}
					if (!hidden.removeHiddenArmor) {
						continue;
					}
				}
				else {
					// Only ammo reaches this branch because of the inventory filter.
					if (!hidden.removeHiddenAmmo) {
						continue;
					}
				}

				const auto count = static_cast<std::int32_t>(data.first);
				auto* entry = data.second.get();
				if (!entry || !entry->extraLists || entry->extraLists->empty()) {
					if (count > 0) {
						removals.push_back(Removal{ obj, nullptr, count });
					}
					continue;
				}

				std::int32_t removedViaXLists = 0;
				for (auto* xList : *entry->extraLists) {
					if (!xList) {
						continue;
					}
					removals.push_back(Removal{ obj, xList, 1 });
					removedViaXLists += 1;
				}

				// Remove any count not represented by extraLists without an instance list.
				const auto remainder = count - removedViaXLists;
				if (remainder > 0) {
					removals.push_back(Removal{ obj, nullptr, remainder });
				}
			}

			std::int32_t totalRemoved = 0;
			std::unordered_set<RE::FormID> removedBaseIDs;
			removedBaseIDs.reserve(removals.size());

			for (const auto& rem : removals) {
				if (!rem.obj || rem.count <= 0) {
					continue;
				}
				a_actor->RemoveItem(rem.obj, rem.count, RE::ITEM_REMOVE_REASON::kRemove, rem.xList, nullptr);
				totalRemoved += rem.count;
				removedBaseIDs.insert(rem.obj->GetFormID());
			}

			if (totalRemoved > 0) {
				logger::info(
					"Removed non-playable items (ContainerMenu): {:08X} ({}) removedCount={} distinctBases={}",
					a_actor->GetFormID(),
					a_actor->GetName(),
					totalRemoved,
					static_cast<std::uint32_t>(removedBaseIDs.size()));
			}

			return totalRemoved > 0;
		}
	}

	void NonPlayableItemSanitizer::Install()
	{
		const auto& settings = PluginSettings::Get();
		const auto& hidden = settings.hiddenItems;
		const bool anyFollowerWork =
			hidden.enableNonPlayableItems &&
			(hidden.removeHiddenWeapon || hidden.removeHiddenArmor || hidden.removeHiddenAmmo);
		if (!anyFollowerWork) {
			Uninstall();
			return;
		}

		if (g_installed) {
			return;
		}

		ContainerMenuDisplayHook::Install();

		// Reset per-session processing when the menu closes.
		g_closeListenerHandle = ContainerMenuUtil::AddOnContainerMenuCloseListener([]() {
			g_processed = false;
		});

		g_listenerHandle = ContainerMenuDisplayHook::AddPreDisplayListener([](RE::ContainerMenu* a_menu) {
			auto target = ContainerMenuUtil::GetAffectedTarget(a_menu);
			if (!target) {
				return;
			}
			if (!ActorScope::IsAffectedFollower(target.get())) {
				return;
			}

			if (g_processed) {
				return;
			}
			g_processed = true;

			const auto actorID = target->GetFormID();

			// Defer scan and removal outside the Scaleform render callback.
			// Synchronous RemoveItem mid-render can crash with Skyrim Souls RE.
			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}

			taskInterface->AddTask([actorID]() {
				auto* actor = RE::TESForm::LookupByID<RE::Actor>(actorID);
				if (!actor || actor->IsDead()) {
					return;
				}
				const bool didRemove = RemoveNonPlayableItems(actor);
				if (didRemove) {
					ContainerMenuUtil::QueueRefreshForActor(actorID);
				}
			});
		});

		g_installed = true;
		logger::info("NonPlayableItemSanitizer: installed (ContainerMenu pre-display)");
	}

	void NonPlayableItemSanitizer::Uninstall()
	{
		if (!g_installed) {
			return;
		}

		if (g_listenerHandle != 0) {
			ContainerMenuDisplayHook::UnregisterListener(g_listenerHandle);
			g_listenerHandle = 0;
		}

		if (g_closeListenerHandle != 0) {
			ContainerMenuUtil::RemoveOnContainerMenuCloseListener(g_closeListenerHandle);
			g_closeListenerHandle = 0;
		}

		g_processed = false;
		g_installed = false;
		logger::info("NonPlayableItemSanitizer: uninstalled");
	}
}
