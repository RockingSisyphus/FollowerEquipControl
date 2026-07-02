#include "Serialization.h"

#include "CombatEquipPreference.h"
#include "CombatEquipOverrideState.h"
#include "HandItemRestore.h"
#include "PreCombatEquipRestore.h"
#include "HeadgearAutoEquip.h"
#include "KnownFollowerState.h"
#include "OutfitSnapshotRestore.h"
#include "SpellSuppressionState.h"
#include "ActorScopeExcludeState.h"
#include "EquipGateTelemetry.h"
#include "InstanceSignature.h"

#include <vector>

namespace FEC
{
	namespace
	{
		constexpr std::uint32_t kSerializationPluginID = 'FEC0';
		constexpr std::uint32_t kRecordKnownFollowers = 'KNFL';
		constexpr std::uint32_t kRecordCombatEquipPreference = 'CEPR';
		constexpr std::uint32_t kRecordOutfitSnapshotRestore = 'OSRS';
		constexpr std::uint32_t kRecordHandItemRestore = 'HIRS';
		constexpr std::uint32_t kRecordPreCombatEquipRestore = 'CEPC';
		constexpr std::uint32_t kRecordHeadgearAutoEquip = 'CHGT';
		constexpr std::uint32_t kRecordSpellSuppression = 'SSPL';
		constexpr std::uint32_t kRecordActorScopeDisable = 'ACDS';
		constexpr std::uint32_t kRecordVersionKnownFollowers = 2;
		constexpr std::uint32_t kRecordVersionCombatEquipPreference = 2;
		constexpr std::uint32_t kRecordVersionOutfitSnapshotRestore = 2;
		constexpr std::uint32_t kRecordVersionHandItemRestore = 2;
		constexpr std::uint32_t kRecordVersionPreCombatEquipRestore = 2;
		constexpr std::uint32_t kRecordVersionHeadgearAutoEquip = 2;
		constexpr std::uint32_t kRecordVersionSpellSuppression = 2;
		constexpr std::uint32_t kRecordVersionActorScopeDisable = 2;

		[[nodiscard]] bool WriteBool(SKSE::SerializationInterface* a_intfc, bool a_value)
		{
			const std::uint8_t v = a_value ? 1u : 0u;
			return a_intfc->WriteRecordData(&v, sizeof(v));
		}

		[[nodiscard]] bool ReadBool(SKSE::SerializationInterface* a_intfc, bool& a_out)
		{
			std::uint8_t v{ 0 };
			if (!a_intfc->ReadRecordData(&v, sizeof(v))) {
				return false;
			}
			a_out = (v != 0);
			return true;
		}

		[[nodiscard]] bool WriteString(SKSE::SerializationInterface* a_intfc, const std::string& a_value)
		{
			constexpr std::uint32_t kMax = 4096;
			const auto rawSize = a_value.size();
			const std::uint32_t size = rawSize > kMax ? kMax : static_cast<std::uint32_t>(rawSize);
			if (!a_intfc->WriteRecordData(&size, sizeof(size))) {
				return false;
			}
			if (size == 0) {
				return true;
			}
			return a_intfc->WriteRecordData(a_value.data(), size);
		}

		[[nodiscard]] bool ReadString(SKSE::SerializationInterface* a_intfc, std::string& a_out)
		{
			constexpr std::uint32_t kMax = 4096;
			std::uint32_t size{ 0 };
			if (!a_intfc->ReadRecordData(&size, sizeof(size))) {
				return false;
			}
			if (size > kMax) {
				return false;
			}
			a_out.clear();
			if (size == 0) {
				return true;
			}
			a_out.resize(size);
			return a_intfc->ReadRecordData(a_out.data(), size);
		}

