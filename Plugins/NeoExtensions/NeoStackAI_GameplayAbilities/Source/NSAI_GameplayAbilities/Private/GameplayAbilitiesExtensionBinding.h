#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIGameplayAbilitiesExtension
{
	const TArray<FLuaFunctionDoc>& GetGameplayAbilityLuaDocs();
	void BindGameplayAbilityLua(sol::state& Lua, FLuaSessionData& Session);
	const TArray<FLuaFunctionDoc>& GetGameplayEffectLuaDocs();
	void BindGameplayEffectLua(sol::state& Lua, FLuaSessionData& Session);
}
