// Copyright 2026 Betide Studio. All Rights Reserved.

#include "NiagaraStructHandlers.h"
#include "Lua/LuaPropertyTable.h"
#include "Lua/LuaEnumReflection.h"

#include "NiagaraTypes.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "NiagaraTypeRegistry.h"
#endif
#include "NiagaraCommon.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "NiagaraEmitterBase.h"
#endif
#include "NiagaraRendererProperties.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceCurveBase.h"
#include "Curves/RichCurve.h"
#include "UObject/UnrealType.h"

namespace NSAINiagaraLCD
{

namespace
{

bool ParseBoolLikeString(const FString& Text, bool& OutValue)
{
	const FString Normalized = Text.TrimStartAndEnd().ToLower();
	if (Normalized == TEXT("true") || Normalized == TEXT("1") || Normalized == TEXT("yes") || Normalized == TEXT("on"))
	{
		OutValue = true;
		return true;
	}
	if (Normalized == TEXT("false") || Normalized == TEXT("0") || Normalized == TEXT("no") || Normalized == TEXT("off"))
	{
		OutValue = false;
		return true;
	}
	return false;
}

// ── FNiagaraBool: int32 Value with True=-1, False=0 ─────────────────────

bool WriteNiagaraBool(UScriptStruct*, void* Mem, const sol::object& Val, FString& OutError)
{
	FNiagaraBool* Out = static_cast<FNiagaraBool*>(Mem);
	if (Val.is<bool>())
	{
		Out->SetValue(Val.as<bool>());
		return true;
	}
	if (Val.is<double>())
	{
		Out->SetValue(Val.as<double>() != 0.0);
		return true;
	}
	if (Val.is<std::string>())
	{
		bool Parsed = false;
		if (ParseBoolLikeString(UTF8_TO_TCHAR(Val.as<std::string>().c_str()), Parsed))
		{
			Out->SetValue(Parsed);
			return true;
		}
	}
	if (Val.is<sol::table>())
	{
		// Accept {Value=...} for round-trip compatibility
		sol::table T = Val.as<sol::table>();
		sol::optional<bool> B = T.get<sol::optional<bool>>("Value");
		if (!B) B = T.get<sol::optional<bool>>("value");
		if (B.has_value()) { Out->SetValue(B.value()); return true; }

		sol::optional<std::string> S = T.get<sol::optional<std::string>>("Value");
		if (!S) S = T.get<sol::optional<std::string>>("value");
		if (S.has_value())
		{
			bool Parsed = false;
			if (ParseBoolLikeString(UTF8_TO_TCHAR(S.value().c_str()), Parsed))
			{
				Out->SetValue(Parsed);
				return true;
			}
		}
	}
	OutError = TEXT("expected bool for FNiagaraBool");
	return false;
}

sol::object ReadNiagaraBool(UScriptStruct*, const void* Mem, sol::state_view Lua)
{
	const FNiagaraBool* B = static_cast<const FNiagaraBool*>(Mem);
	return sol::make_object(Lua, B->GetValue());
}

// ── FNiagaraFloat: wraps float Value (backing struct for the Float type) ─

bool WriteNiagaraFloat(UScriptStruct*, void* Mem, const sol::object& Val, FString& OutError)
{
	FNiagaraFloat* Out = static_cast<FNiagaraFloat*>(Mem);
	if (Val.is<double>()) { Out->Value = static_cast<float>(Val.as<double>()); return true; }
	if (Val.is<bool>())   { Out->Value = Val.as<bool>() ? 1.0f : 0.0f;          return true; }
	if (Val.is<sol::table>())
	{
		sol::table T = Val.as<sol::table>();
		sol::optional<double> V = T.get<sol::optional<double>>("Value");
		if (!V) V = T.get<sol::optional<double>>("value");
		if (V.has_value()) { Out->Value = static_cast<float>(V.value()); return true; }
	}
	OutError = TEXT("expected number for FNiagaraFloat");
	return false;
}

sol::object ReadNiagaraFloat(UScriptStruct*, const void* Mem, sol::state_view Lua)
{
	const FNiagaraFloat* F = static_cast<const FNiagaraFloat*>(Mem);
	return sol::make_object(Lua, static_cast<double>(F->Value));
}

// ── FNiagaraInt32: wraps int32 Value (straight pass-through) ────────────

bool WriteNiagaraInt32(UScriptStruct*, void* Mem, const sol::object& Val, FString& OutError)
{
	FNiagaraInt32* Out = static_cast<FNiagaraInt32*>(Mem);
	if (Val.is<double>())    { Out->Value = static_cast<int32>(Val.as<double>()); return true; }
	if (Val.is<bool>())      { Out->Value = Val.as<bool>() ? 1 : 0;               return true; }
	if (Val.is<sol::table>())
	{
		sol::table T = Val.as<sol::table>();
		sol::optional<double> V = T.get<sol::optional<double>>("Value");
		if (!V) V = T.get<sol::optional<double>>("value");
		if (V.has_value()) { Out->Value = static_cast<int32>(V.value()); return true; }
	}
	OutError = TEXT("expected number for FNiagaraInt32");
	return false;
}

sol::object ReadNiagaraInt32(UScriptStruct*, const void* Mem, sol::state_view Lua)
{
	const FNiagaraInt32* I = static_cast<const FNiagaraInt32*>(Mem);
	return sol::make_object(Lua, static_cast<double>(I->Value));
}

// ── FNiagaraPosition: inherits FVector3f → {x,y,z} ──────────────────────

bool WriteNiagaraPosition(UScriptStruct*, void* Mem, const sol::object& Val, FString& OutError)
{
	if (!Val.is<sol::table>()) { OutError = TEXT("expected table {x,y,z} for FNiagaraPosition"); return false; }
	sol::table T = Val.as<sol::table>();
	FNiagaraPosition* Out = static_cast<FNiagaraPosition*>(Mem);
	Out->X = static_cast<float>(T.get_or("x", T.get_or("X", 0.0)));
	Out->Y = static_cast<float>(T.get_or("y", T.get_or("Y", 0.0)));
	Out->Z = static_cast<float>(T.get_or("z", T.get_or("Z", 0.0)));
	return true;
}

sol::object ReadNiagaraPosition(UScriptStruct*, const void* Mem, sol::state_view Lua)
{
	const FNiagaraPosition* P = static_cast<const FNiagaraPosition*>(Mem);
	sol::table T = Lua.create_table();
	T["x"] = static_cast<double>(P->X);
	T["y"] = static_cast<double>(P->Y);
	T["z"] = static_cast<double>(P->Z);
	return sol::make_object(Lua, T);
}

// ── FNiagaraVariableBase: {name, type} ──────────────────────────────────
//
// Private UPROPERTY Name + TypeDefHandle — written via whole-struct replacement
// using the public (FTypeDef, FName) constructor. Type is optional on write; when
// omitted, existing type is preserved.

bool WriteNiagaraVariableBase(UScriptStruct*, void* Mem, const sol::object& Val, FString& OutError)
{
	FNiagaraVariableBase* Out = static_cast<FNiagaraVariableBase*>(Mem);

	auto ApplyNameAndType = [&](const FString& InName, const FString& InType) -> bool
	{
		FNiagaraTypeDefinition TypeDef = Out->GetType();
		if (!InType.IsEmpty())
		{
			FNiagaraTypeDefinition Resolved = ResolveTypeDefinition(InType);
			if (!Resolved.IsValid())
			{
				OutError = FString::Printf(TEXT("unknown Niagara type '%s'"), *InType);
				return false;
			}
			TypeDef = Resolved;
		}
		*Out = FNiagaraVariableBase(TypeDef, FName(*InName));
		return true;
	};

	if (Val.is<std::string>())
	{
		return ApplyNameAndType(UTF8_TO_TCHAR(Val.as<std::string>().c_str()), FString());
	}
	if (Val.is<sol::table>())
	{
		sol::table T = Val.as<sol::table>();
		// Note: sol2 get_or<std::string> is ambiguous when nested because the inner
		// call's std::string return type collides with both get_or<T>(Key, T&&) and
		// get_or<T,D>(Key, D&&) overloads. Walk aliases with plain fallbacks instead.
		std::string Name = T.get_or<std::string>("name", "");
		if (Name.empty()) Name = T.get_or<std::string>("Name", "");
		std::string Type = T.get_or<std::string>("type", "");
		if (Type.empty()) Type = T.get_or<std::string>("Type", "");
		if (Type.empty()) Type = T.get_or<std::string>("type_name", "");
		if (Name.empty()) { OutError = TEXT("FNiagaraVariableBase: 'name' required"); return false; }
		return ApplyNameAndType(UTF8_TO_TCHAR(Name.c_str()), UTF8_TO_TCHAR(Type.c_str()));
	}
	OutError = TEXT("expected string or {name,type} for FNiagaraVariableBase");
	return false;
}

sol::object ReadNiagaraVariableBase(UScriptStruct*, const void* Mem, sol::state_view Lua)
{
	const FNiagaraVariableBase* V = static_cast<const FNiagaraVariableBase*>(Mem);
	sol::table T = Lua.create_table();
	T["name"] = TCHAR_TO_UTF8(*V->GetName().ToString());
	T["type"] = TCHAR_TO_UTF8(*V->GetType().GetName());
	return sol::make_object(Lua, T);
}

// ── FNiagaraVariableAttributeBinding: renderer attribute binding ────────
//
// RootName is protected — we write it via FProperty reflection so the struct
// handler stays pure-memory. The configure("renderer") caller flushes caches
// afterwards via NSAINiagaraLCD::RefreshRendererBindings which invokes the
// engine's own SetValue() with the right emitter context.

bool WriteAttributeBinding(UScriptStruct* Struct, void* Mem, const sol::object& Val, FString& OutError)
{
	auto SetRootName = [&](const FString& NewRoot) -> bool
	{
		if (FNameProperty* P = CastField<FNameProperty>(Struct->FindPropertyByName(TEXT("RootName"))))
		{
			P->SetPropertyValue_InContainer(Mem, FName(*NewRoot));
			return true;
		}
		OutError = TEXT("FNiagaraVariableAttributeBinding: RootName property not found");
		return false;
	};

	if (Val.is<std::string>())
	{
		return SetRootName(UTF8_TO_TCHAR(Val.as<std::string>().c_str()));
	}
	if (Val.is<sol::table>())
	{
		sol::table T = Val.as<sol::table>();
		std::string Name = T.get_or<std::string>("name", "");
		if (Name.empty()) Name = T.get_or<std::string>("root", "");
		if (Name.empty()) Name = T.get_or<std::string>("root_name", "");
		if (Name.empty())
		{
			OutError = TEXT("FNiagaraVariableAttributeBinding: 'name' required");
			return false;
		}
		return SetRootName(UTF8_TO_TCHAR(Name.c_str()));
	}
	OutError = TEXT("expected string or {name='...'} for FNiagaraVariableAttributeBinding");
	return false;
}

sol::object ReadAttributeBinding(UScriptStruct* Struct, const void* Mem, sol::state_view Lua)
{
	const FNiagaraVariableAttributeBinding* B = static_cast<const FNiagaraVariableAttributeBinding*>(Mem);
	sol::table T = Lua.create_table();

	// Public inline accessors.
	T["name"] = TCHAR_TO_UTF8(*B->GetParamMapBindableVariable().GetName().ToString());
	T["type"] = TCHAR_TO_UTF8(*B->GetType().GetName());
	T["is_particle"] = B->IsParticleBinding();
	T["exists_on_source"] = B->DoesBindingExistOnSource();

	// RootName / BindingSourceMode via reflection since private.
	if (FNameProperty* PN = CastField<FNameProperty>(Struct->FindPropertyByName(TEXT("RootName"))))
	{
		T["root"] = TCHAR_TO_UTF8(*PN->GetPropertyValue_InContainer(Mem).ToString());
	}
	if (FProperty* PS = Struct->FindPropertyByName(TEXT("BindingSourceMode")))
	{
		FString Txt;
		PS->ExportTextItem_Direct(Txt,
			PS->ContainerPtrToValuePtr<void>(Mem), nullptr, nullptr, PPF_None);
		T["source_mode"] = TCHAR_TO_UTF8(*Txt);
	}
	return sol::make_object(Lua, T);
}

// ── FNiagaraMaterialAttributeBinding: material parameter binding ────────
//
// All fields are public EditAnywhere UPROPERTY:
//   - MaterialParameterName: FName
//   - NiagaraVariable, NiagaraChildVariable: FNiagaraVariableBase
//   - ResolvedNiagaraVariable: FNiagaraVariableBase (runtime-cached, not authored)
//
// The LCD default struct writer could handle this, but registering a dedicated
// handler lets us take a shorthand form like { parameter_name = "...",
// variable = "Particles.Color" } without requiring the caller to spell out
// the nested {name,type} table every time.

bool WriteMaterialAttributeBinding(UScriptStruct*, void* Mem, const sol::object& Val, FString& OutError)
{
	if (!Val.is<sol::table>())
	{
		OutError = TEXT("expected table for FNiagaraMaterialAttributeBinding");
		return false;
	}
	FNiagaraMaterialAttributeBinding* B = static_cast<FNiagaraMaterialAttributeBinding*>(Mem);
	sol::table T = Val.as<sol::table>();

	// Accept either "parameter_name" (friendly) or "MaterialParameterName" (UPROPERTY name).
	// Flat fallbacks — nested get_or<std::string> is ambiguous in sol2 v3.3.0.
	std::string ParamName = T.get_or<std::string>("parameter_name", "");
	if (ParamName.empty()) ParamName = T.get_or<std::string>("MaterialParameterName", "");
	if (!ParamName.empty())
	{
		B->MaterialParameterName = FName(UTF8_TO_TCHAR(ParamName.c_str()));
	}

	auto ApplyVariable = [&OutError](FNiagaraVariableBase& OutVar, const sol::object& VarObj) -> bool
	{
		if (!VarObj.valid() || VarObj.is<sol::lua_nil_t>()) return true;

		if (VarObj.is<std::string>())
		{
			OutVar = FNiagaraVariableBase(OutVar.GetType(),
				FName(UTF8_TO_TCHAR(VarObj.as<std::string>().c_str())));
			return true;
		}
		if (VarObj.is<sol::table>())
		{
			sol::table VT = VarObj.as<sol::table>();
			std::string N = VT.get_or<std::string>("name", "");
			if (N.empty()) N = VT.get_or<std::string>("Name", "");
			std::string Y = VT.get_or<std::string>("type", "");
			if (Y.empty()) Y = VT.get_or<std::string>("Type", "");
			if (N.empty()) { OutError = TEXT("variable: 'name' required"); return false; }
			FNiagaraTypeDefinition TypeDef = OutVar.GetType();
			if (!Y.empty())
			{
				FNiagaraTypeDefinition Resolved = ResolveTypeDefinition(UTF8_TO_TCHAR(Y.c_str()));
				if (!Resolved.IsValid()) { OutError = FString::Printf(TEXT("unknown type '%s'"), UTF8_TO_TCHAR(Y.c_str())); return false; }
				TypeDef = Resolved;
			}
			OutVar = FNiagaraVariableBase(TypeDef, FName(UTF8_TO_TCHAR(N.c_str())));
			return true;
		}
		OutError = TEXT("variable: expected string or {name,type}");
		return false;
	};

	// Accept "variable" (friendly) or "NiagaraVariable" (UPROPERTY).
	sol::object VarObj = T["variable"];
	if (!VarObj.valid() || VarObj.is<sol::lua_nil_t>()) VarObj = T["NiagaraVariable"];
	if (!ApplyVariable(B->NiagaraVariable, VarObj)) return false;

	sol::object ChildObj = T["child_variable"];
	if (!ChildObj.valid() || ChildObj.is<sol::lua_nil_t>()) ChildObj = T["NiagaraChildVariable"];
	if (!ApplyVariable(B->NiagaraChildVariable, ChildObj)) return false;

	return true;
}

sol::object ReadMaterialAttributeBinding(UScriptStruct*, const void* Mem, sol::state_view Lua)
{
	const FNiagaraMaterialAttributeBinding* B = static_cast<const FNiagaraMaterialAttributeBinding*>(Mem);
	sol::table T = Lua.create_table();
	T["parameter_name"] = TCHAR_TO_UTF8(*B->MaterialParameterName.ToString());

	sol::table Var = Lua.create_table();
	Var["name"] = TCHAR_TO_UTF8(*B->NiagaraVariable.GetName().ToString());
	Var["type"] = TCHAR_TO_UTF8(*B->NiagaraVariable.GetType().GetName());
	T["variable"] = Var;

	if (!B->NiagaraChildVariable.GetName().IsNone())
	{
		sol::table Child = Lua.create_table();
		Child["name"] = TCHAR_TO_UTF8(*B->NiagaraChildVariable.GetName().ToString());
		Child["type"] = TCHAR_TO_UTF8(*B->NiagaraChildVariable.GetType().GetName());
		T["child_variable"] = Child;
	}
	return sol::make_object(Lua, T);
}

} // namespace

// ── Registration ────────────────────────────────────────────────────────

void RegisterNiagaraStructHandlers()
{
	NeoLuaProperty::RegisterStructHandler(
		FNiagaraFloat::StaticStruct(),
		&WriteNiagaraFloat, &ReadNiagaraFloat);

	NeoLuaProperty::RegisterStructHandler(
		FNiagaraBool::StaticStruct(),
		&WriteNiagaraBool, &ReadNiagaraBool);

	NeoLuaProperty::RegisterStructHandler(
		FNiagaraInt32::StaticStruct(),
		&WriteNiagaraInt32, &ReadNiagaraInt32);

	NeoLuaProperty::RegisterStructHandler(
		FNiagaraPosition::StaticStruct(),
		&WriteNiagaraPosition, &ReadNiagaraPosition);

	NeoLuaProperty::RegisterStructHandler(
		FNiagaraVariableBase::StaticStruct(),
		&WriteNiagaraVariableBase, &ReadNiagaraVariableBase);

	NeoLuaProperty::RegisterStructHandler(
		FNiagaraVariableAttributeBinding::StaticStruct(),
		&WriteAttributeBinding, &ReadAttributeBinding);

	NeoLuaProperty::RegisterStructHandler(
		FNiagaraMaterialAttributeBinding::StaticStruct(),
		&WriteMaterialAttributeBinding, &ReadMaterialAttributeBinding);
}

// ── Type resolution ─────────────────────────────────────────────────────

FNiagaraTypeDefinition ResolveTypeDefinition(const FString& TypeName)
{
	if (TypeName.IsEmpty()) return FNiagaraTypeDefinition();

	// Common aliases first — resolve to the engine's built-in types directly
	const FString Lower = TypeName.ToLower();
	if (Lower == TEXT("float"))                                       return FNiagaraTypeDefinition::GetFloatDef();
	if (Lower == TEXT("int") || Lower == TEXT("int32") || Lower == TEXT("integer")) return FNiagaraTypeDefinition::GetIntDef();
	if (Lower == TEXT("bool") || Lower == TEXT("boolean"))            return FNiagaraTypeDefinition::GetBoolDef();
	if (Lower == TEXT("vec2") || Lower == TEXT("vector2") || Lower == TEXT("vector2d")) return FNiagaraTypeDefinition::GetVec2Def();
	if (Lower == TEXT("vec3") || Lower == TEXT("vector") || Lower == TEXT("vector3"))   return FNiagaraTypeDefinition::GetVec3Def();
	if (Lower == TEXT("vec4") || Lower == TEXT("vector4"))            return FNiagaraTypeDefinition::GetVec4Def();
	if (Lower == TEXT("color") || Lower == TEXT("linearcolor"))       return FNiagaraTypeDefinition::GetColorDef();
	if (Lower == TEXT("quat") || Lower == TEXT("quaternion"))         return FNiagaraTypeDefinition::GetQuatDef();
	if (Lower == TEXT("position"))                                    return FNiagaraTypeDefinition::GetPositionDef();
	if (Lower == TEXT("matrix") || Lower == TEXT("matrix4"))          return FNiagaraTypeDefinition::GetMatrix4Def();
	if (Lower == TEXT("id") || Lower == TEXT("niagaraid"))            return FNiagaraTypeDefinition::GetIDDef();

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	// Direct name lookup through the engine's type registry (NIAGARA_API-exported).
	if (TOptional<FNiagaraTypeDefinition> Direct = FNiagaraTypeRegistry::GetRegisteredTypeByName(FName(*TypeName)))
	{
		return Direct.GetValue();
	}

	// Fallback: walk the full parameter-type list and match by friendly name
	// or underlying struct/class name (case-insensitive).
	const TArray<FNiagaraTypeDefinition> AllTypes = FNiagaraTypeRegistry::GetRegisteredParameterTypes();
	for (const FNiagaraTypeDefinition& Type : AllTypes)
	{
		if (Type.GetName().Equals(TypeName, ESearchCase::IgnoreCase))
		{
			return Type;
		}
	}
	for (const FNiagaraTypeDefinition& Type : AllTypes)
	{
		if (UClass* Cls = Type.GetClass())
		{
			if (Cls->GetName().Equals(TypeName, ESearchCase::IgnoreCase)) return Type;
		}
		if (UScriptStruct* S = Type.GetScriptStruct())
		{
			if (S->GetName().Equals(TypeName, ESearchCase::IgnoreCase)) return Type;
		}
	}
#endif

	return FNiagaraTypeDefinition();
}

TArray<FString> GetAllowedUserParameterTypeNames()
{
	TArray<FString> Names;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	const TArray<FNiagaraTypeDefinition> Allowed = FNiagaraTypeRegistry::GetRegisteredUserVariableTypes();
	Names.Reserve(Allowed.Num());
	for (const FNiagaraTypeDefinition& Type : Allowed)
	{
		Names.Add(Type.GetName());
	}
	Names.Sort();
#endif
	return Names;
}

// ── Byte-level typed value I/O ──────────────────────────────────────────

bool SerializeSolValueToTypedBytes(const sol::object& Value,
	const FNiagaraTypeDefinition& TypeDef,
	TArray<uint8>& OutBytes,
	FString& OutError)
{
	if (!TypeDef.IsValid())
	{
		OutError = TEXT("invalid Niagara type definition");
		return false;
	}

	const int32 ByteSize = TypeDef.GetSize();
	if (ByteSize <= 0)
	{
		OutError = FString::Printf(TEXT("type '%s' has no byte size"), *TypeDef.GetName());
		return false;
	}

	OutBytes.SetNumZeroed(ByteSize);

	// Accept {value=...} / {Value=...} as an explicit scalar wrapper. This keeps
	// static-switch enum and bool authoring consistent with the advanced-mode
	// table shape without forcing callers to know the underlying FNiagaraInt32
	// or FNiagaraBool storage.
	if (Value.is<sol::table>())
	{
		sol::table T = Value.as<sol::table>();
		sol::object Wrapped = T.get<sol::object>("value");
		if (Wrapped.valid() && !Wrapped.is<sol::lua_nil_t>())
		{
			return SerializeSolValueToTypedBytes(Wrapped, TypeDef, OutBytes, OutError);
		}

		Wrapped = T.get<sol::object>("Value");
		if (Wrapped.valid() && !Wrapped.is<sol::lua_nil_t>())
		{
			return SerializeSolValueToTypedBytes(Wrapped, TypeDef, OutBytes, OutError);
		}
	}

	// Enum-typed values (FNiagaraInt32 backed by a UEnum — every static-switch pin like
	// "Orientation Method", "Lifetime Mode", "Color Mode", etc.) — accept the enum's
	// authored name as a string and resolve through the same NeoLuaEnum::ParseRuntime path
	// that FEnumProperty / FByteProperty use. Without this, agents have to know the
	// underlying int index of every enum value, which the binding doesn't surface.
	// Numeric values still work (fall through to the struct handler below).
	//
	// FNiagaraTypeDefinition::IsEnum() is the authoritative check; the underlying script
	// struct is FNiagaraInt32 in every case (NiagaraTypes.h:892), so we write the int32
	// directly into the byte buffer.
	if (TypeDef.IsEnum() && Value.is<std::string>())
	{
		UEnum* Enum = TypeDef.GetEnum();
		if (!Enum)
		{
			OutError = FString::Printf(TEXT("Niagara enum type '%s' has no UEnum"), *TypeDef.GetName());
			return false;
		}
		const FString TextValue = UTF8_TO_TCHAR(Value.as<std::string>().c_str());
		int64 ResolvedValue = INDEX_NONE;
		if (!NeoLuaEnum::ParseRuntime(Enum, TextValue, ResolvedValue))
		{
			// Build a short list of valid names for the error message — caps at 8 to keep
			// log lines readable. The full list is dynamic from the UEnum, so new values
			// added to the engine surface automatically.
			TArray<FString> Names;
			const int32 NumEntries = Enum->NumEnums() - 1; // last is _MAX, skip
			int32 VisibleEntries = 0;
			for (int32 i = 0; i < NumEntries; ++i)
			{
				if (!Enum->HasMetaData(TEXT("Hidden"), i))
				{
					VisibleEntries++;
					if (Names.Num() < 8)
					{
						const FText DisplayName = Enum->GetDisplayNameTextByIndex(i);
						const FString DisplayString = DisplayName.ToString();
						Names.Add(DisplayString.IsEmpty() ? Enum->GetNameStringByIndex(i) : DisplayString);
					}
				}
			}
			OutError = FString::Printf(TEXT("invalid enum value '%s' for '%s'. Try: %s%s"),
				*TextValue, *TypeDef.GetName(),
				*FString::Join(Names, TEXT(", ")),
				VisibleEntries > Names.Num() ? TEXT(", ...") : TEXT(""));
			return false;
		}
		// FNiagaraInt32 layout = single int32 at offset 0; same byte size (4) the type reports.
		check(ByteSize >= sizeof(int32));
		*reinterpret_cast<int32*>(OutBytes.GetData()) = static_cast<int32>(ResolvedValue);
		return true;
	}

	UScriptStruct* Struct = TypeDef.GetScriptStruct();
	if (Struct)
	{
		Struct->InitializeStruct(OutBytes.GetData());
		return NeoLuaProperty::ApplyValueToStructMemory(Struct, OutBytes.GetData(),
			Value, /*OwnerForPostEdit*/nullptr, OutError);
	}

	// No script struct (can happen for data interface types during write — caller should
	// not have dispatched here for those; guard anyway)
	OutError = FString::Printf(TEXT("type '%s' has no reflectable struct — use type-specific path"),
		*TypeDef.GetName());
	return false;
}

sol::object ReadTypedBytesAsSolValue(const uint8* Bytes,
	const FNiagaraTypeDefinition& TypeDef,
	sol::state_view Lua)
{
	if (!Bytes || !TypeDef.IsValid()) return sol::make_object(Lua, sol::lua_nil);

	// Symmetric to the write side: enum-typed FNiagaraInt32 reads back as the authored
	// name string (e.g. "Rotation Rate") rather than the underlying int (e.g. 1). This
	// makes set→get round-trip lossless for static-switch pins. Numeric round-trip is
	// preserved for callers that explicitly want the int — they can `tonumber(name)` or
	// query the UEnum directly.
	if (TypeDef.IsEnum())
	{
		if (UEnum* Enum = TypeDef.GetEnum())
		{
			const int32 IntValue = *reinterpret_cast<const int32*>(Bytes);
			const FString Name = Enum->GetNameStringByValue(IntValue);
			if (!Name.IsEmpty())
			{
				return sol::make_object(Lua, std::string(TCHAR_TO_UTF8(*Name)));
			}
			// Unknown int — fall back to the number so the caller still sees the raw value
			return sol::make_object(Lua, static_cast<double>(IntValue));
		}
	}

	UScriptStruct* Struct = TypeDef.GetScriptStruct();
	if (!Struct) return sol::make_object(Lua, sol::lua_nil);

	return NeoLuaProperty::ReadStructMemoryAsSol(Struct, Bytes, Lua);
}

// ── Curve DI populator ─────────────────────────────────────────────────

namespace
{

// Derive the value extractor for a named FRichCurve field.
// Returns a lambda that pulls a float from each Lua key entry.
TFunction<float(const sol::table&)> CurveExtractorForField(const FString& FieldName)
{
	const FString Lower = FieldName.ToLower();

	// Scalar curve (UNiagaraDataInterfaceCurve::Curve)
	if (Lower == TEXT("curve"))
	{
		return [](const sol::table& K) { return static_cast<float>(K.get_or("value", 0.0)); };
	}

	// Vector components: XCurve/YCurve/ZCurve/WCurve
	// (used by VectorCurve, Vector2DCurve, Vector4Curve)
	if (Lower.StartsWith(TEXT("xcurve")))
	{
		return [](const sol::table& K) { return static_cast<float>(K.get_or("x", K.get_or("X", 0.0))); };
	}
	if (Lower.StartsWith(TEXT("ycurve")))
	{
		return [](const sol::table& K) { return static_cast<float>(K.get_or("y", K.get_or("Y", 0.0))); };
	}
	if (Lower.StartsWith(TEXT("zcurve")))
	{
		return [](const sol::table& K) { return static_cast<float>(K.get_or("z", K.get_or("Z", 0.0))); };
	}
	if (Lower.StartsWith(TEXT("wcurve")))
	{
		return [](const sol::table& K) { return static_cast<float>(K.get_or("w", K.get_or("W", 0.0))); };
	}

	// Color channel curves (UNiagaraDataInterfaceColorCurve)
	// Accept both {color={r,g,b,a}} nested form and {r,g,b,a} flat form.
	auto ColorComp = [](const sol::table& K, const char* Lo, const char* Hi, double Default) -> float
	{
		if (sol::optional<sol::table> C = K.get<sol::optional<sol::table>>("color"))
		{
			return static_cast<float>(C.value().get_or(Lo, C.value().get_or(Hi, Default)));
		}
		return static_cast<float>(K.get_or(Lo, K.get_or(Hi, Default)));
	};

	if (Lower.StartsWith(TEXT("redcurve")) || Lower == TEXT("red"))
		return [ColorComp](const sol::table& K) { return ColorComp(K, "r", "R", 0.0); };
	if (Lower.StartsWith(TEXT("greencurve")) || Lower == TEXT("green"))
		return [ColorComp](const sol::table& K) { return ColorComp(K, "g", "G", 0.0); };
	if (Lower.StartsWith(TEXT("bluecurve")) || Lower == TEXT("blue"))
		return [ColorComp](const sol::table& K) { return ColorComp(K, "b", "B", 0.0); };
	if (Lower.StartsWith(TEXT("alphacurve")) || Lower == TEXT("alpha"))
		return [ColorComp](const sol::table& K) { return ColorComp(K, "a", "A", 1.0); };

	// Unknown field: zero-fill so caller sees a flat curve rather than silent corruption.
	return [](const sol::table&) { return 0.0f; };
}

} // namespace

bool ApplyKeysToCurveDI(UNiagaraDataInterface* DI, const sol::table& Keys, FString& OutError)
{
	if (!DI) { OutError = TEXT("null data interface"); return false; }

	UScriptStruct* RichCurveStruct = FRichCurve::StaticStruct();

	int32 CurveCount = 0;
	for (TFieldIterator<FStructProperty> It(DI->GetClass()); It; ++It)
	{
		FStructProperty* Prop = *It;
		if (!Prop || Prop->Struct != RichCurveStruct) continue;

		FRichCurve* CurvePtr = Prop->ContainerPtrToValuePtr<FRichCurve>(DI);
		if (!CurvePtr) continue;

		TFunction<float(const sol::table&)> Extractor = CurveExtractorForField(Prop->GetName());

		FString KeyErr;
		if (!NeoLuaProperty::ApplyKeysToCurve(*CurvePtr, Keys,
			[&Extractor](const sol::table& K) { return Extractor(K); },
			KeyErr))
		{
			OutError = FString::Printf(TEXT("field '%s': %s"), *Prop->GetName(), *KeyErr);
			return false;
		}
		++CurveCount;
	}

	if (CurveCount == 0)
	{
		OutError = FString::Printf(TEXT("no FRichCurve fields found on '%s'"),
			*DI->GetClass()->GetName());
		return false;
	}

	// Refresh time ranges / sample caches (virtual on UNiagaraDataInterfaceCurveBase).
	if (UNiagaraDataInterfaceCurveBase* CurveBase = Cast<UNiagaraDataInterfaceCurveBase>(DI))
	{
		CurveBase->UpdateTimeRanges();
	}
	return true;
}

// ── Renderer binding refresh ────────────────────────────────────────────

namespace
{

// Walk an arbitrary struct value (recursing into TArray<FStruct> elements and
// direct FStructProperty fields) and invoke visitors when we find a binding
// struct. RootStructAddr is the address of the struct memory; RootStruct is its
// USTRUCT definition.
void WalkStructForBindings(UScriptStruct* RootStruct, void* RootAddr,
	TFunctionRef<void(FNiagaraVariableAttributeBinding&)> OnAttr,
	TFunctionRef<void(FNiagaraMaterialAttributeBinding&)> OnMat);

void WalkPropertyForBindings(FProperty* Prop, void* Container,
	TFunctionRef<void(FNiagaraVariableAttributeBinding&)> OnAttr,
	TFunctionRef<void(FNiagaraMaterialAttributeBinding&)> OnMat)
{
	if (!Prop) return;

	UScriptStruct* const AttrStruct = FNiagaraVariableAttributeBinding::StaticStruct();
	UScriptStruct* const MatStruct  = FNiagaraMaterialAttributeBinding::StaticStruct();

	if (FStructProperty* SP = CastField<FStructProperty>(Prop))
	{
		void* StructAddr = SP->ContainerPtrToValuePtr<void>(Container);
		UScriptStruct* Struct = SP->Struct;

		if (Struct == AttrStruct)
		{
			OnAttr(*static_cast<FNiagaraVariableAttributeBinding*>(StructAddr));
			return;
		}
		if (Struct == MatStruct)
		{
			OnMat(*static_cast<FNiagaraMaterialAttributeBinding*>(StructAddr));
			return;
		}

		// Recurse into nested structs (MaterialParameters, etc.)
		WalkStructForBindings(Struct, StructAddr, OnAttr, OnMat);
		return;
	}

	if (FArrayProperty* AP = CastField<FArrayProperty>(Prop))
	{
		FScriptArrayHelper Helper(AP, AP->ContainerPtrToValuePtr<void>(Container));
		FStructProperty* InnerSP = CastField<FStructProperty>(AP->Inner);
		if (!InnerSP) return;

		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			void* Elem = Helper.GetRawPtr(i);
			if (InnerSP->Struct == AttrStruct)
			{
				OnAttr(*static_cast<FNiagaraVariableAttributeBinding*>(Elem));
			}
			else if (InnerSP->Struct == MatStruct)
			{
				OnMat(*static_cast<FNiagaraMaterialAttributeBinding*>(Elem));
			}
			else
			{
				WalkStructForBindings(InnerSP->Struct, Elem, OnAttr, OnMat);
			}
		}
	}
}

void WalkStructForBindings(UScriptStruct* RootStruct, void* RootAddr,
	TFunctionRef<void(FNiagaraVariableAttributeBinding&)> OnAttr,
	TFunctionRef<void(FNiagaraMaterialAttributeBinding&)> OnMat)
{
	if (!RootStruct || !RootAddr) return;
	for (TFieldIterator<FProperty> It(RootStruct); It; ++It)
	{
		WalkPropertyForBindings(*It, RootAddr, OnAttr, OnMat);
	}
}

} // namespace

