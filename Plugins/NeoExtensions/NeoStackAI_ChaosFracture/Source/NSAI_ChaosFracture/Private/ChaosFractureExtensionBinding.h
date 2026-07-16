#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIChaosFractureExtension
{
	const TArray<FLuaFunctionDoc>& GetChaosFractureLuaDocs();
	void BindChaosFractureLua(sol::state& Lua, FLuaSessionData& Session);
}
