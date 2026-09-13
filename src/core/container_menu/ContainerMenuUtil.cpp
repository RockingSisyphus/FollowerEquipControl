#include "ContainerMenuUtil.h"

#include "ActorScope.h"
#include "Controls.h"
#include "ActorInclusion.h"
#include "KnownFollowerState.h"

#include "PluginSettings.h"
#include "Relocations.h"

#ifndef WIN32_LEAN_AND_MEAN
#	define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <mutex>
#include <utility>
#include <vector>

namespace FEC
{
	namespace ContainerMenuUtil
	{
		namespace
		{
			std::mutex g_listenerMutex;
			std::vector<std::pair<ListenerHandle, std::function<void()>>> g_closeListeners;
			ListenerHandle g_nextListenerHandle{ 1 };
			inline bool g_menuWatcherInstalled{ false };
			std::atomic_bool g_containerMenuClosing{ false };

			[[nodiscard]] bool TryGetRootBool(RE::ContainerMenu* a_menu, const char* a_memberName, bool& a_out)
			{
				if (!a_menu || !a_memberName) {
					return false;
				}
				auto& root = a_menu->GetRuntimeData().root;
				RE::GFxValue val;
				if (!root.GetMember(a_memberName, &val) || !val.IsBool()) {
					return false;
				}
				a_out = val.GetBool();
				return true;
			}

			[[nodiscard]] bool TryGetRootNumber(RE::ContainerMenu* a_menu, const char* a_memberName, double& a_out)
			{
				if (!a_menu || !a_memberName) {
					return false;
				}
				auto& root = a_menu->GetRuntimeData().root;
				RE::GFxValue val;
				if (!root.GetMember(a_memberName, &val) || !val.IsNumber()) {
					return false;
				}
				a_out = val.GetNumber();
				return true;
			}

			// ItemList::UpdateMenu can make SkyUI's held-key Equip Mode look "released"
			// because Scaleform state is rebuilt without replaying key-down events.
			// Re-latch it from the current physical key state.
			void RelatchHeldEquipModes(RE::ContainerMenu* a_menu)
			{
				if (!a_menu) {
					return;
				}

				double platform{};
				if (TryGetRootNumber(a_menu, "_platform", platform)) {
					// On gamepad (_platform != 0), SkyUI keeps _bEquipMode permanently true.
					// No relatch is needed.
					if (static_cast<std::int32_t>(platform) != 0) {
						return;
					}
				}

				auto& root = a_menu->GetRuntimeData().root;

				double equipModeKey{};
				if (TryGetRootNumber(a_menu, "_equipModeKey", equipModeKey)) {
					const auto key = static_cast<std::uint32_t>(equipModeKey);
					const bool pressed = Controls::IsDikKeyDown(key);
					root.SetMember("_bEquipMode", RE::GFxValue(pressed));
				}
			}

			class ContainerMenuOpenCloseSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
			{
			public:
				static ContainerMenuOpenCloseSink* GetSingleton()
				{
					static ContainerMenuOpenCloseSink s;
					return std::addressof(s);
				}

				RE::BSEventNotifyControl ProcessEvent(
					const RE::MenuOpenCloseEvent* a_event,
					RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*a_eventSource*/) override
				{
					if (!a_event) {
						return RE::BSEventNotifyControl::kContinue;
					}

					if (a_event->menuName != RE::ContainerMenu::MENU_NAME) {
						return RE::BSEventNotifyControl::kContinue;
					}

					if (a_event->opening) {
						g_containerMenuClosing.store(false);
						return RE::BSEventNotifyControl::kContinue;
					}

					g_containerMenuClosing.store(true);

					logger::debug("ContainerMenuUtil: ContainerMenu closing, dispatching {} close listener(s)",
						g_closeListeners.size());

					// Copy under lock, then invoke callbacks without holding the mutex.
					thread_local std::vector<std::function<void()>> listeners;
					listeners.clear();
					{
						std::lock_guard lock(g_listenerMutex);
						listeners.reserve(g_closeListeners.size());
						for (auto& entry : g_closeListeners) {
							listeners.emplace_back(entry.second);
						}
					}
					for (auto& listener : listeners) {
						if (listener) {
							listener();
						}
					}

					return RE::BSEventNotifyControl::kContinue;
				}
			};
		}

		void InstallMenuOpenCloseWatcher()
		{
			if (g_menuWatcherInstalled) {
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			ui->AddEventSink<RE::MenuOpenCloseEvent>(ContainerMenuOpenCloseSink::GetSingleton());
			g_menuWatcherInstalled = true;
			logger::info("ContainerMenuUtil: installed MenuOpenClose watcher");
		}

		bool IsContainerMenuClosing()
		{
			return g_containerMenuClosing.load();
		}

		void UninstallMenuOpenCloseWatcher()
		{
			if (!g_menuWatcherInstalled) {
				return;
			}
			auto* ui = RE::UI::GetSingleton();
			if (ui) {
				ui->RemoveEventSink<RE::MenuOpenCloseEvent>(ContainerMenuOpenCloseSink::GetSingleton());
			}
			g_menuWatcherInstalled = false;
			logger::info("ContainerMenuUtil: uninstalled MenuOpenClose watcher");
		}

		ListenerHandle AddOnContainerMenuCloseListener(std::function<void()> a_listener)
		{
			if (!a_listener) {
				return 0;
			}
			std::lock_guard lock(g_listenerMutex);
			const auto handle = g_nextListenerHandle++;
			g_closeListeners.emplace_back(handle, std::move(a_listener));
			return handle;
		}

		bool RemoveOnContainerMenuCloseListener(ListenerHandle a_handle)
		{
			if (a_handle == 0) {
				return false;
			}
			std::lock_guard lock(g_listenerMutex);
			for (auto it = g_closeListeners.begin(); it != g_closeListeners.end(); ++it) {
				if (it->first == a_handle) {
					g_closeListeners.erase(it);
					return true;
				}
			}
			return false;
		}

		bool IsContainerMenuOpen()
		{
			if (g_containerMenuClosing.load()) {
				return false;
			}
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return false;
			}
			auto menu = ui->GetMenu<RE::ContainerMenu>();
			return menu.get() != nullptr;
		}

