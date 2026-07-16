#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAIPoseSearchExtension
{
	const TArray<FLuaFunctionDoc>& GetPoseSearchLuaDocs();
	void BindPoseSearchLua(sol::state& Lua, FLuaSessionData& Session);
}