void RefreshRendererBindings(UNiagaraRendererProperties* Renderer)
{
	if (!Renderer) return;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	const FVersionedNiagaraEmitterBase Outer = Renderer->GetOuterEmitterBase();
	const ENiagaraRendererSourceDataMode Mode = Renderer->GetCurrentSourceMode();
	UNiagaraEmitterBase* RawEmitter = Outer.Emitter;

	// Cache the RootName FNameProperty once (same struct for every binding).
	FNameProperty* RootProp = CastField<FNameProperty>(
		FNiagaraVariableAttributeBinding::StaticStruct()->FindPropertyByName(TEXT("RootName")));

	auto OnAttr = [&](FNiagaraVariableAttributeBinding& B)
	{
		if (!RootProp) return;
		// Re-run SetValue with the current RootName so the engine re-derives
		// BindingSourceMode and refreshes the cache fields. If RootName hasn't
		// changed this is effectively idempotent (namespace is stripped once).
		const FName CurrentRoot = RootProp->GetPropertyValue_InContainer(&B);
		B.SetValue(CurrentRoot, Outer, Mode);
	};

	auto OnMat = [&](FNiagaraMaterialAttributeBinding& B)
	{
		B.CacheValues(RawEmitter);
	};

	// Walk the renderer's UCLASS properties.
	for (TFieldIterator<FProperty> It(Renderer->GetClass()); It; ++It)
	{
		WalkPropertyForBindings(*It, Renderer, OnAttr, OnMat);
	}
#else
	// Pre-5.7: FVersionedNiagaraEmitterBase / UNiagaraEmitterBase / GetOuterEmitterBase
	// don't exist. Renderer-binding refresh is a no-op on older engines.
	(void)Renderer;
#endif
}

} // namespace NSAINiagaraLCD