		RE::GPtr<RE::ContainerMenu> GetOpenContainerMenu()
		{
			if (g_containerMenuClosing.load()) {
				return {};
			}
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return {};
			}
			return ui->GetMenu<RE::ContainerMenu>();
		}

		// Resolve a container-menu target handle to Actor* only after validating FormType.
		//
		// `Actor::LookupByHandle` does not validate the resolved REFR type, and
		// `ContainerMenu::GetTargetRefHandle` can point to a container REFR in Loot mode.
		// Treating a non-actor REFR as `Actor*` can crash when actor-only methods read
		// past the object's actual layout.
		RE::NiPointer<RE::Actor> ResolveActorHandle(RE::RefHandle a_handle)
		{
			RE::NiPointer<RE::TESObjectREFR> refr;
			RE::LookupReferenceByHandle(a_handle, refr);
			if (!refr || refr->GetFormType() != RE::FormType::ActorCharacter) {
				return {};
			}
			return RE::NiPointer<RE::Actor>{ static_cast<RE::Actor*>(refr.get()) };
		}

		RE::NiPointer<RE::Actor> GetNpcTarget(RE::ContainerMenu* a_menu)
		{
			if (!a_menu || a_menu->GetContainerMode() != RE::ContainerMenu::ContainerMode::kNPCMode) {
				return {};
			}

			return ResolveActorHandle(a_menu->GetTargetRefHandle());
		}

		RE::NiPointer<RE::Actor> GetAffectedTarget(RE::ContainerMenu* a_menu)
		{
			if (!a_menu) {
				return {};
			}

			auto target = ResolveActorHandle(a_menu->GetTargetRefHandle());
			if (!target || target->IsPlayerRef() || target->IsDead()) {
				return {};
			}

			if (ActorScope::ShouldAdmitForContainerMenu(target.get(), a_menu->GetContainerMode())) {
				return target;
			}

			return {};
		}

		void QueueRefreshForActor(RE::FormID a_actorID)
		{
			static REL::Relocation<std::uint64_t(RE::ItemList*, RE::Actor*)> UpdateMenu{ Relocations::kItemListUpdate };
			if (!UpdateMenu.address()) {
				logger::warn("ContainerMenuUtil: ItemList update relocation not found");
				return;
			}

			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}

			// Defer to the next frame to avoid re-entrancy while the menu is still laying out or filtering.
			taskInterface->AddTask([a_actorID]() {
				auto menu = GetOpenContainerMenu();
				if (!menu) {
					return;
				}

				auto current = ResolveActorHandle(menu->GetTargetRefHandle());
				if (!current || current->GetFormID() != a_actorID) {
					return;
				}

				auto* itemList = menu->GetRuntimeData().itemList;
				if (!itemList) {
					return;
				}

				// Refresh both sides; the ContainerMenu can show either the NPC target side or the player side.
				UpdateMenu(itemList, current.get());
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					UpdateMenu(itemList, player);
				}

				RelatchHeldEquipModes(menu.get());
			});
		}

		void QueueRefreshForOpenContainerMenu()
		{
			static REL::Relocation<std::uint64_t(RE::ItemList*, RE::Actor*)> UpdateMenu{ Relocations::kItemListUpdate };
			if (!UpdateMenu.address()) {
				logger::warn("ContainerMenuUtil: ItemList update relocation not found");
				return;
			}

			auto* taskInterface = SKSE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}

			// Defer to the next frame to avoid re-entrancy while the menu is still laying out or filtering.
			taskInterface->AddTask([]() {
				auto menu = GetOpenContainerMenu();
				if (!menu) {
					return;
				}

				auto* itemList = menu->GetRuntimeData().itemList;
				if (!itemList) {
					return;
				}

				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					UpdateMenu(itemList, player);
				}

				{
					auto target = ResolveActorHandle(menu->GetTargetRefHandle());
					if (target) {
						UpdateMenu(itemList, target.get());
					}
				}

				RelatchHeldEquipModes(menu.get());
			});
		}

		void Refresh3DAndMenu(RE::Actor* a_actor)
		{
			if (!a_actor) {
				return;
			}

			if (a_actor->Is3DLoaded()) {
				a_actor->Update3DModel();
			}

			auto menu = GetOpenContainerMenu();
			if (!menu) {
				return;
			}

			auto target = ResolveActorHandle(menu->GetTargetRefHandle());
			if (target) {
				QueueRefreshForActor(target->GetFormID());
				return;
			}

			QueueRefreshForOpenContainerMenu();
		}

		RE::ItemList* GetItemList(RE::ContainerMenu* a_menu)
		{
			if (!a_menu) {
				return nullptr;
			}
			return a_menu->GetRuntimeData().itemList;
		}
	}
}
