// Resolves InstanceSignature values to concrete inventory instances.

#pragma once

#include "PCH.h"

#include "InstanceSignature.h"

#include <cstdint>
#include <optional>
#include <unordered_set>

namespace FEC::SignatureResolve
{
	enum class Policy : std::uint8_t
	{
		kIdentityOnly,
		kIdentityThenAnyBase
	};

	enum class ResolveKind : std::uint8_t
	{
		kNoMatch,
		kMatchedXList,
		kMatchedBaseOnly
	};

	struct ResolveResult
	{
		ResolveKind kind{ ResolveKind::kNoMatch };
		RE::ExtraDataList* xList{ nullptr };  // valid only for kMatchedXList

		[[nodiscard]] bool Matched() const noexcept { return kind != ResolveKind::kNoMatch; }
		[[nodiscard]] bool HasXList() const noexcept { return kind == ResolveKind::kMatchedXList && xList != nullptr; }
	};

	// Always tries strict stable-identity matching first.
	// kIdentityOnly returns kMatchedBaseOnly only when the signature has no stable identity and the entry has no extraLists.
	// kIdentityThenAnyBase can fall back to any base instance; if no extraLists exist, it returns kMatchedBaseOnly.
	[[nodiscard]] ResolveResult Resolve(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		const InstanceSignature& a_signature,
		std::optional<InstanceSignature::EquipState> a_desiredState = std::nullopt,
		Policy a_policy = Policy::kIdentityThenAnyBase);

	// Excludes already-claimed xLists so repeated base-object resolutions do not collide.
	[[nodiscard]] ResolveResult Resolve(
		RE::Actor* a_actor,
		RE::TESBoundObject* a_object,
		const InstanceSignature& a_signature,
		std::optional<InstanceSignature::EquipState> a_desiredState,
		Policy a_policy,
		const std::unordered_set<RE::ExtraDataList*>& a_excludeSet);
}
