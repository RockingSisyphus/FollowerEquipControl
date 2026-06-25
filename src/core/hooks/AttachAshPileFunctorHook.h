// Detours hook for AttachAshPileFunctor::operator() used to purge per-actor state after ash-pile attachment.

#pragma once

#include "PCH.h"

namespace FEC
{
	class AttachAshPileFunctorHook
	{
	public:
		static void Install();
		static void Uninstall();
	};
}
