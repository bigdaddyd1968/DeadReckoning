#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIChooserExtension
{
	const TArray<FLuaFunctionDoc>& GetChooserLuaDocs();
	void BindChooserLua(sol::state& Lua, FLuaSessionData& Session);
}