		// Serialize only stable identity; volatile extras such as poison and health are excluded so signatures survive trade/transfer cycles.
		[[nodiscard]] bool WriteCEPSignature(SKSE::SerializationInterface* a_intfc, const InstanceSignature& a_sig)
		{
			if (!WriteBool(a_intfc, a_sig.hasTextDisplayData)) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasCustomName)) {
				return false;
			}
			if (!WriteString(a_intfc, a_sig.customName)) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasTemperFactor) || !a_intfc->WriteRecordData(&a_sig.temperFactor, sizeof(a_sig.temperFactor))) {
				return false;
			}

			if (!WriteBool(a_intfc, a_sig.hasEnchantmentExtra)) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasEnchantmentFormID) || !a_intfc->WriteRecordData(&a_sig.enchantmentFormID, sizeof(a_sig.enchantmentFormID))) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasEnchantmentCharge) || !a_intfc->WriteRecordData(&a_sig.enchantmentCharge, sizeof(a_sig.enchantmentCharge))) {
				return false;
			}

			return true;
		}

		[[nodiscard]] bool ReadCEPSignature(SKSE::SerializationInterface* a_intfc, InstanceSignature& a_out)
		{
			a_out = InstanceSignature{};
			a_out.hasChargeExtra = false;
			a_out.charge = 0.0f;
			a_out.hasHealthExtra = false;
			a_out.health = 0.0f;
			a_out.hasRemoveOnUnequip = false;
			a_out.removeOnUnequip = false;
			a_out.equipState = InstanceSignature::EquipState::kUnknown;

			if (!ReadBool(a_intfc, a_out.hasTextDisplayData)) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasCustomName)) {
				return false;
			}
			if (!ReadString(a_intfc, a_out.customName)) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasTemperFactor) || !a_intfc->ReadRecordData(&a_out.temperFactor, sizeof(a_out.temperFactor))) {
				return false;
			}

			if (!ReadBool(a_intfc, a_out.hasEnchantmentExtra)) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasEnchantmentFormID) || !a_intfc->ReadRecordData(&a_out.enchantmentFormID, sizeof(a_out.enchantmentFormID))) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasEnchantmentCharge) || !a_intfc->ReadRecordData(&a_out.enchantmentCharge, sizeof(a_out.enchantmentCharge))) {
				return false;
			}
			return true;
		}

		[[nodiscard]] bool WriteFullSignature(SKSE::SerializationInterface* a_intfc, const InstanceSignature& a_sig)
		{
			if (!WriteBool(a_intfc, a_sig.hasTextDisplayData)) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasCustomName)) {
				return false;
			}
			if (!WriteString(a_intfc, a_sig.customName)) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasTemperFactor) || !a_intfc->WriteRecordData(&a_sig.temperFactor, sizeof(a_sig.temperFactor))) {
				return false;
			}

			if (!WriteBool(a_intfc, a_sig.hasPoisonExtra)) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasPoisonFormID) || !a_intfc->WriteRecordData(&a_sig.poisonFormID, sizeof(a_sig.poisonFormID))) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasPoisonCount) || !a_intfc->WriteRecordData(&a_sig.poisonCount, sizeof(a_sig.poisonCount))) {
				return false;
			}

			if (!WriteBool(a_intfc, a_sig.hasEnchantmentExtra)) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasEnchantmentFormID) || !a_intfc->WriteRecordData(&a_sig.enchantmentFormID, sizeof(a_sig.enchantmentFormID))) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasEnchantmentCharge) || !a_intfc->WriteRecordData(&a_sig.enchantmentCharge, sizeof(a_sig.enchantmentCharge))) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasRemoveOnUnequip) || !a_intfc->WriteRecordData(&a_sig.removeOnUnequip, sizeof(a_sig.removeOnUnequip))) {
				return false;
			}

			if (!WriteBool(a_intfc, a_sig.hasChargeExtra) || !a_intfc->WriteRecordData(&a_sig.charge, sizeof(a_sig.charge))) {
				return false;
			}
			if (!WriteBool(a_intfc, a_sig.hasHealthExtra) || !a_intfc->WriteRecordData(&a_sig.health, sizeof(a_sig.health))) {
				return false;
			}

			if (!WriteBool(a_intfc, a_sig.capturedFromNonUniqueRow)) {
				return false;
			}
			const auto equipStateRaw = static_cast<std::uint8_t>(a_sig.equipState);
			if (!a_intfc->WriteRecordData(&equipStateRaw, sizeof(equipStateRaw))) {
				return false;
			}

			return true;
		}

		[[nodiscard]] bool ReadFullSignature(SKSE::SerializationInterface* a_intfc, InstanceSignature& a_out)
		{
			a_out = InstanceSignature{};

			if (!ReadBool(a_intfc, a_out.hasTextDisplayData)) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasCustomName)) {
				return false;
			}
			if (!ReadString(a_intfc, a_out.customName)) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasTemperFactor) || !a_intfc->ReadRecordData(&a_out.temperFactor, sizeof(a_out.temperFactor))) {
				return false;
			}

			if (!ReadBool(a_intfc, a_out.hasPoisonExtra)) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasPoisonFormID) || !a_intfc->ReadRecordData(&a_out.poisonFormID, sizeof(a_out.poisonFormID))) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasPoisonCount) || !a_intfc->ReadRecordData(&a_out.poisonCount, sizeof(a_out.poisonCount))) {
				return false;
			}

			if (!ReadBool(a_intfc, a_out.hasEnchantmentExtra)) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasEnchantmentFormID) || !a_intfc->ReadRecordData(&a_out.enchantmentFormID, sizeof(a_out.enchantmentFormID))) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasEnchantmentCharge) || !a_intfc->ReadRecordData(&a_out.enchantmentCharge, sizeof(a_out.enchantmentCharge))) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasRemoveOnUnequip) || !a_intfc->ReadRecordData(&a_out.removeOnUnequip, sizeof(a_out.removeOnUnequip))) {
				return false;
			}

			if (!ReadBool(a_intfc, a_out.hasChargeExtra) || !a_intfc->ReadRecordData(&a_out.charge, sizeof(a_out.charge))) {
				return false;
			}
			if (!ReadBool(a_intfc, a_out.hasHealthExtra) || !a_intfc->ReadRecordData(&a_out.health, sizeof(a_out.health))) {
				return false;
			}

			if (!ReadBool(a_intfc, a_out.capturedFromNonUniqueRow)) {
				return false;
			}
			std::uint8_t equipStateRaw{ 0 };
			if (!a_intfc->ReadRecordData(&equipStateRaw, sizeof(equipStateRaw))) {
				return false;
			}
			a_out.equipState = static_cast<InstanceSignature::EquipState>(equipStateRaw);
			return true;
		}

		[[nodiscard]] bool ResolveSignatureFormIDs(SKSE::SerializationInterface* a_intfc, InstanceSignature& a_sig)
		{
			if (a_sig.hasPoisonFormID && a_sig.poisonFormID != 0) {
				RE::FormID resolved{ 0 };
				if (!a_intfc->ResolveFormID(a_sig.poisonFormID, resolved)) {
					return false;
				}
				a_sig.poisonFormID = resolved;
			}
			if (a_sig.hasEnchantmentFormID && a_sig.enchantmentFormID != 0) {
				RE::FormID resolved{ 0 };
				if (!a_intfc->ResolveFormID(a_sig.enchantmentFormID, resolved)) {
					return false;
				}
				a_sig.enchantmentFormID = resolved;
			}
			return true;
		}

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			if (!a_intfc) {
				return;
			}

			{
				const std::uint32_t count = KnownFollowerState::GetKnownFollowerCount();
				if (a_intfc->OpenRecord(kRecordKnownFollowers, kRecordVersionKnownFollowers)) {
					a_intfc->WriteRecordData(&count, sizeof(count));
					KnownFollowerState::ForEachKnownFollower([&](RE::FormID formID) {
						a_intfc->WriteRecordData(&formID, sizeof(formID));
					});
				}
			}

			{

			}

			{
				auto entries = CombatEquipPreference::SnapshotEntries();
				const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
				if (a_intfc->OpenRecord(kRecordCombatEquipPreference, kRecordVersionCombatEquipPreference)) {
					a_intfc->WriteRecordData(&count, sizeof(count));
					for (const auto& e : entries) {
						const auto catRaw = static_cast<std::uint8_t>(e.category);
						a_intfc->WriteRecordData(&e.actorID, sizeof(e.actorID));
						a_intfc->WriteRecordData(&catRaw, sizeof(catRaw));
						a_intfc->WriteRecordData(&e.entry.baseObjectID, sizeof(e.entry.baseObjectID));
						if (!WriteCEPSignature(a_intfc, e.entry.signature)) {
							logger::warn("Failed to write CEPR signature actor={:08X} cat={}", e.actorID, static_cast<std::uint32_t>(catRaw));
						}
					}
				}
			}

			{
				auto entries = OutfitSnapshotRestore::SnapshotEntries();
				const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
				if (a_intfc->OpenRecord(kRecordOutfitSnapshotRestore, kRecordVersionOutfitSnapshotRestore)) {
					a_intfc->WriteRecordData(&count, sizeof(count));
					for (const auto& e : entries) {
						a_intfc->WriteRecordData(&e.actorID, sizeof(e.actorID));
						a_intfc->WriteRecordData(&e.slotID, sizeof(e.slotID));
						a_intfc->WriteRecordData(&e.entry.baseObjectID, sizeof(e.entry.baseObjectID));
						const bool hasSig = e.entry.signature.has_value();
						if (!WriteBool(a_intfc, hasSig)) {
							continue;
						}
						if (hasSig) {
							if (!WriteFullSignature(a_intfc, *e.entry.signature)) {
								logger::warn("Failed to write OSRS signature actor={:08X}", e.actorID);
							}
						}
					}
				}
			}

			{
				auto entries = HandItemRestore::SnapshotEntries();
				const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
				if (a_intfc->OpenRecord(kRecordHandItemRestore, kRecordVersionHandItemRestore)) {
					a_intfc->WriteRecordData(&count, sizeof(count));
					for (const auto& e : entries) {
						const auto kindRaw = static_cast<std::uint8_t>(e.kind);
						a_intfc->WriteRecordData(&e.actorID, sizeof(e.actorID));
						a_intfc->WriteRecordData(&kindRaw, sizeof(kindRaw));
						a_intfc->WriteRecordData(&e.entry.baseObjectID, sizeof(e.entry.baseObjectID));
						const bool hasSig = e.entry.signature.has_value();
						if (!WriteBool(a_intfc, hasSig)) {
							continue;
						}
						if (hasSig) {
							if (!WriteFullSignature(a_intfc, *e.entry.signature)) {
								logger::warn("Failed to write HIRS signature actor={:08X}", e.actorID);
							}
						}
					}
				}
			}

			{
				auto entries = PreCombatEquipRestore::SnapshotEntries();
				const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
				if (a_intfc->OpenRecord(kRecordPreCombatEquipRestore, kRecordVersionPreCombatEquipRestore)) {
					a_intfc->WriteRecordData(&count, sizeof(count));
					for (const auto& e : entries) {
						a_intfc->WriteRecordData(&e.actorID, sizeof(e.actorID));
						const auto rightKind = static_cast<std::uint8_t>(e.right.kind);
						a_intfc->WriteRecordData(&rightKind, sizeof(rightKind));
						a_intfc->WriteRecordData(&e.right.baseObjectID, sizeof(e.right.baseObjectID));
						const bool hasRightSig = e.right.signature.has_value();
						if (!WriteBool(a_intfc, hasRightSig)) {
							continue;
						}
						if (hasRightSig) {
							if (!WriteFullSignature(a_intfc, *e.right.signature)) {
								logger::warn("Failed to write CEPC right signature actor={:08X}", e.actorID);
							}
						}

						const auto leftKind = static_cast<std::uint8_t>(e.left.kind);
						a_intfc->WriteRecordData(&leftKind, sizeof(leftKind));
						a_intfc->WriteRecordData(&e.left.baseObjectID, sizeof(e.left.baseObjectID));
						const bool hasLeftSig = e.left.signature.has_value();
						if (!WriteBool(a_intfc, hasLeftSig)) {
							continue;
						}
						if (hasLeftSig) {
							if (!WriteFullSignature(a_intfc, *e.left.signature)) {
								logger::warn("Failed to write CEPC left signature actor={:08X}", e.actorID);
							}
						}

						a_intfc->WriteRecordData(&e.ammoID, sizeof(e.ammoID));
					}
				}
			}

			{
				const auto entries = HeadgearAutoEquip::SnapshotEntries();
				if (!entries.empty()) {
					if (!a_intfc->OpenRecord(kRecordHeadgearAutoEquip, kRecordVersionHeadgearAutoEquip)) {
						logger::warn("Serialization: failed to open CHGT record");
					} else {
						const std::uint32_t count = static_cast<std::uint32_t>(entries.size());
						a_intfc->WriteRecordData(&count, sizeof(count));

						for (const auto& e : entries) {
							a_intfc->WriteRecordData(&e.actorID, sizeof(e.actorID));

							const std::uint8_t itemCount = static_cast<std::uint8_t>(
								std::min<std::size_t>(e.wornHeadgear.size(), 255));
							a_intfc->WriteRecordData(&itemCount, sizeof(itemCount));

							for (std::uint8_t j = 0; j < itemCount; ++j) {
								const auto& item = e.wornHeadgear[j];
								a_intfc->WriteRecordData(&item.baseObjectID, sizeof(item.baseObjectID));
								const bool hasSig = item.signature.has_value();
								if (!WriteBool(a_intfc, hasSig)) {
									logger::warn("Failed to write CHGT item signature flag actor={:08X}", e.actorID);
								}
								if (hasSig) {
									if (!WriteFullSignature(a_intfc, *item.signature)) {
										logger::warn("Failed to write CHGT item signature actor={:08X}", e.actorID);
									}
								}
							}
						}
					}
				}
			}

			{
				const std::uint32_t count = SpellSuppressionState::GetTotalCount();
				if (a_intfc->OpenRecord(kRecordSpellSuppression, kRecordVersionSpellSuppression)) {
					a_intfc->WriteRecordData(&count, sizeof(count));
					SpellSuppressionState::ForEachSuppressed([&](RE::FormID actorID, RE::FormID spellID) {
						a_intfc->WriteRecordData(&actorID, sizeof(actorID));
						a_intfc->WriteRecordData(&spellID, sizeof(spellID));
					});
				} else {
					logger::warn("Serialization: failed to open SSPL record");
				}
			}

			{
				const std::uint32_t count = ActorScopeExcludeState::GetCount();
				if (a_intfc->OpenRecord(kRecordActorScopeDisable, kRecordVersionActorScopeDisable)) {
					a_intfc->WriteRecordData(&count, sizeof(count));
					ActorScopeExcludeState::ForEach([&](RE::FormID actorID) {
						a_intfc->WriteRecordData(&actorID, sizeof(actorID));
					});
				} else {
					logger::warn("Serialization: failed to open ACDS record");
				}
			}

		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			if (!a_intfc) {
				return;
			}

			std::uint32_t type{ 0 };
			std::uint32_t version{ 0 };
			std::uint32_t length{ 0 };

			while (a_intfc->GetNextRecordInfo(type, version, length)) {
				if (type != kRecordKnownFollowers &&
					type != kRecordCombatEquipPreference &&
					type != kRecordOutfitSnapshotRestore &&
					type != kRecordHandItemRestore &&
					type != kRecordPreCombatEquipRestore &&
					type != kRecordHeadgearAutoEquip &&
					type != kRecordSpellSuppression &&
					type != kRecordActorScopeDisable) {
					continue;
				}
				if (type == kRecordKnownFollowers && version != kRecordVersionKnownFollowers) {
					logger::warn("Ignoring KNFL record with unexpected version {}", version);
					continue;
				}
				if (type == kRecordCombatEquipPreference && version != kRecordVersionCombatEquipPreference) {
					logger::warn("Ignoring CEPR record with unexpected version {}", version);
					continue;
				}
				if (type == kRecordOutfitSnapshotRestore && version != kRecordVersionOutfitSnapshotRestore) {
					logger::warn("Ignoring OSRS record with unexpected version {}", version);
					continue;
				}
				if (type == kRecordHandItemRestore && version != kRecordVersionHandItemRestore) {
					logger::warn("Ignoring HIRS record with unexpected version {}", version);
					continue;
				}
				if (type == kRecordPreCombatEquipRestore && version != kRecordVersionPreCombatEquipRestore) {
					logger::warn("Ignoring CEPC record with unexpected version {}", version);
					continue;
				}
				if (type == kRecordHeadgearAutoEquip && version != kRecordVersionHeadgearAutoEquip) {
					logger::warn("Ignoring CHGT record with unexpected version {}", version);
					continue;
				}
				if (type == kRecordSpellSuppression && version != kRecordVersionSpellSuppression) {
					logger::warn("Ignoring SSPL record with unexpected version {}", version);
					continue;
				}
				if (type == kRecordActorScopeDisable && version != kRecordVersionActorScopeDisable) {
					logger::warn("Ignoring ACDS record with unexpected version {}", version);
					continue;
				}
				std::uint32_t count{ 0 };
				if (!a_intfc->ReadRecordData(&count, sizeof(count))) {
					logger::warn("Failed to read record count for type {}", type);
					continue;
				}

				if (type == kRecordCombatEquipPreference) {
					for (std::uint32_t i = 0; i < count; i++) {
						RE::FormID storedActorID{ 0 };
						std::uint8_t catRaw{ 0 };
						RE::FormID storedBaseID{ 0 };
						if (!a_intfc->ReadRecordData(&storedActorID, sizeof(storedActorID)) ||
							!a_intfc->ReadRecordData(&catRaw, sizeof(catRaw)) ||
							!a_intfc->ReadRecordData(&storedBaseID, sizeof(storedBaseID))) {
							logger::warn("Failed to read CEPR entry header {}/{}", i + 1, count);
							break;
						}

						InstanceSignature sig;
						if (!ReadCEPSignature(a_intfc, sig)) {
							logger::warn("Failed to read CEPR entry signature {}/{}", i + 1, count);
							break;
						}

						RE::FormID actorID{ 0 };
						if (!a_intfc->ResolveFormID(storedActorID, actorID)) {
							continue;
						}
						RE::FormID baseID{ 0 };
						if (!a_intfc->ResolveFormID(storedBaseID, baseID)) {
							continue;
						}

						if (sig.hasEnchantmentFormID && sig.enchantmentFormID != 0) {
							RE::FormID resolved{ 0 };
							if (!a_intfc->ResolveFormID(sig.enchantmentFormID, resolved)) {
								continue;
							}
							sig.enchantmentFormID = resolved;
						}

						if (catRaw >= static_cast<std::uint8_t>(CombatEquipPreference::Category::kTotal)) {
							continue;
						}

						CombatEquipPreference::Entry entry;
						entry.baseObjectID = baseID;
						entry.signature = std::move(sig);
						CombatEquipPreference::SetLoadedEntry(actorID, static_cast<CombatEquipPreference::Category>(catRaw), std::move(entry));
					}
				} else if (type == kRecordOutfitSnapshotRestore) {
					for (std::uint32_t i = 0; i < count; i++) {
						RE::FormID storedActorID{ 0 };
						std::uint32_t slotID{ 0 };
						RE::FormID storedBaseID{ 0 };
						if (!a_intfc->ReadRecordData(&storedActorID, sizeof(storedActorID)) ||
							!a_intfc->ReadRecordData(&slotID, sizeof(slotID)) ||
							!a_intfc->ReadRecordData(&storedBaseID, sizeof(storedBaseID))) {
							logger::warn("Failed to read OSRS entry header {}/{}", i + 1, count);
							break;
						}
						bool hasSig = false;
						if (!ReadBool(a_intfc, hasSig)) {
							logger::warn("Failed to read OSRS signature flag {}/{}", i + 1, count);
							break;
						}
						std::optional<InstanceSignature> sig;
						if (hasSig) {
							InstanceSignature tmp;
							if (!ReadFullSignature(a_intfc, tmp) || !ResolveSignatureFormIDs(a_intfc, tmp)) {
								hasSig = false;
							} else {
								sig = std::move(tmp);
							}
						}

						RE::FormID actorID{ 0 };
						if (!a_intfc->ResolveFormID(storedActorID, actorID)) {
							continue;
						}
						RE::FormID baseID{ 0 };
						if (storedBaseID != 0 && !a_intfc->ResolveFormID(storedBaseID, baseID)) {
							continue;
						}

						OutfitSnapshotRestore::Entry entry{};
						entry.baseObjectID = baseID;
						entry.signature = std::move(sig);
						OutfitSnapshotRestore::SetLoadedEntry(
							actorID,
							slotID,
							std::move(entry));
					}
				} else if (type == kRecordHandItemRestore) {
					for (std::uint32_t i = 0; i < count; i++) {
						RE::FormID storedActorID{ 0 };
						std::uint8_t kindRaw{ 0 };
						RE::FormID storedBaseID{ 0 };
						if (!a_intfc->ReadRecordData(&storedActorID, sizeof(storedActorID)) ||
							!a_intfc->ReadRecordData(&kindRaw, sizeof(kindRaw)) ||
							!a_intfc->ReadRecordData(&storedBaseID, sizeof(storedBaseID))) {
							logger::warn("Failed to read HIRS entry header {}/{}", i + 1, count);
							break;
						}
						bool hasSig = false;
						if (!ReadBool(a_intfc, hasSig)) {
							logger::warn("Failed to read HIRS signature flag {}/{}", i + 1, count);
							break;
						}
						std::optional<InstanceSignature> sig;
						if (hasSig) {
							InstanceSignature tmp;
							if (!ReadFullSignature(a_intfc, tmp) || !ResolveSignatureFormIDs(a_intfc, tmp)) {
								hasSig = false;
							} else {
								sig = std::move(tmp);
							}
						}

						if (kindRaw > static_cast<std::uint8_t>(HandItemRestore::SlotKind::kAmmo)) {
							continue;
						}

						RE::FormID actorID{ 0 };
						if (!a_intfc->ResolveFormID(storedActorID, actorID)) {
							continue;
						}
						RE::FormID baseID{ 0 };
						if (storedBaseID != 0 && !a_intfc->ResolveFormID(storedBaseID, baseID)) {
							continue;
						}

						HandItemRestore::Entry entry{};
						entry.baseObjectID = baseID;
						entry.signature = std::move(sig);
						HandItemRestore::SetLoadedEntry(
							actorID,
							static_cast<HandItemRestore::SlotKind>(kindRaw),
							std::move(entry));
					}
				} else if (type == kRecordPreCombatEquipRestore) {
					for (std::uint32_t i = 0; i < count; i++) {
						RE::FormID storedActorID{ 0 };
						if (!a_intfc->ReadRecordData(&storedActorID, sizeof(storedActorID))) {
							logger::warn("Failed to read CEPC entry actor {}/{}", i + 1, count);
							break;
						}

						std::uint8_t rightKindRaw{ 0 };
						RE::FormID storedRightBase{ 0 };
						if (!a_intfc->ReadRecordData(&rightKindRaw, sizeof(rightKindRaw)) ||
							!a_intfc->ReadRecordData(&storedRightBase, sizeof(storedRightBase))) {
							logger::warn("Failed to read CEPC right header {}/{}", i + 1, count);
							break;
						}
						bool hasRightSig = false;
						if (!ReadBool(a_intfc, hasRightSig)) {
							logger::warn("Failed to read CEPC right signature flag {}/{}", i + 1, count);
							break;
						}
						std::optional<InstanceSignature> rightSig;
						if (hasRightSig) {
							InstanceSignature sig;
							if (!ReadFullSignature(a_intfc, sig) || !ResolveSignatureFormIDs(a_intfc, sig)) {
								hasRightSig = false;
							} else {
								rightSig = std::move(sig);
							}
						}

						std::uint8_t leftKindRaw{ 0 };
						RE::FormID storedLeftBase{ 0 };
						if (!a_intfc->ReadRecordData(&leftKindRaw, sizeof(leftKindRaw)) ||
							!a_intfc->ReadRecordData(&storedLeftBase, sizeof(storedLeftBase))) {
							logger::warn("Failed to read CEPC left header {}/{}", i + 1, count);
							break;
						}
						bool hasLeftSig = false;
						if (!ReadBool(a_intfc, hasLeftSig)) {
							logger::warn("Failed to read CEPC left signature flag {}/{}", i + 1, count);
							break;
						}
						std::optional<InstanceSignature> leftSig;
						if (hasLeftSig) {
							InstanceSignature sig;
							if (!ReadFullSignature(a_intfc, sig) || !ResolveSignatureFormIDs(a_intfc, sig)) {
								hasLeftSig = false;
							} else {
								leftSig = std::move(sig);
							}
						}

						RE::FormID storedAmmoID{ 0 };
						if (!a_intfc->ReadRecordData(&storedAmmoID, sizeof(storedAmmoID))) {
							logger::warn("Failed to read CEPC ammo {}/{}", i + 1, count);
							break;
						}

						RE::FormID actorID{ 0 };
						if (!a_intfc->ResolveFormID(storedActorID, actorID)) {
							continue;
						}

						RE::FormID rightBase{ 0 };
						if (storedRightBase != 0 && !a_intfc->ResolveFormID(storedRightBase, rightBase)) {
							rightBase = 0;
							rightSig.reset();
						}
						RE::FormID leftBase{ 0 };
						if (storedLeftBase != 0 && !a_intfc->ResolveFormID(storedLeftBase, leftBase)) {
							leftBase = 0;
							leftSig.reset();
						}
						RE::FormID ammoID{ 0 };
						if (storedAmmoID != 0 && !a_intfc->ResolveFormID(storedAmmoID, ammoID)) {
							ammoID = 0;
						}

					PreCombatEquipRestore::SnapshotEntry entry{};
					entry.actorID = actorID;
					entry.right.kind = static_cast<PreCombatEquipRestore::SlotKind>(rightKindRaw);
					entry.right.baseObjectID = rightBase;
					entry.right.signature = std::move(rightSig);
					entry.left.kind = static_cast<PreCombatEquipRestore::SlotKind>(leftKindRaw);
					entry.left.baseObjectID = leftBase;
					entry.left.signature = std::move(leftSig);
					entry.ammoID = ammoID;
					PreCombatEquipRestore::SetLoadedEntry(std::move(entry));
					}
				} else if (type == kRecordHeadgearAutoEquip) {
					for (std::uint32_t i = 0; i < count; i++) {
						RE::FormID storedActorID{ 0 };
						if (!a_intfc->ReadRecordData(&storedActorID, sizeof(storedActorID))) {
							logger::warn("Failed to read CHGT actor ID {}/{}", i + 1, count);
							break;
						}

						std::vector<HeadgearAutoEquip::WornHeadgearItem> items;

						std::uint8_t itemCount = 0;
						if (!a_intfc->ReadRecordData(&itemCount, sizeof(itemCount))) {
							logger::warn("Failed to read CHGT itemCount {}/{}", i + 1, count);
							break;
						}
						for (std::uint8_t j = 0; j < itemCount; ++j) {
							RE::FormID storedBaseID{ 0 };
							if (!a_intfc->ReadRecordData(&storedBaseID, sizeof(storedBaseID))) {
								logger::warn("Failed to read CHGT item baseID {}/{} item {}", i + 1, count, j);
								break;
							}
							bool hasSig = false;
							if (!ReadBool(a_intfc, hasSig)) {
								logger::warn("Failed to read CHGT item sig flag {}/{} item {}", i + 1, count, j);
								break;
							}
							std::optional<InstanceSignature> sig;
							if (hasSig) {
								InstanceSignature tmp;
								if (!ReadFullSignature(a_intfc, tmp) || !ResolveSignatureFormIDs(a_intfc, tmp)) {
									hasSig = false;
								} else {
									sig = std::move(tmp);
								}
							}
							RE::FormID baseID{ 0 };
							if (storedBaseID != 0 && a_intfc->ResolveFormID(storedBaseID, baseID)) {
								HeadgearAutoEquip::WornHeadgearItem item;
								item.baseObjectID = baseID;
								item.signature = std::move(sig);
								items.push_back(std::move(item));
							}
						}

						RE::FormID actorID{ 0 };
						if (!a_intfc->ResolveFormID(storedActorID, actorID)) {
							continue;
						}

						HeadgearAutoEquip::SnapshotEntry entry{};
						entry.actorID = actorID;
						entry.wornHeadgear = std::move(items);
						HeadgearAutoEquip::SetLoadedEntry(std::move(entry));
					}
				} else if (type == kRecordSpellSuppression) {
					for (std::uint32_t i = 0; i < count; i++) {
						RE::FormID storedActorID{ 0 };
						RE::FormID storedSpellID{ 0 };
						if (!a_intfc->ReadRecordData(&storedActorID, sizeof(storedActorID)) ||
							!a_intfc->ReadRecordData(&storedSpellID, sizeof(storedSpellID))) {
							logger::warn("Failed to read SSPL entry {}/{}", i + 1, count);
							break;
						}
						RE::FormID actorID{ 0 };
						if (!a_intfc->ResolveFormID(storedActorID, actorID)) {
							continue;
						}
						RE::FormID spellID{ 0 };
						if (!a_intfc->ResolveFormID(storedSpellID, spellID)) {
							continue;
						}
						SpellSuppressionState::Suppress(actorID, spellID);
					}
				} else if (type == kRecordActorScopeDisable) {
					for (std::uint32_t i = 0; i < count; i++) {
						RE::FormID storedActorID{ 0 };
						if (!a_intfc->ReadRecordData(&storedActorID, sizeof(storedActorID))) {
							logger::warn("Failed to read ACDS entry {}/{}", i + 1, count);
							break;
						}
						RE::FormID actorID{ 0 };
						if (!a_intfc->ResolveFormID(storedActorID, actorID)) {
							continue;
						}
						ActorScopeExcludeState::Exclude(actorID);
					}
				} else {
					for (std::uint32_t i = 0; i < count; i++) {
						RE::FormID storedID{ 0 };
						if (!a_intfc->ReadRecordData(&storedID, sizeof(storedID))) {
							logger::warn("Failed to read record entry {}/{} (type {})", i + 1, count, type);
							break;
						}

						RE::FormID resolvedID{ 0 };
						if (!a_intfc->ResolveFormID(storedID, resolvedID)) {
							continue;
						}

						if (type == kRecordKnownFollowers) {
							KnownFollowerState::AddKnownFollower(resolvedID);
						}
					}
				}
			}
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			KnownFollowerState::Clear();
			CombatEquipPreference::Clear();
			OutfitSnapshotRestore::ClearSnapshots();
			HandItemRestore::ClearSnapshots();
			PreCombatEquipRestore::ClearSnapshots();
			HeadgearAutoEquip::ClearSnapshots();
			CombatEquipOverride::State::Clear();
			EquipGate::Telemetry::Clear();
			SpellSuppressionState::Clear();
			ActorScopeExcludeState::Clear();
		}
	}

	void Serialization::Install()
	{
		if (const auto serialization = SKSE::GetSerializationInterface()) {
			serialization->SetUniqueID(kSerializationPluginID);
			serialization->SetSaveCallback(OnSave);
			serialization->SetLoadCallback(OnLoad);
			serialization->SetRevertCallback(OnRevert);
		}
	}
}
