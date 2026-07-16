#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIWaterExtension
{
	const TArray<FLuaFunctionDoc>& GetWaterLuaDocs();
	void BindWaterLua(sol::state& Lua, FLuaSessionData& Session);
}
