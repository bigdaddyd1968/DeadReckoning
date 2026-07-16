#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIMetaHumanExtension
{
	const TArray<FLuaFunctionDoc>& GetMetaHumanLuaDocs();
	void BindMetaHumanLua(sol::state& Lua, FLuaSessionData& Session);
}

