#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIEnhancedInputExtension
{
	const TArray<FLuaFunctionDoc>& GetEnhancedInputLuaDocs();
	void BindEnhancedInputLua(sol::state& Lua, FLuaSessionData& Session);
}
