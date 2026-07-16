#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIIKRigExtension
{
	const TArray<FLuaFunctionDoc>& GetIKRigLuaDocs();
	void BindIKRigLua(sol::state& Lua, FLuaSessionData& Session);
	const TArray<FLuaFunctionDoc>& GetIKRetargeterLuaDocs();
	void BindIKRetargeterLua(sol::state& Lua, FLuaSessionData& Session);
}
