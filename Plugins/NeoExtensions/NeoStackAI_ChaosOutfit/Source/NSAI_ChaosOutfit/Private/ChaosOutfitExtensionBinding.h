#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIChaosOutfitExtension
{
	const TArray<FLuaFunctionDoc>& GetChaosOutfitLuaDocs();
	void BindChaosOutfitLua(sol::state& Lua, FLuaSessionData& Session);
}
