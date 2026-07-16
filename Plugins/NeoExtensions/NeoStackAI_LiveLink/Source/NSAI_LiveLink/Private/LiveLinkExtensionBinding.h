#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAILiveLinkExtension
{
	const TArray<FLuaFunctionDoc>& GetLiveLinkLuaDocs();
	void BindLiveLinkLua(sol::state& Lua, FLuaSessionData& Session);
}
