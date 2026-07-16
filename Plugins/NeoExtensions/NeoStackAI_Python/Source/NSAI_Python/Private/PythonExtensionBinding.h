#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIPythonExtension
{
	const TArray<FLuaFunctionDoc>& GetPythonLuaDocs();
	void BindPythonLua(sol::state& Lua, FLuaSessionData& Session);
}
