#pragma once

#include "Lua/LuaBindingRegistry.h"

namespace sol { class state; }

namespace NSAINiagaraExtension
{
	const TArray<FLuaFunctionDoc>& GetNiagaraLuaDocs();
	void BindNiagaraLua(sol::state& Lua, FLuaSessionData& Session);
}
