#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIControlRigExtension
{
	const TArray<FLuaFunctionDoc>& GetControlRigLuaDocs();
	void BindControlRigLua(sol::state& Lua, FLuaSessionData& Session);
	const TArray<FLuaFunctionDoc>& GetControlRigGraphOpsLuaDocs();
	void BindControlRigGraphOpsLua(sol::state& Lua, FLuaSessionData& Session);
}
