// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Lua/LuaBindingRegistry.h"
#include "Lua/LuaAssetResolver.h"
#include "Lua/LuaPropertyTable.h"
#include "Lua/LuaStr.h"
#include "Tools/NeoStackToolUtils.h"

#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "ChooserPropertyAccess.h"
#include "IObjectChooser.h"
#include "IChooserColumn.h"
#include "Modules/ModuleManager.h"
#include "ProxyAsset.h"
#include "ProxyTable.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
#include "ObjectChooser_Asset.h"
#include "ObjectChooser_Class.h"
#include "OutputObjectColumn.h"
#include "OutputFloatColumn.h"
#include "OutputStructColumn.h"
#include "OutputBoolColumn.h"
#include "OutputEnumColumn.h"
#if ENGINE_MINOR_VERSION >= 7
#include "OutputGameplayTagQueryColumn.h"
#endif
#include "BoolColumn.h"
#include "EnumColumn.h"
#include "GameplayTagColumn.h"
#if ENGINE_MINOR_VERSION >= 7
#include "GameplayTagQueryColumn.h"
#endif
#include "FloatRangeColumn.h"
#include "FloatDistanceColumn.h"
#include "RandomizeColumn.h"
#include "MultiEnumColumn.h"
#include "ObjectColumn.h"
#include "ObjectClassColumn.h"
#endif // ENGINE_MINOR_VERSION >= 5

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
// ============================================================================
// HELPERS
// ============================================================================

static int32 GetChooserRowCount(const UChooserTable* CT)
{
#if WITH_EDITORONLY_DATA
	if (CT->ResultsStructs.Num() > 0)
	{
		return CT->ResultsStructs.Num();
	}
#endif
	// Fallback: use the max row count across output columns
	int32 MaxRows = 0;
	for (const FInstancedStruct& ColStruct : CT->ColumnsStructs)
	{
		if (!ColStruct.IsValid()) continue;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		if (const FOutputObjectColumn* ObjCol = ColStruct.GetPtr<FOutputObjectColumn>())
		{
			MaxRows = FMath::Max(MaxRows, ObjCol->RowValues.Num());
		}
		else
#endif // ENGINE_MINOR_VERSION >= 5
		if (const FOutputFloatColumn* FloatCol = ColStruct.GetPtr<FOutputFloatColumn>())
		{
			MaxRows = FMath::Max(MaxRows, FloatCol->RowValues.Num());
		}
		else if (const FOutputStructColumn* StructCol = ColStruct.GetPtr<FOutputStructColumn>())
		{
			MaxRows = FMath::Max(MaxRows, StructCol->RowValues.Num());
		}
		else if (const FOutputBoolColumn* BoolOutCol = ColStruct.GetPtr<FOutputBoolColumn>())
		{
			MaxRows = FMath::Max(MaxRows, BoolOutCol->RowValues.Num());
		}
		else if (const FOutputEnumColumn* EnumOutCol = ColStruct.GetPtr<FOutputEnumColumn>())
		{
			MaxRows = FMath::Max(MaxRows, EnumOutCol->RowValues.Num());
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		else if (const FOutputGameplayTagQueryColumn* TagQOutCol = ColStruct.GetPtr<FOutputGameplayTagQueryColumn>())
		{
			MaxRows = FMath::Max(MaxRows, TagQOutCol->RowValues.Num());
		}
#endif
	}
	return MaxRows;
}

static int32 GetChooserAuthoringRowCount(const UChooserTable* CT)
{
#if WITH_EDITORONLY_DATA
	return CT ? CT->ResultsStructs.Num() : 0;
#else
	return CT ? GetChooserRowCount(CT) : 0;
#endif
}

static bool ValidateChooserAuthoringRow(const UChooserTable* CT, int32 RowIndex, const TCHAR* Verb, FLuaSessionData& Session, int32 ColIndex = INDEX_NONE)
{
	const int32 RowCount = GetChooserAuthoringRowCount(CT);
	if (RowIndex < 1 || RowIndex > RowCount)
	{
		if (ColIndex >= 1)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] %s(%d, %d) -> row %d out of range; valid rows are 1..%d. Call add_rows() before editing cells."),
				Verb, RowIndex, ColIndex, RowIndex, RowCount));
		}
		else
		{
			Session.Log(FString::Printf(TEXT("[FAIL] %s(%d) -> row %d out of range; valid rows are 1..%d. Call add_rows() before editing rows."),
				Verb, RowIndex, RowIndex, RowCount));
		}
		return false;
	}
	return true;
}

static const char* GetResultTypeName(EObjectChooserResultType Type)
{
	switch (Type)
	{
	case EObjectChooserResultType::ObjectResult: return "Object";
	case EObjectChooserResultType::ClassResult: return "Class";
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	case EObjectChooserResultType::NoPrimaryResult: return "NoPrimaryResult";
#endif
	default: return "Unknown";
	}
}

static bool IsChooserOutputCellColumn(const FInstancedStruct& ColStruct)
{
	if (!ColStruct.IsValid())
	{
		return false;
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	if (ColStruct.GetPtr<FOutputObjectColumn>())
	{
		return true;
	}
#endif
	if (ColStruct.GetPtr<FOutputFloatColumn>() ||
		ColStruct.GetPtr<FOutputStructColumn>() ||
		ColStruct.GetPtr<FOutputBoolColumn>() ||
		ColStruct.GetPtr<FOutputEnumColumn>())
	{
		return true;
	}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (ColStruct.GetPtr<FOutputGameplayTagQueryColumn>())
	{
		return true;
	}
#endif
	return false;
}

struct FChooserLuaBindingSpec
{
	int32 ContextIndex = INDEX_NONE;
	TArray<FName> PropertyChain;
	bool bRoot = false;
	FString DisplayName;
};

static bool TryGetChooserString(const sol::table& Table, const char* Key, FString& Out)
{
	sol::object Value = Table[Key];
	if (!Value.valid() || Value == sol::lua_nil)
	{
		return false;
	}
	if (!Value.is<std::string>())
	{
		return false;
	}
	Out = NeoLuaStr::ToFString(Value.as<std::string>());
	return true;
}

static bool TryResolveChooserClass(const FString& ClassPath, UClass*& OutClass)
{
	OutClass = nullptr;
	if (ClassPath.IsEmpty())
	{
		return false;
	}

	OutClass = FindObject<UClass>(nullptr, *ClassPath);
	if (!OutClass)
	{
		OutClass = LoadObject<UClass>(nullptr, *ClassPath);
	}
	if (!OutClass && !ClassPath.StartsWith(TEXT("/Script/")))
	{
		OutClass = UClass::TryFindTypeSlow<UClass>(ClassPath);
	}
	return OutClass != nullptr;
}

static bool TryResolveChooserStruct(const FString& StructPath, UScriptStruct*& OutStruct)
{
	OutStruct = nullptr;
	if (StructPath.IsEmpty())
	{
		return false;
	}

	OutStruct = FindObject<UScriptStruct>(nullptr, *StructPath);
	if (!OutStruct)
	{
		OutStruct = LoadObject<UScriptStruct>(nullptr, *StructPath);
	}
	if (!OutStruct && !StructPath.StartsWith(TEXT("/Script/")))
	{
		OutStruct = UClass::TryFindTypeSlow<UScriptStruct>(StructPath);
	}
	return OutStruct != nullptr;
}

static EContextObjectDirection ParseChooserContextDirection(const FString& Direction)
{
	if (Direction.Equals(TEXT("Write"), ESearchCase::IgnoreCase) ||
		Direction.Equals(TEXT("Output"), ESearchCase::IgnoreCase))
	{
		return EContextObjectDirection::Write;
	}
	if (Direction.Equals(TEXT("ReadWrite"), ESearchCase::IgnoreCase) ||
		Direction.Equals(TEXT("InputOutput"), ESearchCase::IgnoreCase) ||
		Direction.Equals(TEXT("Input/Output"), ESearchCase::IgnoreCase))
	{
		return EContextObjectDirection::ReadWrite;
	}
	return EContextObjectDirection::Read;
}

static const char* GetChooserContextDirectionName(EContextObjectDirection Direction)
{
	switch (Direction)
	{
	case EContextObjectDirection::Read: return "Read";
	case EContextObjectDirection::Write: return "Write";
	case EContextObjectDirection::ReadWrite: return "ReadWrite";
	default: return "Unknown";
	}
}

static void SplitChooserPropertyPath(const FString& Path, TArray<FName>& OutChain)
{
	TArray<FString> Parts;
	Path.ParseIntoArray(Parts, TEXT("."), true);
	for (const FString& Part : Parts)
	{
		const FString Trimmed = Part.TrimStartAndEnd();
		if (!Trimmed.IsEmpty())
		{
			OutChain.Add(FName(*Trimmed));
		}
	}
}

static bool ParseChooserBindingSpec(const sol::table& SourceTable, FChooserLuaBindingSpec& OutSpec, FString& OutError)
{
	sol::table BindingTable = SourceTable;
	sol::object BindingObj = SourceTable["binding"];
	if (BindingObj.valid() && BindingObj != sol::lua_nil)
	{
		if (!BindingObj.is<sol::table>())
		{
			OutError = TEXT("binding must be a table");
			return false;
		}
		BindingTable = BindingObj.as<sol::table>();
	}

	int ContextIndex = BindingTable.get_or("context_index", 0);
	if (ContextIndex <= 0)
	{
		ContextIndex = BindingTable.get_or("context", 0);
	}
	if (ContextIndex <= 0)
	{
		OutError = TEXT("binding.context_index must be a 1-based context parameter index");
		return false;
	}

	OutSpec = FChooserLuaBindingSpec();
	OutSpec.ContextIndex = ContextIndex - 1;
	OutSpec.bRoot = BindingTable.get_or("root", false) || BindingTable.get_or("is_root", false);

	FString DisplayName;
	if (TryGetChooserString(BindingTable, "display_name", DisplayName))
	{
		OutSpec.DisplayName = DisplayName;
	}

	sol::object ChainObj = BindingTable["property_chain"];
	if (!ChainObj.valid() || ChainObj == sol::lua_nil)
	{
		ChainObj = BindingTable["property"];
	}
	if (!ChainObj.valid() || ChainObj == sol::lua_nil)
	{
		ChainObj = BindingTable["path"];
	}

	if (ChainObj.valid() && ChainObj != sol::lua_nil)
	{
		if (ChainObj.is<std::string>())
		{
			SplitChooserPropertyPath(NeoLuaStr::ToFString(ChainObj.as<std::string>()), OutSpec.PropertyChain);
		}
		else if (ChainObj.is<sol::table>())
		{
			sol::table ChainTable = ChainObj.as<sol::table>();
			for (int32 ChainTableIndex = 1;; ++ChainTableIndex)
			{
				sol::object Entry = ChainTable[ChainTableIndex];
				if (!Entry.valid() || Entry == sol::lua_nil)
				{
					break;
				}
				if (!Entry.is<std::string>())
				{
					OutError = TEXT("binding.property_chain entries must be strings");
					return false;
				}
				const FString Name = NeoLuaStr::ToFString(Entry.as<std::string>()).TrimStartAndEnd();
				if (!Name.IsEmpty())
				{
					OutSpec.PropertyChain.Add(FName(*Name));
				}
			}
		}
		else
		{
			OutError = TEXT("binding.property must be a string or binding.property_chain must be an array of strings");
			return false;
		}
	}

	if (!OutSpec.bRoot && OutSpec.PropertyChain.IsEmpty())
	{
		OutError = TEXT("binding.property is required unless binding.root is true");
		return false;
	}

	return true;
}

static void ApplyChooserBindingFields(FChooserPropertyBinding& Binding, const FChooserLuaBindingSpec& Spec)
{
	Binding.ContextIndex = Spec.ContextIndex;
	Binding.PropertyBindingChain = Spec.PropertyChain;
	Binding.IsBoundToRoot = Spec.bRoot;
#if WITH_EDITORONLY_DATA
	Binding.DisplayName = Spec.DisplayName;
#endif
	Binding.CompiledBinding.Reset();
}

static FField* ResolveChooserBoundLeafField(const UChooserTable* CT, const FChooserLuaBindingSpec& Spec, bool* bOutLeafIsFunction = nullptr)
{
	if (bOutLeafIsFunction)
	{
		*bOutLeafIsFunction = false;
	}
	if (!CT || Spec.bRoot || Spec.PropertyChain.IsEmpty())
	{
		return nullptr;
	}

	TConstArrayView<FInstancedStruct> ContextData = CT->GetContextData();
	if (!ContextData.IsValidIndex(Spec.ContextIndex))
	{
		return nullptr;
	}

	const UStruct* StructType = nullptr;
	if (const FContextObjectTypeClass* ClassContext = ContextData[Spec.ContextIndex].GetPtr<FContextObjectTypeClass>())
	{
		StructType = ClassContext->Class;
	}
	else if (const FContextObjectTypeStruct* StructContext = ContextData[Spec.ContextIndex].GetPtr<FContextObjectTypeStruct>())
	{
		StructType = StructContext->Struct;
	}

	for (int32 ChainIndex = 0; StructType && ChainIndex < Spec.PropertyChain.Num(); ++ChainIndex)
	{
		const FName FieldName = Spec.PropertyChain[ChainIndex];
		const bool bLeaf = ChainIndex == Spec.PropertyChain.Num() - 1;
		if (FProperty* Property = FindFProperty<FProperty>(StructType, FieldName))
		{
			if (bLeaf)
			{
				return Property;
			}
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				StructType = StructProperty->Struct;
			}
			else if (const FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property))
			{
				StructType = ObjectProperty->PropertyClass;
			}
			else
			{
				return nullptr;
			}
			continue;
		}

		if (const UClass* ClassType = Cast<const UClass>(StructType))
		{
			if (UFunction* Function = ClassType->FindFunctionByName(FieldName))
			{
				if (bLeaf)
				{
					if (bOutLeafIsFunction)
					{
						*bOutLeafIsFunction = true;
					}
					return Function->GetReturnProperty();
				}
				if (const FObjectProperty* ReturnObject = CastField<FObjectProperty>(Function->GetReturnProperty()))
				{
					StructType = ReturnObject->PropertyClass;
					continue;
				}
			}
		}
		return nullptr;
	}
	return nullptr;
}

static const FContextObjectTypeBase* GetChooserContextBase(const UChooserTable* CT, int32 ContextIndex)
{
	if (!CT)
	{
		return nullptr;
	}
	TConstArrayView<FInstancedStruct> ContextData = CT->GetContextData();
	if (!ContextData.IsValidIndex(ContextIndex))
	{
		return nullptr;
	}
	if (const FContextObjectTypeClass* ClassContext = ContextData[ContextIndex].GetPtr<FContextObjectTypeClass>())
	{
		return ClassContext;
	}
	if (const FContextObjectTypeStruct* StructContext = ContextData[ContextIndex].GetPtr<FContextObjectTypeStruct>())
	{
		return StructContext;
	}
	return nullptr;
}

static bool ValidateChooserBindingDirection(const FChooserColumnBase* ColumnBase, const UChooserTable* CT, const FChooserLuaBindingSpec& Spec, FString& OutError)
{
	const FContextObjectTypeBase* ContextBase = GetChooserContextBase(CT, Spec.ContextIndex);
	if (!ContextBase)
	{
		OutError = FString::Printf(TEXT("context index %d is out of range; add a context parameter first"), Spec.ContextIndex + 1);
		return false;
	}
	if (!ColumnBase)
	{
		return true;
	}

	const bool bReads = ColumnBase->HasFilters() || ColumnBase->HasCosts();
	const bool bWrites = ColumnBase->HasOutputs();
	const EContextObjectDirection Direction = ContextBase->Direction;
	if (bReads && Direction == EContextObjectDirection::Write)
	{
		OutError = FString::Printf(TEXT("column reads from context %d, but that context direction is Write"), Spec.ContextIndex + 1);
		return false;
	}
	if (bWrites && Direction == EContextObjectDirection::Read)
	{
		OutError = FString::Printf(TEXT("column writes to context %d, but that context direction is Read"), Spec.ContextIndex + 1);
		return false;
	}
	return true;
}

static bool IsChooserBindingCompatibleWithProperty(const UScriptStruct* ParameterStruct, const FProperty* Property, FString& OutError)
{
	if (!ParameterStruct || !Property)
	{
		return true;
	}

	if (ParameterStruct == FBoolContextProperty::StaticStruct())
	{
		if (Property->IsA<FBoolProperty>())
		{
			return true;
		}
		OutError = FString::Printf(TEXT("expected bool property, got %s"), *Property->GetCPPType());
		return false;
	}

	if (ParameterStruct == FEnumContextProperty::StaticStruct())
	{
		if (Property->IsA<FEnumProperty>())
		{
			return true;
		}
		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			if (ByteProperty->Enum)
			{
				return true;
			}
		}
		OutError = FString::Printf(TEXT("expected enum or enum-backed byte property, got %s"), *Property->GetCPPType());
		return false;
	}

	if (ParameterStruct == FFloatContextProperty::StaticStruct())
	{
		if (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>() || Property->IsA<FIntProperty>())
		{
			return true;
		}
		OutError = FString::Printf(TEXT("expected float, double, or int32 property, got %s"), *Property->GetCPPType());
		return false;
	}

	if (ParameterStruct == FObjectContextProperty::StaticStruct())
	{
		if (Property->IsA<FObjectPropertyBase>())
		{
			return true;
		}
		OutError = FString::Printf(TEXT("expected object property, got %s"), *Property->GetCPPType());
		return false;
	}

	if (ParameterStruct == FStructContextProperty::StaticStruct())
	{
		if (Property->IsA<FStructProperty>())
		{
			return true;
		}
		OutError = FString::Printf(TEXT("expected struct property, got %s"), *Property->GetCPPType());
		return false;
	}

	if (ParameterStruct == FGameplayTagContextProperty::StaticStruct())
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (StructProperty && StructProperty->Struct == FGameplayTagContainer::StaticStruct())
		{
			return true;
		}
		OutError = FString::Printf(TEXT("expected FGameplayTagContainer property, got %s"), *Property->GetCPPType());
		return false;
	}

	if (ParameterStruct == FRandomizeContextProperty::StaticStruct())
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (StructProperty && StructProperty->Struct == FChooserRandomizationContext::StaticStruct())
		{
			return true;
		}
		OutError = FString::Printf(TEXT("expected FChooserRandomizationContext property, got %s"), *Property->GetCPPType());
		return false;
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (ParameterStruct == FGameplayTagQueryContextProperty::StaticStruct())
	{
		const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
		if (StructProperty && StructProperty->Struct == FGameplayTagQuery::StaticStruct())
		{
			return true;
		}
		OutError = FString::Printf(TEXT("expected FGameplayTagQuery property, got %s"), *Property->GetCPPType());
		return false;
	}
#endif

	return true;
}

static bool ValidateChooserBindingTarget(const UScriptStruct* ParameterStruct, const UChooserTable* CT, const FChooserLuaBindingSpec& Spec, FField* LeafField, FString& OutError)
{
	if (Spec.bRoot)
	{
		TConstArrayView<FInstancedStruct> ContextData = CT ? CT->GetContextData() : TConstArrayView<FInstancedStruct>();
		if (!ContextData.IsValidIndex(Spec.ContextIndex))
		{
			OutError = FString::Printf(TEXT("context index %d is out of range; add a context parameter first"), Spec.ContextIndex + 1);
			return false;
		}
		if (ParameterStruct == FStructContextProperty::StaticStruct())
		{
			if (ContextData[Spec.ContextIndex].GetPtr<FContextObjectTypeStruct>())
			{
				return true;
			}
			OutError = TEXT("root struct binding requires a struct context parameter");
			return false;
		}
		OutError = TEXT("root binding is only supported for struct column inputs");
		return false;
	}

	const FProperty* Property = CastField<FProperty>(LeafField);
	if (!Property)
	{
		OutError = Spec.PropertyChain.Num() > 0
			? FString::Printf(TEXT("property/function not found: %s"), *Spec.PropertyChain.Last().ToString())
			: TEXT("binding property is empty");
		return false;
	}
	return IsChooserBindingCompatibleWithProperty(ParameterStruct, Property, OutError);
}

static void AddChooserBindingInfo(sol::table& Entry, const FChooserPropertyBinding& Binding)
{
	Entry["context_index"] = Binding.ContextIndex + 1;
	Entry["root"] = Binding.IsBoundToRoot;
	sol::state_view Lua(Entry.lua_state());
	sol::table Chain = Lua.create_table();
	for (int32 i = 0; i < Binding.PropertyBindingChain.Num(); ++i)
	{
		Chain[i + 1] = TCHAR_TO_UTF8(*Binding.PropertyBindingChain[i].ToString());
	}
	Entry["property_chain"] = Chain;
#if WITH_EDITORONLY_DATA
	if (!Binding.DisplayName.IsEmpty())
	{
		Entry["display_name"] = TCHAR_TO_UTF8(*Binding.DisplayName);
	}
	if (!Binding.CompileMessage.IsEmpty())
	{
		Entry["compile_message"] = TCHAR_TO_UTF8(*Binding.CompileMessage.ToString());
	}
#endif
}

static bool AddChooserColumnBindingInfo(sol::table& Entry, const FInstancedStruct& ColStruct)
{
	const FInstancedStruct* InputValue = nullptr;
	if (const FBoolColumn* BoolCol = ColStruct.GetPtr<FBoolColumn>()) InputValue = &BoolCol->InputValue;
	else if (const FEnumColumn* EnumCol = ColStruct.GetPtr<FEnumColumn>()) InputValue = &EnumCol->InputValue;
	else if (const FMultiEnumColumn* MultiEnumCol = ColStruct.GetPtr<FMultiEnumColumn>()) InputValue = &MultiEnumCol->InputValue;
	else if (const FGameplayTagColumn* TagCol = ColStruct.GetPtr<FGameplayTagColumn>()) InputValue = &TagCol->InputValue;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	else if (const FGameplayTagQueryColumn* TagQueryCol = ColStruct.GetPtr<FGameplayTagQueryColumn>()) InputValue = &TagQueryCol->InputValue;
#endif
	else if (const FFloatRangeColumn* FloatRangeCol = ColStruct.GetPtr<FFloatRangeColumn>()) InputValue = &FloatRangeCol->InputValue;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	else if (const FFloatDistanceColumn* FloatDistanceCol = ColStruct.GetPtr<FFloatDistanceColumn>()) InputValue = &FloatDistanceCol->InputValue;
#endif
	else if (const FRandomizeColumn* RandomizeCol = ColStruct.GetPtr<FRandomizeColumn>()) InputValue = &RandomizeCol->InputValue;
	else if (const FObjectColumn* ObjectCol = ColStruct.GetPtr<FObjectColumn>()) InputValue = &ObjectCol->InputValue;
	else if (const FObjectClassColumn* ObjectClassCol = ColStruct.GetPtr<FObjectClassColumn>()) InputValue = &ObjectClassCol->InputValue;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	else if (const FOutputObjectColumn* OutputObjectCol = ColStruct.GetPtr<FOutputObjectColumn>()) InputValue = &OutputObjectCol->InputValue;
#endif
	else if (const FOutputFloatColumn* OutputFloatCol = ColStruct.GetPtr<FOutputFloatColumn>()) InputValue = &OutputFloatCol->InputValue;
	else if (const FOutputStructColumn* OutputStructCol = ColStruct.GetPtr<FOutputStructColumn>()) InputValue = &OutputStructCol->InputValue;
	else if (const FOutputBoolColumn* OutputBoolCol = ColStruct.GetPtr<FOutputBoolColumn>()) InputValue = &OutputBoolCol->InputValue;
	else if (const FOutputEnumColumn* OutputEnumCol = ColStruct.GetPtr<FOutputEnumColumn>()) InputValue = &OutputEnumCol->InputValue;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	else if (const FOutputGameplayTagQueryColumn* OutputTagQueryCol = ColStruct.GetPtr<FOutputGameplayTagQueryColumn>()) InputValue = &OutputTagQueryCol->InputValue;
#endif

	if (!InputValue || !InputValue->IsValid())
	{
		Entry["is_bound"] = false;
		return false;
	}

	Entry["is_bound"] = true;
	Entry["binding_type"] = TCHAR_TO_UTF8(*InputValue->GetScriptStruct()->GetName());
	if (const FBoolContextProperty* BoolParam = InputValue->GetPtr<FBoolContextProperty>()) AddChooserBindingInfo(Entry, BoolParam->Binding);
	else if (const FEnumContextProperty* EnumParam = InputValue->GetPtr<FEnumContextProperty>()) AddChooserBindingInfo(Entry, EnumParam->Binding);
	else if (const FGameplayTagContextProperty* TagParam = InputValue->GetPtr<FGameplayTagContextProperty>()) AddChooserBindingInfo(Entry, TagParam->Binding);
	else if (const FFloatContextProperty* FloatParam = InputValue->GetPtr<FFloatContextProperty>()) AddChooserBindingInfo(Entry, FloatParam->Binding);
	else if (const FRandomizeContextProperty* RandomizeParam = InputValue->GetPtr<FRandomizeContextProperty>()) AddChooserBindingInfo(Entry, RandomizeParam->Binding);
	else if (const FObjectContextProperty* ObjectParam = InputValue->GetPtr<FObjectContextProperty>()) AddChooserBindingInfo(Entry, ObjectParam->Binding);
	else if (const FStructContextProperty* StructParam = InputValue->GetPtr<FStructContextProperty>()) AddChooserBindingInfo(Entry, StructParam->Binding);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	else if (const FGameplayTagQueryContextProperty* TagQueryParam = InputValue->GetPtr<FGameplayTagQueryContextProperty>()) AddChooserBindingInfo(Entry, TagQueryParam->Binding);
#endif
	return true;
}

static FChooserPropertyBinding* GetMutableChooserInputBinding(FInstancedStruct& InputValue)
{
	if (!InputValue.IsValid())
	{
		return nullptr;
	}
	if (FBoolContextProperty* BoolParam = InputValue.GetMutablePtr<FBoolContextProperty>()) return &BoolParam->Binding;
	if (FEnumContextProperty* EnumParam = InputValue.GetMutablePtr<FEnumContextProperty>()) return &EnumParam->Binding;
	if (FGameplayTagContextProperty* TagParam = InputValue.GetMutablePtr<FGameplayTagContextProperty>()) return &TagParam->Binding;
	if (FFloatContextProperty* FloatParam = InputValue.GetMutablePtr<FFloatContextProperty>()) return &FloatParam->Binding;
	if (FRandomizeContextProperty* RandomizeParam = InputValue.GetMutablePtr<FRandomizeContextProperty>()) return &RandomizeParam->Binding;
	if (FObjectContextProperty* ObjectParam = InputValue.GetMutablePtr<FObjectContextProperty>()) return &ObjectParam->Binding;
	if (FStructContextProperty* StructParam = InputValue.GetMutablePtr<FStructContextProperty>()) return &StructParam->Binding;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (FGameplayTagQueryContextProperty* TagQueryParam = InputValue.GetMutablePtr<FGameplayTagQueryContextProperty>()) return &TagQueryParam->Binding;
#endif
	return nullptr;
}

static FInstancedStruct* GetMutableChooserColumnInputStruct(FInstancedStruct& ColStruct)
{
	if (FBoolColumn* BoolCol = ColStruct.GetMutablePtr<FBoolColumn>()) return &BoolCol->InputValue;
	if (FEnumColumn* EnumCol = ColStruct.GetMutablePtr<FEnumColumn>()) return &EnumCol->InputValue;
	if (FMultiEnumColumn* MultiEnumCol = ColStruct.GetMutablePtr<FMultiEnumColumn>()) return &MultiEnumCol->InputValue;
	if (FGameplayTagColumn* TagCol = ColStruct.GetMutablePtr<FGameplayTagColumn>()) return &TagCol->InputValue;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (FGameplayTagQueryColumn* TagQueryCol = ColStruct.GetMutablePtr<FGameplayTagQueryColumn>()) return &TagQueryCol->InputValue;
#endif
	if (FFloatRangeColumn* FloatRangeCol = ColStruct.GetMutablePtr<FFloatRangeColumn>()) return &FloatRangeCol->InputValue;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	if (FFloatDistanceColumn* FloatDistanceCol = ColStruct.GetMutablePtr<FFloatDistanceColumn>()) return &FloatDistanceCol->InputValue;
#endif
	if (FRandomizeColumn* RandomizeCol = ColStruct.GetMutablePtr<FRandomizeColumn>()) return &RandomizeCol->InputValue;
	if (FObjectColumn* ObjectCol = ColStruct.GetMutablePtr<FObjectColumn>()) return &ObjectCol->InputValue;
	if (FObjectClassColumn* ObjectClassCol = ColStruct.GetMutablePtr<FObjectClassColumn>()) return &ObjectClassCol->InputValue;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	if (FOutputObjectColumn* OutputObjectCol = ColStruct.GetMutablePtr<FOutputObjectColumn>()) return &OutputObjectCol->InputValue;
#endif
	if (FOutputFloatColumn* OutputFloatCol = ColStruct.GetMutablePtr<FOutputFloatColumn>()) return &OutputFloatCol->InputValue;
	if (FOutputStructColumn* OutputStructCol = ColStruct.GetMutablePtr<FOutputStructColumn>()) return &OutputStructCol->InputValue;
	if (FOutputBoolColumn* OutputBoolCol = ColStruct.GetMutablePtr<FOutputBoolColumn>()) return &OutputBoolCol->InputValue;
	if (FOutputEnumColumn* OutputEnumCol = ColStruct.GetMutablePtr<FOutputEnumColumn>()) return &OutputEnumCol->InputValue;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (FOutputGameplayTagQueryColumn* OutputTagQueryCol = ColStruct.GetMutablePtr<FOutputGameplayTagQueryColumn>()) return &OutputTagQueryCol->InputValue;
#endif
	return nullptr;
}

static void OffsetChooserBindingsAfterContextInsert(UChooserTable* CT, int32 InsertAt)
{
	if (!CT)
	{
		return;
	}
	for (FInstancedStruct& ColStruct : CT->ColumnsStructs)
	{
		if (FInstancedStruct* InputValue = GetMutableChooserColumnInputStruct(ColStruct))
		{
			if (FChooserPropertyBinding* Binding = GetMutableChooserInputBinding(*InputValue))
			{
				if (Binding->ContextIndex >= InsertAt)
				{
					Binding->ContextIndex++;
					Binding->CompiledBinding.Reset();
				}
			}
		}
	}
}

static void RefreshChooserColumnBindingMetadata(FInstancedStruct& ColStruct)
{
#if WITH_EDITOR
	if (FOutputStructColumn* StructColumn = ColStruct.GetMutablePtr<FOutputStructColumn>())
	{
		StructColumn->StructTypeChanged();
	}

	const UEnum* BoundEnum = nullptr;
	if (const FEnumColumn* EnumColumn = ColStruct.GetPtr<FEnumColumn>())
	{
		BoundEnum = EnumColumn->GetEnum();
	}
	else if (const FMultiEnumColumn* MultiEnumColumn = ColStruct.GetPtr<FMultiEnumColumn>())
	{
		BoundEnum = MultiEnumColumn->GetEnum();
	}
	else if (const FOutputEnumColumn* OutputEnumColumn = ColStruct.GetPtr<FOutputEnumColumn>())
	{
		BoundEnum = OutputEnumColumn->GetEnum();
	}

	if (BoundEnum)
	{
		if (FEnumColumnBase* EnumBase = ColStruct.GetMutablePtr<FEnumColumnBase>())
		{
			EnumBase->EnumChanged(BoundEnum);
		}
	}
#else
	(void)ColStruct;
#endif
}

template <typename ColumnType, typename ParameterType>
static bool TryApplyChooserTypedBinding(FInstancedStruct& ColStruct, UChooserTable* CT, const FChooserLuaBindingSpec& Spec, FString& OutError)
{
	if (ColumnType* Column = ColStruct.GetMutablePtr<ColumnType>())
	{
		FChooserColumnBase* Base = ColStruct.GetMutablePtr<FChooserColumnBase>();
		if (!ValidateChooserBindingDirection(Base, CT, Spec, OutError))
		{
			return true;
		}
		bool bLeafIsFunction = false;
		FField* LeafField = ResolveChooserBoundLeafField(CT, Spec, &bLeafIsFunction);
		if (bLeafIsFunction && Base && Base->HasOutputs())
		{
			OutError = TEXT("output columns cannot bind to function return values because UE property bindings only write to properties");
			return true;
		}
		if (!ValidateChooserBindingTarget(ParameterType::StaticStruct(), CT, Spec, LeafField, OutError))
		{
			return true;
		}

		Column->InputValue.InitializeAs(ParameterType::StaticStruct());
		ParameterType& Param = Column->InputValue.template GetMutable<ParameterType>();
		ApplyChooserBindingFields(Param.Binding, Spec);
#if WITH_EDITORONLY_DATA && ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		Param.Binding.SetPropertyData(CT, LeafField);
#endif
		RefreshChooserColumnBindingMetadata(ColStruct);
		Param.Binding.Compile(CT, true);
		if (Base)
		{
			Base->Compile(CT, true);
		}
		return true;
	}
	return false;
}

static bool ApplyChooserColumnBinding(FInstancedStruct& ColStruct, UChooserTable* CT, const FChooserLuaBindingSpec& Spec, FString& OutError)
{
	if (!CT || !CT->GetContextData().IsValidIndex(Spec.ContextIndex))
	{
		OutError = FString::Printf(TEXT("context index %d is out of range; add a context parameter first"), Spec.ContextIndex + 1);
		return false;
	}

	if (TryApplyChooserTypedBinding<FBoolColumn, FBoolContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FOutputBoolColumn, FBoolContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FEnumColumn, FEnumContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FMultiEnumColumn, FEnumContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FOutputEnumColumn, FEnumContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FGameplayTagColumn, FGameplayTagContextProperty>(ColStruct, CT, Spec, OutError) ||
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		TryApplyChooserTypedBinding<FGameplayTagQueryColumn, FGameplayTagContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FOutputGameplayTagQueryColumn, FGameplayTagQueryContextProperty>(ColStruct, CT, Spec, OutError) ||
#endif
		TryApplyChooserTypedBinding<FFloatRangeColumn, FFloatContextProperty>(ColStruct, CT, Spec, OutError) ||
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
		TryApplyChooserTypedBinding<FFloatDistanceColumn, FFloatContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FOutputObjectColumn, FObjectContextProperty>(ColStruct, CT, Spec, OutError) ||
#endif
		TryApplyChooserTypedBinding<FOutputFloatColumn, FFloatContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FRandomizeColumn, FRandomizeContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FObjectColumn, FObjectContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FObjectClassColumn, FObjectContextProperty>(ColStruct, CT, Spec, OutError) ||
		TryApplyChooserTypedBinding<FOutputStructColumn, FStructContextProperty>(ColStruct, CT, Spec, OutError))
	{
		return OutError.IsEmpty();
	}

	const FString TypeName = ColStruct.IsValid() && ColStruct.GetScriptStruct()
		? ColStruct.GetScriptStruct()->GetName()
		: TEXT("(invalid)");
	OutError = FString::Printf(TEXT("column type %s does not expose an InputValue binding"), *TypeName);
	return false;
}

static void AddChooserEditorSummary(sol::table& Result, const UChooserTable* CT)
{
#if WITH_EDITORONLY_DATA
	int32 DisabledCount = 0;
	for (int32 i = 0; i < CT->DisabledRows.Num(); ++i)
	{
		if (CT->DisabledRows[i])
		{
			DisabledCount++;
		}
	}
	Result["disabled_rows"] = DisabledCount;
	Result["nested_choosers"] = static_cast<int>(CT->NestedChoosers.Num());
#else
	(void)CT;
	Result["disabled_rows"] = 0;
	Result["nested_choosers"] = 0;
#endif
}

static void AddChooserColumnEditorFields(sol::table& Entry, const FChooserColumnBase* ColumnBase)
{
#if WITH_EDITORONLY_DATA
	Entry["is_disabled"] = ColumnBase->bDisabled;
#else
	(void)Entry;
	(void)ColumnBase;
#endif
}

static void AddChooserEnumValueName(sol::table& Entry, const FChooserEnumRowData& EnumRow)
{
#if WITH_EDITORONLY_DATA
	if (!EnumRow.ValueName.IsNone())
	{
		Entry["value_name"] = TCHAR_TO_UTF8(*EnumRow.ValueName.ToString());
	}
#else
	(void)Entry;
	(void)EnumRow;
#endif
}

static void AddChooserRowResultInfo(sol::table& RowTable, const UChooserTable* CT, int32 RowIdx)
{
#if WITH_EDITORONLY_DATA
	if (RowIdx < CT->ResultsStructs.Num())
	{
		const FInstancedStruct& ResultStruct = CT->ResultsStructs[RowIdx];
		if (ResultStruct.IsValid())
		{
			FString ResultType = ResultStruct.GetScriptStruct()
				? ResultStruct.GetScriptStruct()->GetName()
				: TEXT("Unknown");
			RowTable["result_type"] = TCHAR_TO_UTF8(*ResultType);

			if (const FAssetChooser* AssetResult = ResultStruct.GetPtr<FAssetChooser>())
			{
				if (UObject* Asset = AssetResult->Asset.Get())
				{
					RowTable["result_asset"] = TCHAR_TO_UTF8(*Asset->GetName());
					RowTable["result_asset_path"] = TCHAR_TO_UTF8(*Asset->GetPathName());
				}
			}
			else if (const FSoftAssetChooser* SoftResult = ResultStruct.GetPtr<FSoftAssetChooser>())
			{
				FSoftObjectPath SoftPath = SoftResult->Asset.ToSoftObjectPath();
				if (SoftPath.IsValid())
				{
					RowTable["result_asset"] = TCHAR_TO_UTF8(*FPackageName::GetShortName(SoftPath.ToString()));
					RowTable["result_asset_path"] = TCHAR_TO_UTF8(*SoftPath.ToString());
					RowTable["result_is_soft"] = true;
				}
			}
			else if (const FNestedChooser* Nested = ResultStruct.GetPtr<FNestedChooser>())
			{
				if (Nested->Chooser)
				{
					RowTable["result_nested_chooser"] = TCHAR_TO_UTF8(*Nested->Chooser->GetName());
				}
			}
			else if (const FEvaluateChooser* EvalChooser = ResultStruct.GetPtr<FEvaluateChooser>())
			{
				if (EvalChooser->Chooser)
				{
					RowTable["result_evaluate_chooser"] = TCHAR_TO_UTF8(*EvalChooser->Chooser->GetName());
				}
			}
		}
	}
#else
	(void)RowTable;
	(void)CT;
	(void)RowIdx;
#endif
}

// Map a column type name (case-insensitive) to a UScriptStruct for column creation
static const UScriptStruct* ResolveChooserColumnStruct(const FString& TypeName)
{
	// Filter columns
	if (TypeName.Equals(TEXT("Bool"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("BoolColumn"), ESearchCase::IgnoreCase))
		return FBoolColumn::StaticStruct();
	if (TypeName.Equals(TEXT("Enum"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("EnumColumn"), ESearchCase::IgnoreCase))
		return FEnumColumn::StaticStruct();
	if (TypeName.Equals(TEXT("GameplayTag"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("GameplayTagColumn"), ESearchCase::IgnoreCase))
		return FGameplayTagColumn::StaticStruct();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (TypeName.Equals(TEXT("GameplayTagQuery"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("GameplayTagQueryColumn"), ESearchCase::IgnoreCase))
		return FGameplayTagQueryColumn::StaticStruct();
#endif
	if (TypeName.Equals(TEXT("FloatRange"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FloatRangeColumn"), ESearchCase::IgnoreCase))
		return FFloatRangeColumn::StaticStruct();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	if (TypeName.Equals(TEXT("FloatDistance"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FloatDistanceColumn"), ESearchCase::IgnoreCase))
		return FFloatDistanceColumn::StaticStruct();
#endif
	if (TypeName.Equals(TEXT("Randomize"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("RandomizeColumn"), ESearchCase::IgnoreCase))
		return FRandomizeColumn::StaticStruct();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	if (TypeName.Equals(TEXT("MultiEnum"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("MultiEnumColumn"), ESearchCase::IgnoreCase))
		return FMultiEnumColumn::StaticStruct();
#endif
	if (TypeName.Equals(TEXT("Object"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("ObjectColumn"), ESearchCase::IgnoreCase))
		return FObjectColumn::StaticStruct();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	if (TypeName.Equals(TEXT("ObjectClass"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("ObjectClassColumn"), ESearchCase::IgnoreCase))
		return FObjectClassColumn::StaticStruct();
#endif

	// Output columns
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	if (TypeName.Equals(TEXT("OutputObject"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("OutputObjectColumn"), ESearchCase::IgnoreCase))
		return FOutputObjectColumn::StaticStruct();
#endif // ENGINE_MINOR_VERSION >= 5
	if (TypeName.Equals(TEXT("OutputFloat"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("OutputFloatColumn"), ESearchCase::IgnoreCase))
		return FOutputFloatColumn::StaticStruct();
	if (TypeName.Equals(TEXT("OutputStruct"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("OutputStructColumn"), ESearchCase::IgnoreCase))
		return FOutputStructColumn::StaticStruct();
	if (TypeName.Equals(TEXT("OutputBool"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("OutputBoolColumn"), ESearchCase::IgnoreCase))
		return FOutputBoolColumn::StaticStruct();
	if (TypeName.Equals(TEXT("OutputEnum"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("OutputEnumColumn"), ESearchCase::IgnoreCase))
		return FOutputEnumColumn::StaticStruct();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (TypeName.Equals(TEXT("OutputGameplayTagQuery"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("OutputGameplayTagQueryColumn"), ESearchCase::IgnoreCase))
		return FOutputGameplayTagQueryColumn::StaticStruct();
#endif

	return nullptr;
}

// Build a row/fallback result FInstancedStruct from a Lua argument that can be
// either a plain asset-path string (legacy: FAssetChooser) or an options table:
//   { kind = "asset"|"soft_asset"|"class"|"evaluate_chooser",
//     asset_path = "/Game/...", class_path = "/Script/...", chooser_path = "/Game/..." }
// Returns false with OutError on any failure. OwningChooser is only used for a
// ResultType compatibility diagnostic on kind="class".
static bool BuildResultInstancedStruct(const sol::object& Value,
                                       const UChooserTable* OwningChooser,
                                       FInstancedStruct& OutStruct,
                                       FString& OutSummary,
                                       FString& OutError)
{
	auto ExtractPath = [](const sol::table& Opts, const char* Key) -> FString
	{
		std::string S = Opts.get_or<std::string>(Key, "");
		return NeoLuaStr::ToFString(S);
	};

	FString Kind = TEXT("asset");
	FString AssetPath, ClassPath, ChooserPath;

	if (Value.is<std::string>())
	{
		AssetPath = NeoLuaStr::ToFString(Value.as<std::string>());
	}
	else if (Value.is<sol::table>())
	{
		sol::table Opts = Value.as<sol::table>();
		std::string KindStr = Opts.get_or<std::string>("kind", "asset");
		Kind = NeoLuaStr::ToFString(KindStr);
		AssetPath   = ExtractPath(Opts, "asset_path");
		ClassPath   = ExtractPath(Opts, "class_path");
		ChooserPath = ExtractPath(Opts, "chooser_path");
	}
	else
	{
		OutError = TEXT("pass an asset path string or { kind=\"asset|soft_asset|class|evaluate_chooser\", asset_path|class_path|chooser_path=\"/Game/...\" }");
		return false;
	}

	if (Kind.Equals(TEXT("asset"), ESearchCase::IgnoreCase))
	{
		if (AssetPath.IsEmpty())
		{
			OutError = TEXT("asset_path is required for kind=\"asset\"");
			return false;
		}
		UObject* Asset = NeoLuaAsset::ResolveWithRegistry<UObject>(AssetPath);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("asset not found: %s"), *AssetPath);
			return false;
		}
		FAssetChooser New;
		New.Asset = Asset;
		OutStruct = FInstancedStruct::Make(New);
		OutSummary = FString::Printf(TEXT("Asset=%s"), *Asset->GetPathName());
		return true;
	}

	if (Kind.Equals(TEXT("soft_asset"), ESearchCase::IgnoreCase))
	{
		if (AssetPath.IsEmpty())
		{
			OutError = TEXT("asset_path is required for kind=\"soft_asset\"");
			return false;
		}
		FString Normalized = NeoLuaAsset::NormalizePath(AssetPath);
		FSoftAssetChooser New;
		New.Asset = TSoftObjectPtr<UObject>(FSoftObjectPath(Normalized));
		OutStruct = FInstancedStruct::Make(New);
		OutSummary = FString::Printf(TEXT("SoftAsset=%s"), *Normalized);
		return true;
	}

	if (Kind.Equals(TEXT("class"), ESearchCase::IgnoreCase))
	{
		if (ClassPath.IsEmpty())
		{
			OutError = TEXT("class_path is required for kind=\"class\"");
			return false;
		}
		if (OwningChooser && OwningChooser->ResultType != EObjectChooserResultType::ClassResult)
		{
			OutError = FString::Printf(TEXT("kind=\"class\" requires chooser ResultType=ClassResult (got %s); change ResultType first or use kind=\"asset\""),
				ANSI_TO_TCHAR(GetResultTypeName(OwningChooser->ResultType)));
			return false;
		}
		UClass* Found = FindObject<UClass>(nullptr, *ClassPath);
		if (!Found)
		{
			Found = LoadObject<UClass>(nullptr, *ClassPath);
		}
		if (!Found)
		{
			OutError = FString::Printf(TEXT("class not found: %s"), *ClassPath);
			return false;
		}
		FClassChooser New;
		New.Class = Found;
		OutStruct = FInstancedStruct::Make(New);
		OutSummary = FString::Printf(TEXT("Class=%s"), *Found->GetPathName());
		return true;
	}

	if (Kind.Equals(TEXT("evaluate_chooser"), ESearchCase::IgnoreCase))
	{
		if (ChooserPath.IsEmpty())
		{
			OutError = TEXT("chooser_path is required for kind=\"evaluate_chooser\"");
			return false;
		}
		UChooserTable* Referenced = NeoLuaAsset::ResolveWithRegistry<UChooserTable>(ChooserPath);
		if (!Referenced)
		{
			OutError = FString::Printf(TEXT("chooser not found: %s"), *ChooserPath);
			return false;
		}
		FEvaluateChooser New;
		New.Chooser = Referenced;
		OutStruct = FInstancedStruct::Make(New);
		OutSummary = FString::Printf(TEXT("EvaluateChooser=%s"), *Referenced->GetPathName());
		return true;
	}

	OutError = FString::Printf(TEXT("unknown kind \"%s\"; valid: asset, soft_asset, class, evaluate_chooser (nested chooser authoring requires editing CT->NestedChoosers — not yet supported)"), *Kind);
	return false;
}

// ============================================================================
// BINDING
// ============================================================================

static TArray<FLuaFunctionDoc> ChooserTableDocs = {};

static void BindChooserTable(sol::state& Lua, FLuaSessionData& Session)
{
	Lua.set_function("_enrich_chooser_table", [&Session](sol::table AssetObj, sol::this_state S)
	{
		sol::state_view LuaView(S);
		std::string PathStr = AssetObj.get<std::string>("path");
		FString FPath = NeoLuaStr::ToFString(PathStr);
		UChooserTable* CT = NeoLuaAsset::Resolve<UChooserTable>(FPath);
		if (!CT) return;

		// GC-safe weak reference — every method lambda captures this and re-resolves
		// to guard against the asset being destroyed out from under the Lua handle.
		TWeakObjectPtr<UChooserTable> WeakCT(CT);

		// ---- help text ----
		AssetObj["_help_text"] =
			"ChooserTable enrichment (read + write).\n"
			"\n"
			"info() — summary: column/row counts, first_output_column, result type, fallback, disabled rows, nested choosers\n"
			"\n"
			"list(type):\n"
			"  list(\"columns\")   — all columns with index, struct type, is_output, engine_has_outputs, is_disabled\n"
			"  list(\"rows\")      — all rows with index, disabled flag, filter values (Bool/Enum/Tag/FloatRange/Randomize/FloatDistance/MultiEnum/GameplayTagQuery), outputs (Object/Float/Struct/Bool/Enum/GameplayTagQuery)\n"
			"  list(\"references\")— flat list of all asset references across all output columns\n"
			"  list(\"context\")   — context parameter definitions\n"
			"\n"
			"Column management (editor-only):\n"
			"  add_context(opts)       — add context parameter: {kind=\"class\", class=\"/Script/Engine.Actor\", direction=\"Read\"} or {kind=\"struct\", struct=\"/Script/...\", direction=\"Write\"}\n"
			"  add_column(type, opts?) — add column. Types: Bool, Enum, GameplayTag, GameplayTagQuery, FloatRange, FloatDistance, Randomize, MultiEnum, Object, ObjectClass, OutputObject, OutputFloat, OutputStruct, OutputBool, OutputEnum, OutputGameplayTagQuery. opts.index = 1-based insertion position; opts.binding = {context_index=1, property=\"A.B\"} or {context_index=1, root=true}\n"
			"  bind_column_input(index, binding) — rebind an existing column without changing row values. binding shape matches add_column opts.binding\n"
			"  remove_column(index)    — remove column by 1-based index\n"
			"\n"
			"Cell value setting (editor-only, 1-based row/column indices):\n"
			"  set_cell(row, column, opts) — set filter cell value:\n"
			"    Bool:             opts.value = \"True\"/\"False\"/\"Any\"\n"
			"    Enum:             opts.value = int, opts.comparison = \"Equal\"/\"NotEqual\"/\"Any\"\n"
			"    GameplayTag:      opts.tags = \"Tag.One,Tag.Two\"\n"
			"    FloatRange:       opts.min, opts.max, opts.no_min, opts.no_max\n"
			"    Randomize:        opts.weight = float\n"
			"    FloatDistance:    opts.value = float\n"
			"    MultiEnum:        opts.value = int (bitmask)\n"
			"    GameplayTagQuery: opts.match_type = \"any\"/\"all\"/\"none\", opts.tags = \"Tag.One,Tag.Two\"\n"
			"    Object:           opts.asset_path = \"/Game/...\", opts.comparison = \"Equal\"/\"NotEqual\"/\"Any\"\n"
			"    ObjectClass:      opts.class_path = \"/Script/...\", opts.comparison = \"Equal\"/\"NotEqual\"/\"SubClassOf\"/\"NotSubClassOf\"/\"Any\"\n"
			"  set_output_cell(row, column, opts) — set output cell value:\n"
			"    OutputObject:     opts.asset_path = \"/Game/...\"\n"
			"    OutputFloat:      opts.value = float\n"
			"    OutputBool:       opts.value = bool\n"
			"    OutputEnum:       opts.value = int\n"
			"    OutputGameplayTagQuery: opts.match_type = \"any\"/\"all\"/\"none\", opts.tags = \"Tag.One,Tag.Two\"\n"
			"\n"
			"Row operations (editor-only):\n"
			"  add_rows(count?, index?)   — insert rows (default 1 at end)\n"
			"  delete_rows(indices)       — delete rows by 1-based index array, e.g. {1, 3, 5}\n"
			"  move_row(from, to)         — move row from 1-based index to another\n"
			"  set_row_disabled(index, disabled) — enable/disable a row\n"
			"  set_row_result(index, arg) — set the result for a row. arg is either an asset path string\n"
			"                               or { kind=\"asset\"|\"soft_asset\"|\"class\"|\"evaluate_chooser\",\n"
			"                                    asset_path=\"/Game/...\" | class_path=\"/Script/...\" | chooser_path=\"/Game/...\" }\n"
			"  set_fallback_result(arg)   — set the chooser's fallback result (same arg shape)\n"
			"  set_fallback_output(column, opts) — set a column's FallbackValue (output written when fallback fires).\n"
			"                                     opts mirrors set_output_cell per column type.\n"
			"  compile()                  — recompile the chooser after edits\n"
			"\n"
			"Runtime evaluation (globals, outside the handle):\n"
			"  chooser_evaluate(table_path, context_object_path?)            — returns selected asset path or nil\n"
			"  chooser_evaluate_multi(table_path, context_object_path?)      — returns array of asset paths\n"
			"  chooser_make_context()                                        — returns a context handle with add_object(path), evaluate(table_path)\n"
			"  chooser_set_debug_target(table_path, name) / chooser_reset_debug_target(table_path)\n"
			"  chooser_get_debug_row(table_path)                             — last selected row index (1-based) or 0 if none\n"
			"  chooser_set_debug_testing(table_path, enable)                 — toggle debug cell pass/fail display\n";

		// ================================================================
		// info()
		// ================================================================
		AssetObj.set_function("info", [WeakCT, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] info -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			sol::table Result = Lua.create_table();

			int32 ColumnCount = CT->ColumnsStructs.Num();
			int32 OutputColumnCount = 0;
			int32 FilterColumnCount = 0;
			int32 FirstOutputColumn = 0;

			for (int32 ColIdx = 0; ColIdx < CT->ColumnsStructs.Num(); ++ColIdx)
			{
				const FInstancedStruct& ColStruct = CT->ColumnsStructs[ColIdx];
				if (!ColStruct.IsValid()) continue;
				if (IsChooserOutputCellColumn(ColStruct))
				{
					OutputColumnCount++;
					if (FirstOutputColumn == 0)
					{
						FirstOutputColumn = ColIdx + 1;
					}
				}
				else if (ColStruct.GetPtr<FChooserColumnBase>())
				{
					FilterColumnCount++;
				}
			}

			int32 RowCount = GetChooserRowCount(CT);

			Result["column_count"] = ColumnCount;
			Result["filter_columns"] = FilterColumnCount;
			Result["output_columns"] = OutputColumnCount;
			Result["first_output_column"] = FirstOutputColumn;
			Result["row_count"] = RowCount;
			Result["has_fallback"] = CT->FallbackResult.IsValid();
			Result["result_type"] = GetResultTypeName(CT->ResultType);

			if (CT->OutputObjectType)
			{
				Result["output_class"] = TCHAR_TO_UTF8(*CT->OutputObjectType->GetName());
			}

			AddChooserEditorSummary(Result, CT);

			// Context data
			TConstArrayView<FInstancedStruct> CtxData = CT->GetContextData();
			Result["context_count"] = static_cast<int>(CtxData.Num());

			Session.Log(FString::Printf(TEXT("[OK] info() -> %d columns (%d filter, %d output, first output=%d), %d rows"),
				ColumnCount, FilterColumnCount, OutputColumnCount, FirstOutputColumn, RowCount));
			return Result;
		});

		// ================================================================
		// list(type)
		// ================================================================
		AssetObj.set_function("list", [WeakCT, &Session](sol::table Self,
			sol::optional<std::string> TypeOpt, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] list -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			FString FType = NeoLuaStr::ToFStringOpt(TypeOpt, TEXT("all"));

			// ---- list() / list("all") -> info() ----
			if (FType.Equals(TEXT("all"), ESearchCase::IgnoreCase))
			{
				sol::protected_function InfoFn = Self["info"];
				if (InfoFn.valid()) return InfoFn(Self);
				return sol::lua_nil;
			}

			// ---- list("columns") ----
			if (FType.Equals(TEXT("columns"), ESearchCase::IgnoreCase) || FType.Equals(TEXT("column"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lua.create_table();
				int32 ColIdx = 0;

				for (const FInstancedStruct& ColStruct : CT->ColumnsStructs)
				{
					sol::table E = Lua.create_table();
					E["index"] = ColIdx + 1; // 1-based

					if (!ColStruct.IsValid())
					{
						E["type"] = "(invalid)";
						E["is_output"] = false;
						Result[ColIdx + 1] = E;
						ColIdx++;
						continue;
					}

					FString TypeName = ColStruct.GetScriptStruct()
						? ColStruct.GetScriptStruct()->GetName()
						: TEXT("Unknown");
					E["type"] = TCHAR_TO_UTF8(*TypeName);

					if (const FChooserColumnBase* ColBase = ColStruct.GetPtr<FChooserColumnBase>())
					{
						E["is_output"] = IsChooserOutputCellColumn(ColStruct);
						E["engine_has_outputs"] = ColBase->HasOutputs();
						E["has_filters"] = ColBase->HasFilters();
						E["has_costs"] = ColBase->HasCosts();
						AddChooserColumnEditorFields(E, ColBase);
						AddChooserColumnBindingInfo(E, ColStruct);

						// Row value counts for output columns
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
						if (const FOutputObjectColumn* ObjCol = ColStruct.GetPtr<FOutputObjectColumn>())
						{
							E["row_values_count"] = static_cast<int>(ObjCol->RowValues.Num());
						}
						else
#endif // ENGINE_MINOR_VERSION >= 5
						if (const FOutputFloatColumn* FloatCol = ColStruct.GetPtr<FOutputFloatColumn>())
						{
							E["row_values_count"] = static_cast<int>(FloatCol->RowValues.Num());
						}
						else if (const FOutputStructColumn* StructCol = ColStruct.GetPtr<FOutputStructColumn>())
						{
							E["row_values_count"] = static_cast<int>(StructCol->RowValues.Num());
						}
						else if (const FOutputBoolColumn* BoolOutCol = ColStruct.GetPtr<FOutputBoolColumn>())
						{
							E["row_values_count"] = static_cast<int>(BoolOutCol->RowValues.Num());
						}
						else if (const FOutputEnumColumn* EnumOutCol = ColStruct.GetPtr<FOutputEnumColumn>())
						{
							E["row_values_count"] = static_cast<int>(EnumOutCol->RowValues.Num());
						}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
						else if (const FOutputGameplayTagQueryColumn* TagQOutCol = ColStruct.GetPtr<FOutputGameplayTagQueryColumn>())
						{
							E["row_values_count"] = static_cast<int>(TagQOutCol->RowValues.Num());
						}
#endif
						// Row value counts for filter/scoring columns
						else if (const FRandomizeColumn* RandCol = ColStruct.GetPtr<FRandomizeColumn>())
						{
							E["row_values_count"] = static_cast<int>(RandCol->RowValues.Num());
							E["is_randomize"] = true;
						}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
						else if (const FFloatDistanceColumn* FDistCol = ColStruct.GetPtr<FFloatDistanceColumn>())
						{
							E["row_values_count"] = static_cast<int>(FDistCol->RowValues.Num());
						}
#endif
					}

					Result[ColIdx + 1] = E;
					ColIdx++;
				}

				Session.Log(FString::Printf(TEXT("[OK] list(\"columns\") -> %d"), ColIdx));
				return Result;
			}

			// ---- list("rows") ----
			if (FType.Equals(TEXT("rows"), ESearchCase::IgnoreCase) || FType.Equals(TEXT("row"), ESearchCase::IgnoreCase))
			{
				int32 RowCount = GetChooserRowCount(CT);
				sol::table Result = Lua.create_table();

				for (int32 RowIdx = 0; RowIdx < RowCount; ++RowIdx)
				{
					sol::table RowT = Lua.create_table();
					RowT["index"] = RowIdx + 1; // 1-based
					RowT["disabled"] = CT->IsRowDisabled(RowIdx);

					// Collect filter values from filter columns for this row
					sol::table Filters = Lua.create_table();
					int32 FiltIdx = 1;

					for (int32 ColI = 0; ColI < CT->ColumnsStructs.Num(); ++ColI)
					{
						const FInstancedStruct& ColStruct = CT->ColumnsStructs[ColI];
						if (!ColStruct.IsValid()) continue;

						// Bool filter column
						if (const FBoolColumn* BoolCol = ColStruct.GetPtr<FBoolColumn>())
						{
							if (RowIdx < BoolCol->RowValuesWithAny.Num())
							{
								sol::table FE = Lua.create_table();
								FE["column_index"] = ColI + 1;
								FE["column_type"] = "Bool";
								EBoolColumnCellValue CellVal = BoolCol->RowValuesWithAny[RowIdx];
								switch (CellVal)
								{
								case EBoolColumnCellValue::MatchFalse: FE["value"] = "False"; break;
								case EBoolColumnCellValue::MatchTrue:  FE["value"] = "True"; break;
								case EBoolColumnCellValue::MatchAny:   FE["value"] = "Any"; break;
								}
								Filters[FiltIdx++] = FE;
							}
						}
						// Enum filter column
						else if (const FEnumColumn* EnumCol = ColStruct.GetPtr<FEnumColumn>())
						{
							if (RowIdx < EnumCol->RowValues.Num())
							{
								const FChooserEnumRowData& EnumRow = EnumCol->RowValues[RowIdx];
								sol::table FE = Lua.create_table();
								FE["column_index"] = ColI + 1;
								FE["column_type"] = "Enum";
								FE["value"] = static_cast<int>(EnumRow.Value);
								switch (EnumRow.Comparison)
								{
								case EEnumColumnCellValueComparison::MatchEqual:    FE["comparison"] = "Equal"; break;
								case EEnumColumnCellValueComparison::MatchNotEqual: FE["comparison"] = "NotEqual"; break;
								case EEnumColumnCellValueComparison::MatchAny:      FE["comparison"] = "Any"; break;
								default: FE["comparison"] = "Unknown"; break;
								}
								AddChooserEnumValueName(FE, EnumRow);
								Filters[FiltIdx++] = FE;
							}
						}
						// GameplayTag filter column
						else if (const FGameplayTagColumn* TagCol = ColStruct.GetPtr<FGameplayTagColumn>())
						{
							if (RowIdx < TagCol->RowValues.Num())
							{
								const FGameplayTagContainer& Tags = TagCol->RowValues[RowIdx];
								sol::table FE = Lua.create_table();
								FE["column_index"] = ColI + 1;
								FE["column_type"] = "GameplayTag";
								FE["tags"] = TCHAR_TO_UTF8(*Tags.ToStringSimple());
								Filters[FiltIdx++] = FE;
							}
						}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
						// GameplayTagQuery filter column
						else if (const FGameplayTagQueryColumn* TagQCol = ColStruct.GetPtr<FGameplayTagQueryColumn>())
						{
							if (RowIdx < TagQCol->RowValues.Num())
							{
								const FGameplayTagQuery& Query = TagQCol->RowValues[RowIdx];
								sol::table FE = Lua.create_table();
								FE["column_index"] = ColI + 1;
								FE["column_type"] = "GameplayTagQuery";
								FE["description"] = TCHAR_TO_UTF8(*Query.GetDescription());
								// List the tags referenced by the query
								TArray<FGameplayTag> QueryTags;
								Query.GetGameplayTagArray(QueryTags);
								FString TagStr;
								for (int32 Ti = 0; Ti < QueryTags.Num(); ++Ti)
								{
									if (Ti > 0) TagStr += TEXT(",");
									TagStr += QueryTags[Ti].ToString();
								}
								FE["tags"] = TCHAR_TO_UTF8(*TagStr);
								Filters[FiltIdx++] = FE;
							}
						}
#endif
						// Float range filter column
						else if (const FFloatRangeColumn* FloatRangeCol = ColStruct.GetPtr<FFloatRangeColumn>())
						{
							if (RowIdx < FloatRangeCol->RowValues.Num())
							{
								const FChooserFloatRangeRowData& RangeRow = FloatRangeCol->RowValues[RowIdx];
								sol::table FE = Lua.create_table();
								FE["column_index"] = ColI + 1;
								FE["column_type"] = "FloatRange";
								if (!RangeRow.bNoMin) FE["min"] = RangeRow.Min;
								if (!RangeRow.bNoMax) FE["max"] = RangeRow.Max;
								FE["no_min"] = RangeRow.bNoMin;
								FE["no_max"] = RangeRow.bNoMax;
								Filters[FiltIdx++] = FE;
							}
						}
	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
						// FloatDistance scoring column
						else if (const FFloatDistanceColumn* FDistCol = ColStruct.GetPtr<FFloatDistanceColumn>())
						{
							if (RowIdx < FDistCol->RowValues.Num())
							{
								const FChooserFloatDistanceRowData& DistRow = FDistCol->RowValues[RowIdx];
								sol::table FE = Lua.create_table();
								FE["column_index"] = ColI + 1;
								FE["column_type"] = "FloatDistance";
								FE["value"] = DistRow.Value;
								Filters[FiltIdx++] = FE;
							}
						}
#endif
						// Randomize column
						else if (const FRandomizeColumn* RandCol = ColStruct.GetPtr<FRandomizeColumn>())
						{
							if (RowIdx < RandCol->RowValues.Num())
							{
								sol::table FE = Lua.create_table();
								FE["column_index"] = ColI + 1;
								FE["column_type"] = "Randomize";
								FE["weight"] = RandCol->RowValues[RowIdx];
								Filters[FiltIdx++] = FE;
							}
						}
						// MultiEnum filter column (5.5+)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
						else if (const FMultiEnumColumn* MEnumCol = ColStruct.GetPtr<FMultiEnumColumn>())
						{
							if (RowIdx < MEnumCol->RowValues.Num())
							{
								const FChooserMultiEnumRowData& MEnumRow = MEnumCol->RowValues[RowIdx];
								sol::table FE = Lua.create_table();
								FE["column_index"] = ColI + 1;
								FE["column_type"] = "MultiEnum";
								FE["value"] = static_cast<int>(MEnumRow.Value);
								Filters[FiltIdx++] = FE;
							}
						}
#endif
						// Object filter column
						else if (const FObjectColumn* ObjFilterCol = ColStruct.GetPtr<FObjectColumn>())
						{
							if (RowIdx < ObjFilterCol->RowValues.Num())
							{
								const FChooserObjectRowData& ObjRow = ObjFilterCol->RowValues[RowIdx];
								sol::table FE = Lua.create_table();
								FE["column_index"] = ColI + 1;
								FE["column_type"] = "Object";
								switch (ObjRow.Comparison)
								{
								case EObjectColumnCellValueComparison::MatchEqual:    FE["comparison"] = "Equal"; break;
								case EObjectColumnCellValueComparison::MatchNotEqual: FE["comparison"] = "NotEqual"; break;
								case EObjectColumnCellValueComparison::MatchAny:      FE["comparison"] = "Any"; break;
								default: FE["comparison"] = "Unknown"; break;
								}
								if (ObjRow.Value.IsValid())
								{
									FE["value"] = TCHAR_TO_UTF8(*ObjRow.Value.ToSoftObjectPath().ToString());
								}
								Filters[FiltIdx++] = FE;
							}
						}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
						// Object class filter column
						else if (const FObjectClassColumn* ObjClassCol = ColStruct.GetPtr<FObjectClassColumn>())
						{
							if (RowIdx < ObjClassCol->RowValues.Num())
							{
								const FChooserObjectClassRowData& ClassRow = ObjClassCol->RowValues[RowIdx];
								sol::table FE = Lua.create_table();
								FE["column_index"] = ColI + 1;
								FE["column_type"] = "ObjectClass";
								switch (ClassRow.Comparison)
								{
								case EObjectClassColumnCellValueComparison::Equal:         FE["comparison"] = "Equal"; break;
								case EObjectClassColumnCellValueComparison::NotEqual:      FE["comparison"] = "NotEqual"; break;
								case EObjectClassColumnCellValueComparison::SubClassOf:    FE["comparison"] = "SubClassOf"; break;
								case EObjectClassColumnCellValueComparison::NotSubClassOf: FE["comparison"] = "NotSubClassOf"; break;
								case EObjectClassColumnCellValueComparison::Any:           FE["comparison"] = "Any"; break;
								}
								if (ClassRow.Value)
								{
									FE["value"] = TCHAR_TO_UTF8(*ClassRow.Value->GetPathName());
								}
								Filters[FiltIdx++] = FE;
							}
						}
#endif
					}

					RowT["filters"] = Filters;

					// Collect outputs from all output columns for this row
					sol::table Outputs = Lua.create_table();
					int32 OutIdx = 1;

					for (int32 ColI = 0; ColI < CT->ColumnsStructs.Num(); ++ColI)
					{
						const FInstancedStruct& ColStruct = CT->ColumnsStructs[ColI];
						if (!ColStruct.IsValid()) continue;

						// Output Object column — extract asset references
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
						if (const FOutputObjectColumn* ObjCol = ColStruct.GetPtr<FOutputObjectColumn>())
						{
							if (RowIdx < ObjCol->RowValues.Num())
							{
								const FChooserOutputObjectRowData& RowData = ObjCol->RowValues[RowIdx];
								if (RowData.Value.IsValid())
								{
									sol::table OutEntry = Lua.create_table();
									OutEntry["column_index"] = ColI + 1;
									FString ValType = RowData.Value.GetScriptStruct()
										? RowData.Value.GetScriptStruct()->GetName()
										: TEXT("Unknown");
									OutEntry["chooser_type"] = TCHAR_TO_UTF8(*ValType);

									// Try FAssetChooser (hard reference)
									if (const FAssetChooser* AssetChooser = RowData.Value.GetPtr<FAssetChooser>())
									{
										if (UObject* Asset = AssetChooser->Asset.Get())
										{
											OutEntry["asset_name"] = TCHAR_TO_UTF8(*Asset->GetName());
											OutEntry["asset_path"] = TCHAR_TO_UTF8(*Asset->GetPathName());
											OutEntry["asset_class"] = TCHAR_TO_UTF8(*Asset->GetClass()->GetName());
										}
									}
									// Try FSoftAssetChooser (soft reference)
									else if (const FSoftAssetChooser* SoftChooser = RowData.Value.GetPtr<FSoftAssetChooser>())
									{
										FSoftObjectPath SoftPath = SoftChooser->Asset.ToSoftObjectPath();
										if (SoftPath.IsValid())
										{
											OutEntry["asset_name"] = TCHAR_TO_UTF8(*FPackageName::GetShortName(SoftPath.ToString()));
											OutEntry["asset_path"] = TCHAR_TO_UTF8(*SoftPath.ToString());
											OutEntry["is_soft"] = true;
										}
									}
									// Try FNestedChooser
									else if (const FNestedChooser* Nested = RowData.Value.GetPtr<FNestedChooser>())
									{
										if (Nested->Chooser)
										{
											OutEntry["nested_chooser"] = TCHAR_TO_UTF8(*Nested->Chooser->GetName());
											OutEntry["nested_chooser_path"] = TCHAR_TO_UTF8(*Nested->Chooser->GetPathName());
										}
									}
									// Try FEvaluateChooser
									else if (const FEvaluateChooser* EvalChooser = RowData.Value.GetPtr<FEvaluateChooser>())
									{
										if (EvalChooser->Chooser)
										{
											OutEntry["evaluate_chooser"] = TCHAR_TO_UTF8(*EvalChooser->Chooser->GetName());
											OutEntry["evaluate_chooser_path"] = TCHAR_TO_UTF8(*EvalChooser->Chooser->GetPathName());
										}
									}

									Outputs[OutIdx++] = OutEntry;
								}
							}
						}
						// Output Float column
						else
#endif // ENGINE_MINOR_VERSION >= 5
						if (const FOutputFloatColumn* FloatCol = ColStruct.GetPtr<FOutputFloatColumn>())
						{
							if (RowIdx < FloatCol->RowValues.Num())
							{
								sol::table OutEntry = Lua.create_table();
								OutEntry["column_index"] = ColI + 1;
								OutEntry["type"] = "float";
								OutEntry["value"] = FloatCol->RowValues[RowIdx];
								Outputs[OutIdx++] = OutEntry;
							}
						}
						// Output Bool column
						else if (const FOutputBoolColumn* BoolOutCol = ColStruct.GetPtr<FOutputBoolColumn>())
						{
							if (RowIdx < BoolOutCol->RowValues.Num())
							{
								sol::table OutEntry = Lua.create_table();
								OutEntry["column_index"] = ColI + 1;
								OutEntry["type"] = "bool";
								OutEntry["value"] = BoolOutCol->RowValues[RowIdx];
								Outputs[OutIdx++] = OutEntry;
							}
						}
						// Output Enum column
						else if (const FOutputEnumColumn* EnumOutCol = ColStruct.GetPtr<FOutputEnumColumn>())
						{
							if (RowIdx < EnumOutCol->RowValues.Num())
							{
								const FChooserOutputEnumRowData& EnumRow = EnumOutCol->RowValues[RowIdx];
								sol::table OutEntry = Lua.create_table();
								OutEntry["column_index"] = ColI + 1;
								OutEntry["type"] = "enum";
								OutEntry["value"] = static_cast<int>(EnumRow.Value);
#if WITH_EDITORONLY_DATA
								if (!EnumRow.ValueName.IsNone())
								{
									OutEntry["value_name"] = TCHAR_TO_UTF8(*EnumRow.ValueName.ToString());
								}
#endif
								Outputs[OutIdx++] = OutEntry;
							}
						}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
						// Output GameplayTagQuery column
						else if (const FOutputGameplayTagQueryColumn* TagQOutCol = ColStruct.GetPtr<FOutputGameplayTagQueryColumn>())
						{
							if (RowIdx < TagQOutCol->RowValues.Num())
							{
								const FGameplayTagQuery& Query = TagQOutCol->RowValues[RowIdx];
								sol::table OutEntry = Lua.create_table();
								OutEntry["column_index"] = ColI + 1;
								OutEntry["type"] = "gameplay_tag_query";
								OutEntry["description"] = TCHAR_TO_UTF8(*Query.GetDescription());
								TArray<FGameplayTag> QueryTags;
								Query.GetGameplayTagArray(QueryTags);
								FString TagStr;
								for (int32 Ti = 0; Ti < QueryTags.Num(); ++Ti)
								{
									if (Ti > 0) TagStr += TEXT(",");
									TagStr += QueryTags[Ti].ToString();
								}
								OutEntry["tags"] = TCHAR_TO_UTF8(*TagStr);
								Outputs[OutIdx++] = OutEntry;
							}
						}
#endif
						// Output Struct column
						else if (const FOutputStructColumn* StructCol = ColStruct.GetPtr<FOutputStructColumn>())
						{
							if (RowIdx < StructCol->RowValues.Num())
							{
								const FInstancedStruct& StructVal = StructCol->RowValues[RowIdx];
								sol::table OutEntry = Lua.create_table();
								OutEntry["column_index"] = ColI + 1;
								OutEntry["type"] = "struct";
								if (StructVal.IsValid() && StructVal.GetScriptStruct())
								{
									OutEntry["struct_type"] = TCHAR_TO_UTF8(*StructVal.GetScriptStruct()->GetName());
								}
								Outputs[OutIdx++] = OutEntry;
							}
						}
					}

					RowT["outputs"] = Outputs;

					// Also include the result struct info if available
					AddChooserRowResultInfo(RowT, CT, RowIdx);

					Result[RowIdx + 1] = RowT;
				}

				Session.Log(FString::Printf(TEXT("[OK] list(\"rows\") -> %d"), RowCount));
				return Result;
			}

			// ---- list("references") — flat list of all asset references ----
			if (FType.Equals(TEXT("references"), ESearchCase::IgnoreCase) || FType.Equals(TEXT("reference"), ESearchCase::IgnoreCase))
			{
				sol::table Result = Lua.create_table();
				int32 RefIdx = 1;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
				for (const FInstancedStruct& ColStruct : CT->ColumnsStructs)
				{
					if (!ColStruct.IsValid()) continue;
					const FOutputObjectColumn* ObjCol = ColStruct.GetPtr<FOutputObjectColumn>();
					if (!ObjCol) continue;

					for (int32 RowIdx = 0; RowIdx < ObjCol->RowValues.Num(); ++RowIdx)
					{
						const FChooserOutputObjectRowData& RowData = ObjCol->RowValues[RowIdx];
						if (!RowData.Value.IsValid()) continue;

						if (const FAssetChooser* AssetChooser = RowData.Value.GetPtr<FAssetChooser>())
						{
							if (UObject* Asset = AssetChooser->Asset.Get())
							{
								sol::table Ref = Lua.create_table();
								Ref["row"] = RowIdx + 1;
								Ref["asset_name"] = TCHAR_TO_UTF8(*Asset->GetName());
								Ref["asset_path"] = TCHAR_TO_UTF8(*Asset->GetPathName());
								Ref["asset_class"] = TCHAR_TO_UTF8(*Asset->GetClass()->GetName());
								Ref["is_soft"] = false;
								Result[RefIdx++] = Ref;
							}
						}
						else if (const FSoftAssetChooser* SoftChooser = RowData.Value.GetPtr<FSoftAssetChooser>())
						{
							FSoftObjectPath SoftPath = SoftChooser->Asset.ToSoftObjectPath();
							if (SoftPath.IsValid())
							{
								sol::table Ref = Lua.create_table();
								Ref["row"] = RowIdx + 1;
								Ref["asset_name"] = TCHAR_TO_UTF8(*FPackageName::GetShortName(SoftPath.ToString()));
								Ref["asset_path"] = TCHAR_TO_UTF8(*SoftPath.ToString());
								Ref["is_soft"] = true;
								Result[RefIdx++] = Ref;
							}
						}
					}

					// Also include fallback reference
					if (ObjCol->FallbackValue.Value.IsValid())
					{
						if (const FAssetChooser* AssetChooser = ObjCol->FallbackValue.Value.GetPtr<FAssetChooser>())
						{
							if (UObject* Asset = AssetChooser->Asset.Get())
							{
								sol::table Ref = Lua.create_table();
								Ref["row"] = "fallback";
								Ref["asset_name"] = TCHAR_TO_UTF8(*Asset->GetName());
								Ref["asset_path"] = TCHAR_TO_UTF8(*Asset->GetPathName());
								Ref["asset_class"] = TCHAR_TO_UTF8(*Asset->GetClass()->GetName());
								Ref["is_fallback"] = true;
								Result[RefIdx++] = Ref;
							}
						}
					}
				}
#endif // ENGINE_MINOR_VERSION >= 5

				Session.Log(FString::Printf(TEXT("[OK] list(\"references\") -> %d"), RefIdx - 1));
				return Result;
			}

			// ---- list("context") — context parameter definitions ----
			if (FType.Equals(TEXT("context"), ESearchCase::IgnoreCase))
			{
				TConstArrayView<FInstancedStruct> CtxData = CT->GetContextData();
				sol::table Result = Lua.create_table();

				for (int32 i = 0; i < CtxData.Num(); ++i)
				{
					sol::table E = Lua.create_table();
					E["index"] = i + 1;

					if (CtxData[i].IsValid() && CtxData[i].GetScriptStruct())
					{
						E["type"] = TCHAR_TO_UTF8(*CtxData[i].GetScriptStruct()->GetName());
						if (const FContextObjectTypeClass* ClassContext = CtxData[i].GetPtr<FContextObjectTypeClass>())
						{
							E["kind"] = "class";
							E["direction"] = GetChooserContextDirectionName(ClassContext->Direction);
							if (ClassContext->Class)
							{
								E["class"] = TCHAR_TO_UTF8(*ClassContext->Class->GetPathName());
								E["class_name"] = TCHAR_TO_UTF8(*ClassContext->Class->GetName());
							}
						}
						else if (const FContextObjectTypeStruct* StructContext = CtxData[i].GetPtr<FContextObjectTypeStruct>())
						{
							E["kind"] = "struct";
							E["direction"] = GetChooserContextDirectionName(StructContext->Direction);
							if (StructContext->Struct)
							{
								E["struct"] = TCHAR_TO_UTF8(*StructContext->Struct->GetPathName());
								E["struct_name"] = TCHAR_TO_UTF8(*StructContext->Struct->GetName());
							}
						}
					}
					else
					{
						E["type"] = "(invalid)";
					}

					Result[i + 1] = E;
				}

				Session.Log(FString::Printf(TEXT("[OK] list(\"context\") -> %d"), CtxData.Num()));
				return Result;
			}

			Session.Log(FString::Printf(TEXT("[FAIL] list(\"%s\") -> unknown type. Valid: columns, rows, references, context"), *FType));
			return sol::lua_nil;
		});

#if WITH_EDITOR
		// ================================================================
		// add_context(opts) — add a class or struct context parameter
		// ================================================================
		AssetObj.set_function("add_context", [WeakCT, &Session](sol::table /*self*/,
			sol::table Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] add_context -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}

			FString Kind;
			TryGetChooserString(Opts, "kind", Kind);
			if (Kind.IsEmpty())
			{
				if (Opts["class"].valid() || Opts["class_path"].valid())
				{
					Kind = TEXT("class");
				}
				else if (Opts["struct"].valid() || Opts["struct_path"].valid())
				{
					Kind = TEXT("struct");
				}
			}

			FString DirectionStr;
			TryGetChooserString(Opts, "direction", DirectionStr);
			const EContextObjectDirection Direction = ParseChooserContextDirection(DirectionStr);

			FInstancedStruct NewContext;
			if (Kind.Equals(TEXT("class"), ESearchCase::IgnoreCase) || Kind.Equals(TEXT("object"), ESearchCase::IgnoreCase))
			{
				FString ClassPath;
				if (!TryGetChooserString(Opts, "class", ClassPath))
				{
					TryGetChooserString(Opts, "class_path", ClassPath);
				}

				UClass* Class = nullptr;
				if (!TryResolveChooserClass(ClassPath, Class))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add_context -> class not found: %s"), *ClassPath));
					return sol::lua_nil;
				}

				NewContext.InitializeAs<FContextObjectTypeClass>();
				FContextObjectTypeClass& Context = NewContext.GetMutable<FContextObjectTypeClass>();
				Context.Class = Class;
				Context.Direction = Direction;
			}
			else if (Kind.Equals(TEXT("struct"), ESearchCase::IgnoreCase))
			{
				FString StructPath;
				if (!TryGetChooserString(Opts, "struct", StructPath))
				{
					TryGetChooserString(Opts, "struct_path", StructPath);
				}

				UScriptStruct* Struct = nullptr;
				if (!TryResolveChooserStruct(StructPath, Struct))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] add_context -> struct not found: %s"), *StructPath));
					return sol::lua_nil;
				}

				NewContext.InitializeAs<FContextObjectTypeStruct>();
				FContextObjectTypeStruct& Context = NewContext.GetMutable<FContextObjectTypeStruct>();
				Context.Struct = Struct;
				Context.Direction = Direction;
			}
			else
			{
				Session.Log(TEXT("[FAIL] add_context -> opts.kind must be class or struct"));
				return sol::lua_nil;
			}

			int32 InsertAt = CT->ContextData.Num();
			const int OptIdx = Opts.get_or("index", 0);
			if (OptIdx > 0)
			{
				InsertAt = FMath::Clamp(OptIdx - 1, 0, CT->ContextData.Num());
			}

			CT->Modify();
			CT->ContextData.Insert(MoveTemp(NewContext), InsertAt);
			OffsetChooserBindingsAfterContextInsert(CT, InsertAt);
			CT->Compile(true);
			CT->MarkPackageDirty();

			Session.Log(FString::Printf(TEXT("[OK] add_context -> index %d, now %d context parameters"), InsertAt + 1, CT->ContextData.Num()));
			return sol::make_object(Lua, InsertAt + 1);
		});

		// ================================================================
		// add_column(type, opts?) — add a column by type name
		// ================================================================
		AssetObj.set_function("add_column", [WeakCT, &Session](sol::table /*self*/,
			const std::string& TypeStr, sol::optional<sol::table> OptsOpt, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] add_column -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			FString FTypeName = NeoLuaStr::ToFString(TypeStr);

			const UScriptStruct* ColStruct = ResolveChooserColumnStruct(FTypeName);
			if (!ColStruct)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] add_column(\"%s\") -> unknown column type. Valid: Bool, Enum, GameplayTag, GameplayTagQuery, FloatRange, FloatDistance, Randomize, MultiEnum, Object, ObjectClass, OutputObject, OutputFloat, OutputStruct, OutputBool, OutputEnum, OutputGameplayTagQuery"), *FTypeName));
				return sol::lua_nil;
			}

			int32 CurrentRows = GetChooserRowCount(CT);
			int32 InsertAt = CT->ColumnsStructs.Num(); // default: append

			if (OptsOpt.has_value())
			{
				sol::table Opts = OptsOpt.value();
				int OptIdx = Opts.get_or("index", 0);
				if (OptIdx > 0)
				{
					InsertAt = FMath::Clamp(OptIdx - 1, 0, CT->ColumnsStructs.Num());
				}
			}

			CT->Modify();

			// Create the new column via FInstancedStruct
			FInstancedStruct NewCol;
			NewCol.InitializeAs(ColStruct);

			// Initialize the column for this chooser and set row count to match existing rows
			if (FChooserColumnBase* ColBase = NewCol.GetMutablePtr<FChooserColumnBase>())
			{
	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
				ColBase->Initialize(CT);
#endif
				if (CurrentRows > 0)
				{
					ColBase->SetNumRows(CurrentRows);
				}
			}

			if (OptsOpt.has_value())
			{
				sol::table Opts = OptsOpt.value();
				sol::object BindingObj = Opts["binding"];
				if (BindingObj.valid() && BindingObj != sol::lua_nil)
				{
					FChooserLuaBindingSpec BindingSpec;
					FString BindingError;
					if (!ParseChooserBindingSpec(Opts, BindingSpec, BindingError) ||
						!ApplyChooserColumnBinding(NewCol, CT, BindingSpec, BindingError))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] add_column(\"%s\") binding -> %s"), *FTypeName, *BindingError));
						return sol::lua_nil;
					}
				}
			}

			CT->ColumnsStructs.Insert(MoveTemp(NewCol), InsertAt);

			CT->Compile(true);
			CT->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] add_column(\"%s\") at index %d -> now %d columns"),
				*FTypeName, InsertAt + 1, CT->ColumnsStructs.Num()));
			return sol::make_object(Lua, InsertAt + 1); // return 1-based index
		});

		// ================================================================
		// bind_column_input(column, binding) — rebind a column in place
		// ================================================================
		AssetObj.set_function("bind_column_input", [WeakCT, &Session](sol::table /*self*/,
			int Index, sol::table Binding, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] bind_column_input -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}

			const int32 ColIdx = Index - 1;
			if (!CT->ColumnsStructs.IsValidIndex(ColIdx))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] bind_column_input(%d) -> column out of range; valid columns are 1..%d (run list(\"columns\"))"),
					Index, CT->ColumnsStructs.Num()));
				return sol::lua_nil;
			}

			FChooserLuaBindingSpec BindingSpec;
			FString Error;
			if (!ParseChooserBindingSpec(Binding, BindingSpec, Error))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] bind_column_input(%d) -> %s"), Index, *Error));
				return sol::lua_nil;
			}

			CT->Modify();
			if (!ApplyChooserColumnBinding(CT->ColumnsStructs[ColIdx], CT, BindingSpec, Error))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] bind_column_input(%d) -> %s"), Index, *Error));
				return sol::lua_nil;
			}

			CT->Compile(true);
			CT->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] bind_column_input(%d) -> context %d"), Index, BindingSpec.ContextIndex + 1));
			return sol::make_object(Lua, true);
		});

		// ================================================================
		// remove_column(index) — remove a column by 1-based index
		// ================================================================
		AssetObj.set_function("remove_column", [WeakCT, &Session](sol::table /*self*/,
			int Index, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] remove_column -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			int32 Idx = Index - 1;

			if (Idx < 0 || Idx >= CT->ColumnsStructs.Num())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] remove_column(%d) -> index out of range; valid columns are 1..%d (run list(\"columns\"))"),
					Index, CT->ColumnsStructs.Num()));
				return sol::lua_nil;
			}

			CT->Modify();
			CT->ColumnsStructs.RemoveAt(Idx);
			CT->MarkPackageDirty();

			Session.Log(FString::Printf(TEXT("[OK] remove_column(%d) -> now %d columns"),
				Index, CT->ColumnsStructs.Num()));
			return sol::make_object(Lua, true);
		});

		// ================================================================
		// set_cell(row, column, opts) — set a filter cell value
		// ================================================================
		AssetObj.set_function("set_cell", [WeakCT, &Session](sol::table /*self*/,
			int RowIndex, int ColIndex, sol::table Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] set_cell -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			int32 RowIdx = RowIndex - 1;
			int32 ColIdx = ColIndex - 1;

			if (RowIdx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_cell(%d, %d) -> row index must be >= 1"), RowIndex, ColIndex));
				return sol::lua_nil;
			}

			if (!ValidateChooserAuthoringRow(CT, RowIndex, TEXT("set_cell"), Session, ColIndex))
			{
				return sol::lua_nil;
			}

			if (ColIdx < 0 || ColIdx >= CT->ColumnsStructs.Num())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_cell(%d, %d) -> column %d out of range; valid columns are 1..%d (run list(\"columns\"))"),
					RowIndex, ColIndex, ColIndex, CT->ColumnsStructs.Num()));
				return sol::lua_nil;
			}

			FInstancedStruct& ColStruct = CT->ColumnsStructs[ColIdx];
			if (!ColStruct.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_cell(%d, %d) -> column %d is empty/invalid; remove_column() and add_column() with a valid filter type"),
					RowIndex, ColIndex, ColIndex));
				return sol::lua_nil;
			}

			CT->Modify();

			// ---- Bool column ----
			if (FBoolColumn* BoolCol = ColStruct.GetMutablePtr<FBoolColumn>())
			{
				// Expand if needed
				while (BoolCol->RowValuesWithAny.Num() <= RowIdx)
				{
					BoolCol->RowValuesWithAny.Add(EBoolColumnCellValue::MatchAny);
				}

				std::string ValStr = Opts.get_or<std::string>("value", "Any");
				FString FVal = NeoLuaStr::ToFString(ValStr);
				if (FVal.Equals(TEXT("True"), ESearchCase::IgnoreCase))
					BoolCol->RowValuesWithAny[RowIdx] = EBoolColumnCellValue::MatchTrue;
				else if (FVal.Equals(TEXT("False"), ESearchCase::IgnoreCase))
					BoolCol->RowValuesWithAny[RowIdx] = EBoolColumnCellValue::MatchFalse;
				else
					BoolCol->RowValuesWithAny[RowIdx] = EBoolColumnCellValue::MatchAny;

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_cell(%d, %d) -> Bool = %s"), RowIndex, ColIndex, *FVal));
				return sol::make_object(Lua, true);
			}

			// ---- Enum column ----
			if (FEnumColumn* EnumCol = ColStruct.GetMutablePtr<FEnumColumn>())
			{
				while (EnumCol->RowValues.Num() <= RowIdx)
				{
					EnumCol->RowValues.Add(FChooserEnumRowData());
				}

				FChooserEnumRowData& RowData = EnumCol->RowValues[RowIdx];
				RowData.Value = static_cast<uint8>(Opts.get_or("value", 0));

				std::string CompStr = Opts.get_or<std::string>("comparison", "Equal");
				FString FComp = NeoLuaStr::ToFString(CompStr);
				if (FComp.Equals(TEXT("NotEqual"), ESearchCase::IgnoreCase))
					RowData.Comparison = EEnumColumnCellValueComparison::MatchNotEqual;
				else if (FComp.Equals(TEXT("Any"), ESearchCase::IgnoreCase))
					RowData.Comparison = EEnumColumnCellValueComparison::MatchAny;
				else
					RowData.Comparison = EEnumColumnCellValueComparison::MatchEqual;

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_cell(%d, %d) -> Enum = %d (%s)"),
					RowIndex, ColIndex, RowData.Value, *FComp));
				return sol::make_object(Lua, true);
			}

			// ---- GameplayTag column ----
			if (FGameplayTagColumn* TagCol = ColStruct.GetMutablePtr<FGameplayTagColumn>())
			{
				while (TagCol->RowValues.Num() <= RowIdx)
				{
					TagCol->RowValues.Add(FGameplayTagContainer());
				}

				std::string TagStr = Opts.get_or<std::string>("tags", "");
				FString FTags = NeoLuaStr::ToFString(TagStr);
				FGameplayTagContainer NewTags;
				TArray<FString> TagArray;
				FTags.ParseIntoArray(TagArray, TEXT(","));
				for (const FString& TagName : TagArray)
				{
					FString Trimmed = TagName.TrimStartAndEnd();
					FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Trimmed), false);
					if (Tag.IsValid())
					{
						NewTags.AddTag(Tag);
					}
				}
				TagCol->RowValues[RowIdx] = NewTags;

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_cell(%d, %d) -> GameplayTag = %s"),
					RowIndex, ColIndex, *NewTags.ToStringSimple()));
				return sol::make_object(Lua, true);
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			// ---- GameplayTagQuery column ----
			if (FGameplayTagQueryColumn* TagQCol = ColStruct.GetMutablePtr<FGameplayTagQueryColumn>())
			{
				while (TagQCol->RowValues.Num() <= RowIdx)
				{
					TagQCol->RowValues.Add(FGameplayTagQuery());
				}

				// Parse tags
				std::string TagStr = Opts.get_or<std::string>("tags", "");
				FString FTags = NeoLuaStr::ToFString(TagStr);
				FGameplayTagContainer TagContainer;
				TArray<FString> TagArray;
				FTags.ParseIntoArray(TagArray, TEXT(","));
				for (const FString& TagName : TagArray)
				{
					FString Trimmed = TagName.TrimStartAndEnd();
					FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Trimmed), false);
					if (Tag.IsValid())
					{
						TagContainer.AddTag(Tag);
					}
				}

				// Build query based on match_type
				std::string MatchType = Opts.get_or<std::string>("match_type", "any");
				FString FMatchType = NeoLuaStr::ToFString(MatchType);

				FGameplayTagQuery NewQuery;
				if (FMatchType.Equals(TEXT("all"), ESearchCase::IgnoreCase))
					NewQuery = FGameplayTagQuery::MakeQuery_MatchAllTags(TagContainer);
				else if (FMatchType.Equals(TEXT("none"), ESearchCase::IgnoreCase))
					NewQuery = FGameplayTagQuery::MakeQuery_MatchNoTags(TagContainer);
				else
					NewQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(TagContainer);

				TagQCol->RowValues[RowIdx] = MoveTemp(NewQuery);

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_cell(%d, %d) -> GameplayTagQuery (%s) = %s"),
					RowIndex, ColIndex, *FMatchType, *FTags));
				return sol::make_object(Lua, true);
			}
#endif

			// ---- FloatRange column ----
			if (FFloatRangeColumn* FRangeCol = ColStruct.GetMutablePtr<FFloatRangeColumn>())
			{
				while (FRangeCol->RowValues.Num() <= RowIdx)
				{
					FRangeCol->RowValues.Add(FChooserFloatRangeRowData());
				}

				FChooserFloatRangeRowData& RangeRow = FRangeCol->RowValues[RowIdx];

				sol::optional<double> MinVal = Opts.get<sol::optional<double>>("min");
				sol::optional<double> MaxVal = Opts.get<sol::optional<double>>("max");

				if (MinVal.has_value())
					RangeRow.Min = static_cast<float>(MinVal.value());
				if (MaxVal.has_value())
					RangeRow.Max = static_cast<float>(MaxVal.value());

				RangeRow.bNoMin = Opts.get_or("no_min", false);
				RangeRow.bNoMax = Opts.get_or("no_max", false);

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_cell(%d, %d) -> FloatRange [%s%g, %g%s]"),
					RowIndex, ColIndex,
					RangeRow.bNoMin ? TEXT("-inf,") : TEXT(""),
					RangeRow.bNoMin ? 0.0f : RangeRow.Min,
					RangeRow.bNoMax ? 0.0f : RangeRow.Max,
					RangeRow.bNoMax ? TEXT(",+inf") : TEXT("")));
				return sol::make_object(Lua, true);
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			// ---- FloatDistance column ----
			if (FFloatDistanceColumn* FDistCol = ColStruct.GetMutablePtr<FFloatDistanceColumn>())
			{
				while (FDistCol->RowValues.Num() <= RowIdx)
				{
					FDistCol->RowValues.Add(FChooserFloatDistanceRowData());
				}

				FDistCol->RowValues[RowIdx].Value = static_cast<float>(Opts.get_or("value", 0.0));

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_cell(%d, %d) -> FloatDistance = %g"),
					RowIndex, ColIndex, FDistCol->RowValues[RowIdx].Value));
				return sol::make_object(Lua, true);
			}
#endif

			// ---- Randomize column ----
			if (FRandomizeColumn* RandCol = ColStruct.GetMutablePtr<FRandomizeColumn>())
			{
				while (RandCol->RowValues.Num() <= RowIdx)
				{
					RandCol->RowValues.Add(1.0f);
				}

				RandCol->RowValues[RowIdx] = static_cast<float>(Opts.get_or("weight", 1.0));

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_cell(%d, %d) -> Randomize weight = %g"),
					RowIndex, ColIndex, RandCol->RowValues[RowIdx]));
				return sol::make_object(Lua, true);
			}

			// ---- MultiEnum column ---- (5.5+)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			if (FMultiEnumColumn* MEnumCol = ColStruct.GetMutablePtr<FMultiEnumColumn>())
			{
				while (MEnumCol->RowValues.Num() <= RowIdx)
				{
					MEnumCol->RowValues.Add(FChooserMultiEnumRowData());
				}

				MEnumCol->RowValues[RowIdx].Value = static_cast<uint32>(Opts.get_or("value", 0));

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_cell(%d, %d) -> MultiEnum bitmask = %u"),
					RowIndex, ColIndex, MEnumCol->RowValues[RowIdx].Value));
				return sol::make_object(Lua, true);
			}
#endif

			// ---- Object filter column ----
			if (FObjectColumn* ObjCol = ColStruct.GetMutablePtr<FObjectColumn>())
			{
				while (ObjCol->RowValues.Num() <= RowIdx)
				{
					ObjCol->RowValues.Add(FChooserObjectRowData());
				}

				FChooserObjectRowData& RowData = ObjCol->RowValues[RowIdx];

				std::string CompStr = Opts.get_or<std::string>("comparison", "Equal");
				FString FComp = NeoLuaStr::ToFString(CompStr);
				if (FComp.Equals(TEXT("NotEqual"), ESearchCase::IgnoreCase))
					RowData.Comparison = EObjectColumnCellValueComparison::MatchNotEqual;
				else if (FComp.Equals(TEXT("Any"), ESearchCase::IgnoreCase))
					RowData.Comparison = EObjectColumnCellValueComparison::MatchAny;
				else
					RowData.Comparison = EObjectColumnCellValueComparison::MatchEqual;

				std::string AssetPath = Opts.get_or<std::string>("asset_path", "");
				if (!AssetPath.empty())
				{
					FString FAssetPath = NeoLuaStr::ToFString(AssetPath);
					if (!FAssetPath.StartsWith(TEXT("/")))
						FAssetPath = TEXT("/Game/") + FAssetPath;
					RowData.Value = TSoftObjectPtr<UObject>(FSoftObjectPath(FAssetPath));
				}

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_cell(%d, %d) -> Object (%s)"),
					RowIndex, ColIndex, *FComp));
				return sol::make_object(Lua, true);
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			// ---- ObjectClass filter column ----
			if (FObjectClassColumn* ObjClassCol = ColStruct.GetMutablePtr<FObjectClassColumn>())
			{
				while (ObjClassCol->RowValues.Num() <= RowIdx)
				{
					ObjClassCol->RowValues.Add(FChooserObjectClassRowData());
				}

				FChooserObjectClassRowData& RowData = ObjClassCol->RowValues[RowIdx];

				std::string CompStr = Opts.get_or<std::string>("comparison", "SubClassOf");
				FString FComp = NeoLuaStr::ToFString(CompStr);
				if (FComp.Equals(TEXT("Equal"), ESearchCase::IgnoreCase))
					RowData.Comparison = EObjectClassColumnCellValueComparison::Equal;
				else if (FComp.Equals(TEXT("NotEqual"), ESearchCase::IgnoreCase))
					RowData.Comparison = EObjectClassColumnCellValueComparison::NotEqual;
				else if (FComp.Equals(TEXT("NotSubClassOf"), ESearchCase::IgnoreCase))
					RowData.Comparison = EObjectClassColumnCellValueComparison::NotSubClassOf;
				else if (FComp.Equals(TEXT("Any"), ESearchCase::IgnoreCase))
					RowData.Comparison = EObjectClassColumnCellValueComparison::Any;
				else
					RowData.Comparison = EObjectClassColumnCellValueComparison::SubClassOf;

				std::string ClassPath = Opts.get_or<std::string>("class_path", "");
				if (!ClassPath.empty())
				{
					FString FClassPath = NeoLuaStr::ToFString(ClassPath);
					UClass* FoundClass = FindObject<UClass>(nullptr, *FClassPath);
					if (!FoundClass)
					{
						FoundClass = LoadObject<UClass>(nullptr, *FClassPath);
					}
					RowData.Value = FoundClass; // nullptr is valid (clears the class)
				}

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_cell(%d, %d) -> ObjectClass (%s)"),
					RowIndex, ColIndex, *FComp));
				return sol::make_object(Lua, true);
			}
#endif

			// Not a recognized filter column type
			FString ColTypeName = ColStruct.GetScriptStruct()
				? ColStruct.GetScriptStruct()->GetName()
				: TEXT("Unknown");
			Session.Log(FString::Printf(TEXT("[FAIL] set_cell(%d, %d) -> column type %s is not a filter column; use set_output_cell for output columns. Valid filter types: Bool, Enum, GameplayTag, GameplayTagQuery, FloatRange, FloatDistance, Randomize, MultiEnum, Object, ObjectClass"),
				RowIndex, ColIndex, *ColTypeName));
			return sol::lua_nil;
		});

		// ================================================================
		// set_output_cell(row, column, opts) — set an output cell value
		// ================================================================
		AssetObj.set_function("set_output_cell", [WeakCT, &Session](sol::table /*self*/,
			int RowIndex, int ColIndex, sol::table Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] set_output_cell -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			int32 RowIdx = RowIndex - 1;
			int32 ColIdx = ColIndex - 1;

			if (RowIdx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_output_cell(%d, %d) -> row index must be >= 1"), RowIndex, ColIndex));
				return sol::lua_nil;
			}

			if (!ValidateChooserAuthoringRow(CT, RowIndex, TEXT("set_output_cell"), Session, ColIndex))
			{
				return sol::lua_nil;
			}

			if (ColIdx < 0 || ColIdx >= CT->ColumnsStructs.Num())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_output_cell(%d, %d) -> column %d out of range; valid columns are 1..%d (run list(\"columns\"))"),
					RowIndex, ColIndex, ColIndex, CT->ColumnsStructs.Num()));
				return sol::lua_nil;
			}

			FInstancedStruct& ColStruct = CT->ColumnsStructs[ColIdx];
			if (!ColStruct.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_output_cell(%d, %d) -> column %d is empty/invalid; remove_column() and add_column() with a valid output type"),
					RowIndex, ColIndex, ColIndex));
				return sol::lua_nil;
			}

			CT->Modify();

			// ---- Output Object column ----
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			if (FOutputObjectColumn* ObjCol = ColStruct.GetMutablePtr<FOutputObjectColumn>())
			{
				while (ObjCol->RowValues.Num() <= RowIdx)
				{
					ObjCol->RowValues.Add(FChooserOutputObjectRowData());
				}

				// Accept either the legacy "asset_path" shape or the same { kind = ... } table
				// accepted by set_row_result, so OutputObject cells can hold soft/class/evaluate results too.
				FInstancedStruct NewResult;
				FString Summary;
				FString Error;
				sol::object AsObject = sol::object(Opts);
				if (!BuildResultInstancedStruct(AsObject, CT, NewResult, Summary, Error))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_output_cell(%d, %d) -> %s"),
						RowIndex, ColIndex, *Error));
					return sol::lua_nil;
				}

				ObjCol->RowValues[RowIdx].Value = MoveTemp(NewResult);

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_output_cell(%d, %d) -> OutputObject %s"),
					RowIndex, ColIndex, *Summary));
				return sol::make_object(Lua, true);
			}
#endif // ENGINE_MINOR_VERSION >= 5

			// ---- Output Struct column ----
			if (FOutputStructColumn* StructCol = ColStruct.GetMutablePtr<FOutputStructColumn>())
			{
				while (StructCol->RowValues.Num() <= RowIdx)
				{
					StructCol->RowValues.Add(FInstancedStruct());
				}

				FInstancedStruct& Slot = StructCol->RowValues[RowIdx];
#if WITH_EDITORONLY_DATA
				if (!Slot.IsValid() && StructCol->DefaultRowValue.IsValid())
				{
					Slot.InitializeAs(StructCol->DefaultRowValue.GetScriptStruct());
				}
#endif
				if (!Slot.IsValid() || !Slot.GetScriptStruct())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_output_cell(%d, %d) -> OutputStruct has no struct type configured; bind the column to a struct property first (edit the asset in the chooser editor)"),
						RowIndex, ColIndex));
					return sol::lua_nil;
				}

				FString Error;
				UScriptStruct* SlotStruct = const_cast<UScriptStruct*>(Slot.GetScriptStruct());
				if (!NeoLuaProperty::ApplyValueToStructMemory(SlotStruct, Slot.GetMutableMemory(), sol::object(Opts), CT, Error))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_output_cell(%d, %d) -> %s"),
						RowIndex, ColIndex, *Error));
					return sol::lua_nil;
				}

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_output_cell(%d, %d) -> OutputStruct (%s)"),
					RowIndex, ColIndex, *SlotStruct->GetName()));
				return sol::make_object(Lua, true);
			}

			// ---- Output Float column ----
			if (FOutputFloatColumn* FloatCol = ColStruct.GetMutablePtr<FOutputFloatColumn>())
			{
				while (FloatCol->RowValues.Num() <= RowIdx)
				{
					FloatCol->RowValues.Add(0.0);
				}

				FloatCol->RowValues[RowIdx] = Opts.get_or("value", 0.0);

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_output_cell(%d, %d) -> OutputFloat = %g"),
					RowIndex, ColIndex, FloatCol->RowValues[RowIdx]));
				return sol::make_object(Lua, true);
			}

			// ---- Output Bool column ----
			if (FOutputBoolColumn* BoolCol = ColStruct.GetMutablePtr<FOutputBoolColumn>())
			{
				while (BoolCol->RowValues.Num() <= RowIdx)
				{
					BoolCol->RowValues.Add(false);
				}

				BoolCol->RowValues[RowIdx] = Opts.get_or("value", false);

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_output_cell(%d, %d) -> OutputBool = %s"),
					RowIndex, ColIndex, BoolCol->RowValues[RowIdx] ? TEXT("true") : TEXT("false")));
				return sol::make_object(Lua, true);
			}

			// ---- Output Enum column ----
			if (FOutputEnumColumn* EnumCol = ColStruct.GetMutablePtr<FOutputEnumColumn>())
			{
				while (EnumCol->RowValues.Num() <= RowIdx)
				{
					EnumCol->RowValues.Add(FChooserOutputEnumRowData());
				}

				EnumCol->RowValues[RowIdx].Value = static_cast<uint8>(Opts.get_or("value", 0));

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_output_cell(%d, %d) -> OutputEnum = %d"),
					RowIndex, ColIndex, EnumCol->RowValues[RowIdx].Value));
				return sol::make_object(Lua, true);
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			// ---- Output GameplayTagQuery column ----
			if (FOutputGameplayTagQueryColumn* TagQCol = ColStruct.GetMutablePtr<FOutputGameplayTagQueryColumn>())
			{
				while (TagQCol->RowValues.Num() <= RowIdx)
				{
					TagQCol->RowValues.Add(FGameplayTagQuery());
				}

				// Parse tags
				std::string TagStr = Opts.get_or<std::string>("tags", "");
				FString FTags = NeoLuaStr::ToFString(TagStr);
				FGameplayTagContainer TagContainer;
				TArray<FString> TagArray;
				FTags.ParseIntoArray(TagArray, TEXT(","));
				for (const FString& TagName : TagArray)
				{
					FString Trimmed = TagName.TrimStartAndEnd();
					FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Trimmed), false);
					if (Tag.IsValid())
					{
						TagContainer.AddTag(Tag);
					}
				}

				// Build query based on match_type
				std::string MatchType = Opts.get_or<std::string>("match_type", "any");
				FString FMatchType = NeoLuaStr::ToFString(MatchType);

				FGameplayTagQuery NewQuery;
				if (FMatchType.Equals(TEXT("all"), ESearchCase::IgnoreCase))
					NewQuery = FGameplayTagQuery::MakeQuery_MatchAllTags(TagContainer);
				else if (FMatchType.Equals(TEXT("none"), ESearchCase::IgnoreCase))
					NewQuery = FGameplayTagQuery::MakeQuery_MatchNoTags(TagContainer);
				else
					NewQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(TagContainer);

				TagQCol->RowValues[RowIdx] = MoveTemp(NewQuery);

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_output_cell(%d, %d) -> OutputGameplayTagQuery (%s) = %s"),
					RowIndex, ColIndex, *FMatchType, *FTags));
				return sol::make_object(Lua, true);
			}
#endif

			// Not a recognized output column type
			FString ColTypeName = ColStruct.GetScriptStruct()
				? ColStruct.GetScriptStruct()->GetName()
				: TEXT("Unknown");
			Session.Log(FString::Printf(TEXT("[FAIL] set_output_cell(%d, %d) -> column type %s is not an output column; use set_cell for filter columns. Valid output types: OutputObject, OutputFloat, OutputBool, OutputEnum, OutputStruct, OutputGameplayTagQuery"),
				RowIndex, ColIndex, *ColTypeName));
			return sol::lua_nil;
		});

		// ================================================================
		// add_rows(count?, index?) — insert rows
		// ================================================================
		AssetObj.set_function("add_rows", [WeakCT, &Session](sol::table /*self*/,
			sol::optional<int> CountOpt, sol::optional<int> IndexOpt, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] add_rows -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			int32 Count = CountOpt.value_or(1);
			int32 CurrentRows = GetChooserRowCount(CT);
			int32 InsertAt = IndexOpt.has_value() ? (IndexOpt.value() - 1) : CurrentRows; // 1-based to 0-based
			InsertAt = FMath::Clamp(InsertAt, 0, CurrentRows);

			CT->Modify();

			// Insert into all columns
			for (FInstancedStruct& ColStruct : CT->ColumnsStructs)
			{
				if (!ColStruct.IsValid()) continue;
				if (FChooserColumnBase* ColBase = ColStruct.GetMutablePtr<FChooserColumnBase>())
				{
					ColBase->InsertRows(InsertAt, Count);
				}
			}

			// Insert into ResultsStructs
			for (int32 i = 0; i < Count; ++i)
			{
				if (InsertAt <= CT->ResultsStructs.Num())
				{
					CT->ResultsStructs.Insert(FInstancedStruct(), InsertAt);
				}
				else
				{
					CT->ResultsStructs.Add(FInstancedStruct());
				}
			}

			// Insert into DisabledRows
			for (int32 i = 0; i < Count; ++i)
			{
				if (InsertAt <= CT->DisabledRows.Num())
				{
					CT->DisabledRows.Insert(false, InsertAt);
				}
				else
				{
					CT->DisabledRows.Add(false);
				}
			}

			CT->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] add_rows(%d) at index %d -> now %d rows"),
				Count, InsertAt + 1, GetChooserRowCount(CT)));
			return sol::make_object(Lua, true);
		});

		// ================================================================
		// delete_rows(indices) — delete rows by 1-based index array
		// ================================================================
		AssetObj.set_function("delete_rows", [WeakCT, &Session](sol::table /*self*/,
			sol::table Indices, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] delete_rows -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}

			// Collect and sort indices (convert 1-based to 0-based).
			// IMPORTANT: reject <1 or >RowCount BEFORE casting to uint32 — the engine's
			// DeleteRows macro (Plugins/Chooser/.../IChooserColumn.h:148) calls
			// TArray::RemoveAt(Index) with no bounds check, so an underflowed index
			// like static_cast<uint32>(-1) = 0xFFFFFFFF asserts in the engine.
			const int32 RowCount = GetChooserRowCount(CT);
			TArray<uint32> RowIndices;
			for (auto& Pair : Indices)
			{
				if (!Pair.second.is<int>())
				{
					continue;
				}
				const int32 OneBased = Pair.second.as<int>();
				if (OneBased < 1 || OneBased > RowCount)
				{
					Session.Log(FString::Printf(
						TEXT("[FAIL] delete_rows -> row index %d out of range; pass a 1-based integer array like {1,3,5}, valid rows are 1..%d"),
						OneBased, RowCount));
					return sol::lua_nil;
				}
				RowIndices.Add(static_cast<uint32>(OneBased - 1));
			}

			if (RowIndices.Num() == 0)
			{
				Session.Log(FString::Printf(
					TEXT("[FAIL] delete_rows -> pass a 1-based integer array like {1,3,5}; valid rows are 1..%d"),
					RowCount));
				return sol::lua_nil;
			}

			// Sort descending and deduplicate — engine's DeleteRows calls RemoveAt
			// sequentially, so indices must be in reverse order to stay valid
			RowIndices.Sort([](uint32 A, uint32 B) { return A > B; });
			for (int32 i = RowIndices.Num() - 1; i > 0; --i)
			{
				if (RowIndices[i] == RowIndices[i - 1])
				{
					RowIndices.RemoveAt(i);
				}
			}

			CT->Modify();

			// Delete from columns (indices already reverse-sorted)
			for (FInstancedStruct& ColStruct : CT->ColumnsStructs)
			{
				if (!ColStruct.IsValid()) continue;
				if (FChooserColumnBase* ColBase = ColStruct.GetMutablePtr<FChooserColumnBase>())
				{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
					TArray<int32> SignedRowIndices;
					SignedRowIndices.Reserve(RowIndices.Num());
					for (uint32 Idx : RowIndices)
					{
						SignedRowIndices.Add(static_cast<int32>(Idx));
					}
					ColBase->DeleteRows(SignedRowIndices);
#else
					ColBase->DeleteRows(RowIndices);
#endif
				}
			}

			// Delete from ResultsStructs and DisabledRows (same reverse order)
			for (uint32 Idx : RowIndices)
			{
				if ((int32)Idx < CT->ResultsStructs.Num())
				{
					CT->ResultsStructs.RemoveAt(Idx);
				}
				if ((int32)Idx < CT->DisabledRows.Num())
				{
					CT->DisabledRows.RemoveAt(Idx);
				}
			}

			CT->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] delete_rows -> deleted %d rows, now %d rows"),
				RowIndices.Num(), GetChooserRowCount(CT)));
			return sol::make_object(Lua, true);
		});

		// ================================================================
		// move_row(from, to) — 1-based indices
		// ================================================================
		AssetObj.set_function("move_row", [WeakCT, &Session](sol::table /*self*/,
			int From, int To, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] move_row -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			int32 SrcIdx = From - 1; // 1-based to 0-based
			int32 DstIdx = To - 1;
			int32 RowCount = GetChooserRowCount(CT);

			if (SrcIdx < 0 || SrcIdx >= RowCount || DstIdx < 0 || DstIdx >= RowCount)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] move_row(%d, %d) -> both indices must be between 1 and %d; use list(\"rows\") first"),
					From, To, RowCount));
				return sol::lua_nil;
			}

			CT->Modify();

			for (FInstancedStruct& ColStruct : CT->ColumnsStructs)
			{
				if (!ColStruct.IsValid()) continue;
				if (FChooserColumnBase* ColBase = ColStruct.GetMutablePtr<FChooserColumnBase>())
				{
					ColBase->MoveRow(SrcIdx, DstIdx);
				}
			}

			// Move in ResultsStructs — must match engine MoveRow logic:
			// RemoveAt shifts elements, so adjust target when moving forward
			if (SrcIdx < CT->ResultsStructs.Num() && DstIdx < CT->ResultsStructs.Num())
			{
				FInstancedStruct Temp = MoveTemp(CT->ResultsStructs[SrcIdx]);
				CT->ResultsStructs.RemoveAt(SrcIdx);
				int32 AdjustedDst = (SrcIdx < DstIdx) ? DstIdx - 1 : DstIdx;
				CT->ResultsStructs.Insert(MoveTemp(Temp), AdjustedDst);
			}

			// Move in DisabledRows — same adjustment
			if (SrcIdx < CT->DisabledRows.Num() && DstIdx < CT->DisabledRows.Num())
			{
				bool Temp = CT->DisabledRows[SrcIdx];
				CT->DisabledRows.RemoveAt(SrcIdx);
				int32 AdjustedDst = (SrcIdx < DstIdx) ? DstIdx - 1 : DstIdx;
				CT->DisabledRows.Insert(Temp, AdjustedDst);
			}

			CT->MarkPackageDirty();
			Session.Log(FString::Printf(TEXT("[OK] move_row -> moved row %d to %d"), From, To));
			return sol::make_object(Lua, true);
		});

		// ================================================================
		// set_row_disabled(index, disabled)
		// ================================================================
		AssetObj.set_function("set_row_disabled", [WeakCT, &Session](sol::table /*self*/,
			int Index, bool bDisabled, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] set_row_disabled -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			int32 Idx = Index - 1;

			if (Idx < 0)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_row_disabled(%d) -> row index must be >= 1"), Index));
				return sol::lua_nil;
			}

			if (!ValidateChooserAuthoringRow(CT, Index, TEXT("set_row_disabled"), Session))
			{
				return sol::lua_nil;
			}

			// Expand if needed
			while (CT->DisabledRows.Num() <= Idx)
			{
				CT->DisabledRows.Add(false);
			}

			CT->Modify();
			CT->DisabledRows[Idx] = bDisabled;
			CT->MarkPackageDirty();

			Session.Log(FString::Printf(TEXT("[OK] set_row_disabled(%d, %s)"),
				Index, bDisabled ? TEXT("true") : TEXT("false")));
			return sol::make_object(Lua, true);
		});

		// ================================================================
		// set_row_result(index, asset_path | { kind=..., asset_path|class_path|chooser_path=... })
		//   kind = "asset" (default) | "soft_asset" | "class" | "evaluate_chooser"
		//   Nested chooser authoring is deferred: FNestedChooser references a
		//   chooser inside CT->NestedChoosers and requires additional editor
		//   bookkeeping the binding does not yet model.
		// ================================================================
		AssetObj.set_function("set_row_result", [WeakCT, &Session](sol::table /*self*/,
			int Index, sol::object Arg, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] set_row_result -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			int32 Idx = Index - 1;

			if (Idx < 0 || Idx >= CT->ResultsStructs.Num())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_row_result(%d) -> row out of range; valid rows are 1..%d (add_rows() first if needed)"),
					Index, CT->ResultsStructs.Num()));
				return sol::lua_nil;
			}

			FInstancedStruct NewResult;
			FString Summary;
			FString Error;
			if (!BuildResultInstancedStruct(Arg, CT, NewResult, Summary, Error))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_row_result(%d) -> %s"), Index, *Error));
				return sol::lua_nil;
			}

			CT->Modify();
			CT->ResultsStructs[Idx] = MoveTemp(NewResult);
			CT->MarkPackageDirty();

			Session.Log(FString::Printf(TEXT("[OK] set_row_result(%d) -> %s"), Index, *Summary));
			return sol::make_object(Lua, true);
		});

		// ================================================================
		// set_fallback_result(asset_path | { kind=..., ... })
		// ================================================================
		AssetObj.set_function("set_fallback_result", [WeakCT, &Session](sol::table /*self*/,
			sol::object Arg, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] set_fallback_result -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}

			FInstancedStruct NewResult;
			FString Summary;
			FString Error;
			if (!BuildResultInstancedStruct(Arg, CT, NewResult, Summary, Error))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_fallback_result -> %s"), *Error));
				return sol::lua_nil;
			}

			CT->Modify();
			CT->FallbackResult = MoveTemp(NewResult);
			CT->MarkPackageDirty();

			Session.Log(FString::Printf(TEXT("[OK] set_fallback_result -> %s"), *Summary));
			return sol::make_object(Lua, true);
		});

		// ================================================================
		// set_fallback_output(column, opts) — set a column's FallbackValue
		//
		// When the chooser's primary FallbackResult fires (no row matched),
		// every output column also writes its FallbackValue to the context.
		// This is a separate surface from set_output_cell because row-index 0
		// is reserved for "no row" in the binding's 1-based convention.
		//
		// opts shape mirrors set_output_cell per column type:
		//   OutputObject:            { kind="asset", asset_path="/Game/..." } (or soft_asset/class/evaluate_chooser)
		//   OutputFloat:             { value = float }
		//   OutputBool:              { value = bool }
		//   OutputEnum:              { value = int }
		//   OutputStruct:            table of struct fields (routed through NeoLuaProperty::ApplyValueToStructMemory)
		//   OutputGameplayTagQuery:  { match_type="any|all|none", tags="A,B,C" }
		// ================================================================
		AssetObj.set_function("set_fallback_output", [WeakCT, &Session](sol::table /*self*/,
			int ColIndex, sol::table Opts, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] set_fallback_output -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}

			int32 ColIdx = ColIndex - 1;
			if (ColIdx < 0 || ColIdx >= CT->ColumnsStructs.Num())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_fallback_output(%d) -> column out of range; valid columns are 1..%d (run list(\"columns\"))"),
					ColIndex, CT->ColumnsStructs.Num()));
				return sol::lua_nil;
			}

			FInstancedStruct& ColStruct = CT->ColumnsStructs[ColIdx];
			if (!ColStruct.IsValid())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] set_fallback_output(%d) -> column is empty/invalid"), ColIndex));
				return sol::lua_nil;
			}

			CT->Modify();

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
			if (FOutputObjectColumn* ObjCol = ColStruct.GetMutablePtr<FOutputObjectColumn>())
			{
				FInstancedStruct NewResult;
				FString Summary;
				FString Error;
				if (!BuildResultInstancedStruct(sol::object(Opts), CT, NewResult, Summary, Error))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_fallback_output(%d) -> %s"), ColIndex, *Error));
					return sol::lua_nil;
				}
				ObjCol->FallbackValue.Value = MoveTemp(NewResult);
				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_fallback_output(%d) -> OutputObject %s"), ColIndex, *Summary));
				return sol::make_object(Lua, true);
			}
#endif // ENGINE_MINOR_VERSION >= 5

			if (FOutputFloatColumn* FloatCol = ColStruct.GetMutablePtr<FOutputFloatColumn>())
			{
				FloatCol->FallbackValue = Opts.get_or("value", 0.0);
				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_fallback_output(%d) -> OutputFloat = %g"),
					ColIndex, FloatCol->FallbackValue));
				return sol::make_object(Lua, true);
			}

			if (FOutputBoolColumn* BoolCol = ColStruct.GetMutablePtr<FOutputBoolColumn>())
			{
				BoolCol->bFallbackValue = Opts.get_or("value", false);
				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_fallback_output(%d) -> OutputBool = %s"),
					ColIndex, BoolCol->bFallbackValue ? TEXT("true") : TEXT("false")));
				return sol::make_object(Lua, true);
			}

			if (FOutputEnumColumn* EnumCol = ColStruct.GetMutablePtr<FOutputEnumColumn>())
			{
				EnumCol->FallbackValue.Value = static_cast<uint8>(Opts.get_or("value", 0));
				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_fallback_output(%d) -> OutputEnum = %d"),
					ColIndex, EnumCol->FallbackValue.Value));
				return sol::make_object(Lua, true);
			}

			if (FOutputStructColumn* StructCol = ColStruct.GetMutablePtr<FOutputStructColumn>())
			{
				FInstancedStruct& Slot = StructCol->FallbackValue;
#if WITH_EDITORONLY_DATA
				if (!Slot.IsValid() && StructCol->DefaultRowValue.IsValid())
				{
					Slot.InitializeAs(StructCol->DefaultRowValue.GetScriptStruct());
				}
#endif
				if (!Slot.IsValid() || !Slot.GetScriptStruct())
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_fallback_output(%d) -> OutputStruct has no struct type configured; bind the column to a struct property first (edit the asset in the chooser editor)"),
						ColIndex));
					return sol::lua_nil;
				}

				FString Error;
				UScriptStruct* SlotStruct = const_cast<UScriptStruct*>(Slot.GetScriptStruct());
				if (!NeoLuaProperty::ApplyValueToStructMemory(SlotStruct, Slot.GetMutableMemory(), sol::object(Opts), CT, Error))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] set_fallback_output(%d) -> %s"), ColIndex, *Error));
					return sol::lua_nil;
				}

				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_fallback_output(%d) -> OutputStruct (%s)"),
					ColIndex, *SlotStruct->GetName()));
				return sol::make_object(Lua, true);
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			if (FOutputGameplayTagQueryColumn* TagQCol = ColStruct.GetMutablePtr<FOutputGameplayTagQueryColumn>())
			{
				std::string TagStr = Opts.get_or<std::string>("tags", "");
				FString FTags = NeoLuaStr::ToFString(TagStr);
				FGameplayTagContainer TagContainer;
				TArray<FString> TagArray;
				FTags.ParseIntoArray(TagArray, TEXT(","));
				for (const FString& TagName : TagArray)
				{
					FString Trimmed = TagName.TrimStartAndEnd();
					FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Trimmed), false);
					if (Tag.IsValid())
					{
						TagContainer.AddTag(Tag);
					}
				}
				std::string MatchType = Opts.get_or<std::string>("match_type", "any");
				FString FMatchType = NeoLuaStr::ToFString(MatchType);
				FGameplayTagQuery NewQuery;
				if (FMatchType.Equals(TEXT("all"), ESearchCase::IgnoreCase))
					NewQuery = FGameplayTagQuery::MakeQuery_MatchAllTags(TagContainer);
				else if (FMatchType.Equals(TEXT("none"), ESearchCase::IgnoreCase))
					NewQuery = FGameplayTagQuery::MakeQuery_MatchNoTags(TagContainer);
				else
					NewQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(TagContainer);
				TagQCol->FallbackValue = MoveTemp(NewQuery);
				CT->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] set_fallback_output(%d) -> OutputGameplayTagQuery (%s) = %s"),
					ColIndex, *FMatchType, *FTags));
				return sol::make_object(Lua, true);
			}
#endif

			FString ColTypeName = ColStruct.GetScriptStruct()
				? ColStruct.GetScriptStruct()->GetName()
				: TEXT("Unknown");
			Session.Log(FString::Printf(TEXT("[FAIL] set_fallback_output(%d) -> column type %s is not an output column. Valid output types: OutputObject, OutputFloat, OutputBool, OutputEnum, OutputStruct, OutputGameplayTagQuery"),
				ColIndex, *ColTypeName));
			return sol::lua_nil;
		});

		// ================================================================
		// compile() — recompile the chooser after edits
		// ================================================================
		AssetObj.set_function("compile", [WeakCT, &Session](sol::table /*self*/, sol::this_state S) -> sol::object
		{
			sol::state_view Lua(S);
			UChooserTable* CT = WeakCT.Get();
			if (!CT)
			{
				Session.Log(TEXT("[FAIL] compile -> chooser asset no longer valid; call open_asset() again"));
				return sol::lua_nil;
			}
			CT->Compile(true);
			Session.Log(TEXT("[OK] compile -> chooser recompiled"));
			return sol::make_object(Lua, true);
		});
#endif // WITH_EDITOR
	});

	// ========================================================================
	// Global runtime evaluation / debug APIs — thin wrappers around
	// UChooserFunctionLibrary (+ UChooserTable debug-target surface).
	//
	// NOTE: AddChooserStructInput / GetChooserStructOutput are CustomThunk
	// UFUNCTIONs with variadic struct payloads. Invoking them from native C++
	// outside Blueprint requires reconstructing the CustomThunk call frame,
	// which is non-trivial. Expose later if a concrete call-site appears.
	// ========================================================================

	Lua.set_function("chooser_evaluate", [&Session](const std::string& TablePath,
		sol::optional<std::string> ContextPathOpt, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		FString FPath = NeoLuaStr::ToFString(TablePath);
		UChooserTable* Table = NeoLuaAsset::ResolveWithRegistry<UChooserTable>(FPath);
		if (!Table)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] chooser_evaluate -> chooser not found: %s"), *FPath));
			return sol::lua_nil;
		}

		UObject* ContextObj = nullptr;
		if (ContextPathOpt.has_value())
		{
			FString CPath = NeoLuaStr::ToFString(ContextPathOpt.value());
			ContextObj = NeoLuaAsset::ResolveWithRegistry<UObject>(CPath);
			if (!ContextObj)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] chooser_evaluate -> context object not found: %s"), *CPath));
				return sol::lua_nil;
			}
		}

		UObject* Selected = UChooserFunctionLibrary::EvaluateChooser(ContextObj, Table, UObject::StaticClass());
		if (!Selected)
		{
			Session.Log(FString::Printf(TEXT("[OK] chooser_evaluate(%s) -> nil (no row matched and no fallback)"), *FPath));
			return sol::lua_nil;
		}

		FString SelectedPath = Selected->GetPathName();
		Session.Log(FString::Printf(TEXT("[OK] chooser_evaluate(%s) -> %s"), *FPath, *SelectedPath));
		return sol::make_object(LuaView, std::string(TCHAR_TO_UTF8(*SelectedPath)));
	});

	Lua.set_function("chooser_evaluate_multi", [&Session](const std::string& TablePath,
		sol::optional<std::string> ContextPathOpt, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		FString FPath = NeoLuaStr::ToFString(TablePath);
		UChooserTable* Table = NeoLuaAsset::ResolveWithRegistry<UChooserTable>(FPath);
		if (!Table)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] chooser_evaluate_multi -> chooser not found: %s"), *FPath));
			return sol::lua_nil;
		}

		UObject* ContextObj = nullptr;
		if (ContextPathOpt.has_value())
		{
			FString CPath = NeoLuaStr::ToFString(ContextPathOpt.value());
			ContextObj = NeoLuaAsset::ResolveWithRegistry<UObject>(CPath);
			if (!ContextObj)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] chooser_evaluate_multi -> context object not found: %s"), *CPath));
				return sol::lua_nil;
			}
		}

		TArray<UObject*> Results = UChooserFunctionLibrary::EvaluateChooserMulti(ContextObj, Table, UObject::StaticClass());
		sol::table Arr = LuaView.create_table();
		int32 OutIdx = 1;
		for (UObject* Obj : Results)
		{
			if (Obj)
			{
				Arr[OutIdx++] = TCHAR_TO_UTF8(*Obj->GetPathName());
			}
		}
		Session.Log(FString::Printf(TEXT("[OK] chooser_evaluate_multi(%s) -> %d results"), *FPath, OutIdx - 1));
		return sol::make_object(LuaView, Arr);
	});

	Lua.set_function("chooser_make_context", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);
		// Shared ownership so every closure on the returned table keeps the same context alive.
		TSharedPtr<FChooserEvaluationContext> Ctx = MakeShared<FChooserEvaluationContext>();

		sol::table CtxObj = LuaView.create_table();

		CtxObj.set_function("add_object", [Ctx, &Session](sol::table /*self*/, const std::string& Path) -> bool
		{
			// Engine caps ObjectParams at 4 via TFixedAllocator<4> (IObjectChooser.h:102).
			// Exceeding it would assert, so reject in Lua with a clear message.
			if (Ctx->ObjectParams.Num() >= 4)
			{
				Session.Log(TEXT("[FAIL] context:add_object -> FChooserEvaluationContext is limited to 4 object inputs"));
				return false;
			}
			FString FPath = NeoLuaStr::ToFString(Path);
			UObject* Obj = NeoLuaAsset::ResolveWithRegistry<UObject>(FPath);
			if (!Obj)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] context:add_object -> asset not found: %s"), *FPath));
				return false;
			}
			Ctx->AddObjectParam(Obj);
			Session.Log(FString::Printf(TEXT("[OK] context:add_object -> %s (count=%d)"), *FPath, Ctx->ObjectParams.Num()));
			return true;
		});

		CtxObj.set_function("get_object", [Ctx, &Session](sol::table /*self*/, int Index, sol::this_state S2) -> sol::object
		{
			sol::state_view LuaInner(S2);
			int32 Idx = Index - 1;
			if (Idx < 0 || Idx >= Ctx->ObjectParams.Num())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] context:get_object(%d) -> out of range; have %d object inputs"),
					Index, Ctx->ObjectParams.Num()));
				return sol::lua_nil;
			}
			if (UObject* Obj = Ctx->ObjectParams[Idx].Object)
			{
				return sol::make_object(LuaInner, std::string(TCHAR_TO_UTF8(*Obj->GetPathName())));
			}
			return sol::lua_nil;
		});

		CtxObj.set_function("count", [Ctx](sol::table /*self*/) -> int
		{
			return Ctx->ObjectParams.Num();
		});

		CtxObj.set_function("evaluate", [Ctx, &Session](sol::table /*self*/, const std::string& TablePath, sol::this_state S2) -> sol::object
		{
			sol::state_view LuaInner(S2);
			FString FPath = NeoLuaStr::ToFString(TablePath);
			UChooserTable* Table = NeoLuaAsset::ResolveWithRegistry<UChooserTable>(FPath);
			if (!Table)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] context:evaluate -> chooser not found: %s"), *FPath));
				return sol::lua_nil;
			}

			// Build an FInstancedStruct wrapping the chooser, then call the base evaluator
			// so the caller's whole context (all added objects) is preserved — unlike
			// the simple EvaluateChooser overload which only takes a single ContextObject.
			FInstancedStruct EvalStruct;
			EvalStruct.InitializeAs(FEvaluateChooser::StaticStruct());
			EvalStruct.GetMutable<FEvaluateChooser>().Chooser = Table;

			UObject* Selected = UChooserFunctionLibrary::EvaluateObjectChooserBase(*Ctx, EvalStruct, UObject::StaticClass(), false);
			if (!Selected)
			{
				Session.Log(FString::Printf(TEXT("[OK] context:evaluate(%s) -> nil"), *FPath));
				return sol::lua_nil;
			}
			FString SelectedPath = Selected->GetPathName();
			Session.Log(FString::Printf(TEXT("[OK] context:evaluate(%s) -> %s"), *FPath, *SelectedPath));
			return sol::make_object(LuaInner, std::string(TCHAR_TO_UTF8(*SelectedPath)));
		});

		CtxObj.set_function("evaluate_multi", [Ctx, &Session](sol::table /*self*/, const std::string& TablePath, sol::this_state S2) -> sol::object
		{
			sol::state_view LuaInner(S2);
			FString FPath = NeoLuaStr::ToFString(TablePath);
			UChooserTable* Table = NeoLuaAsset::ResolveWithRegistry<UChooserTable>(FPath);
			if (!Table)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] context:evaluate_multi -> chooser not found: %s"), *FPath));
				return sol::lua_nil;
			}

			FInstancedStruct EvalStruct;
			EvalStruct.InitializeAs(FEvaluateChooser::StaticStruct());
			EvalStruct.GetMutable<FEvaluateChooser>().Chooser = Table;

			TArray<UObject*> Results = UChooserFunctionLibrary::EvaluateObjectChooserBaseMulti(*Ctx, EvalStruct, UObject::StaticClass(), false);
			sol::table Arr = LuaInner.create_table();
			int32 OutIdx = 1;
			for (UObject* Obj : Results)
			{
				if (Obj)
				{
					Arr[OutIdx++] = TCHAR_TO_UTF8(*Obj->GetPathName());
				}
			}
			Session.Log(FString::Printf(TEXT("[OK] context:evaluate_multi(%s) -> %d"), *FPath, OutIdx - 1));
			return sol::make_object(LuaInner, Arr);
		});

		return sol::make_object(LuaView, CtxObj);
	});

#if WITH_EDITOR
	Lua.set_function("chooser_set_debug_target", [&Session](const std::string& TablePath, const std::string& Name) -> bool
	{
		FString FPath = NeoLuaStr::ToFString(TablePath);
		UChooserTable* Table = NeoLuaAsset::ResolveWithRegistry<UChooserTable>(FPath);
		if (!Table)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] chooser_set_debug_target -> chooser not found: %s"), *FPath));
			return false;
		}
		Table->SetDebugTarget(NeoLuaStr::ToFString(Name));
		Session.Log(FString::Printf(TEXT("[OK] chooser_set_debug_target(%s) -> %s"), *FPath, ANSI_TO_TCHAR(Name.c_str())));
		return true;
	});

	Lua.set_function("chooser_reset_debug_target", [&Session](const std::string& TablePath) -> bool
	{
		FString FPath = NeoLuaStr::ToFString(TablePath);
		UChooserTable* Table = NeoLuaAsset::ResolveWithRegistry<UChooserTable>(FPath);
		if (!Table)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] chooser_reset_debug_target -> chooser not found: %s"), *FPath));
			return false;
		}
		Table->ResetDebugTarget();
		Session.Log(FString::Printf(TEXT("[OK] chooser_reset_debug_target(%s)"), *FPath));
		return true;
	});

	Lua.set_function("chooser_get_debug_row", [&Session](const std::string& TablePath) -> int
	{
		FString FPath = NeoLuaStr::ToFString(TablePath);
		UChooserTable* Table = NeoLuaAsset::ResolveWithRegistry<UChooserTable>(FPath);
		if (!Table)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] chooser_get_debug_row -> chooser not found: %s"), *FPath));
			return 0;
		}
		// Engine stores -1 for "no row selected" or ChooserColumn_SpecialIndex_Fallback for fallback.
		// Translate: 0 = none, -1 = fallback, otherwise 1-based row index.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		const TArray<int32>& RawRows = Table->GetDebugSelectedRows();
		const int32 Raw = RawRows.Num() > 0 ? RawRows[0] : INDEX_NONE;
#else
		const int32 Raw = Table->GetDebugSelectedRow();
#endif
		if (Raw == ChooserColumn_SpecialIndex_Fallback) return -1;
		if (Raw < 0) return 0;
		return Raw + 1;
	});

	Lua.set_function("chooser_set_debug_testing", [&Session](const std::string& TablePath, bool bEnable) -> bool
	{
		FString FPath = NeoLuaStr::ToFString(TablePath);
		UChooserTable* Table = NeoLuaAsset::ResolveWithRegistry<UChooserTable>(FPath);
		if (!Table)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] chooser_set_debug_testing -> chooser not found: %s"), *FPath));
			return false;
		}
		Table->SetEnableDebugTesting(bEnable);
		Session.Log(FString::Printf(TEXT("[OK] chooser_set_debug_testing(%s, %s)"),
			*FPath, bEnable ? TEXT("true") : TEXT("false")));
		return true;
	});
#endif // WITH_EDITOR
}

static EObjectChooserResultType ParseProxyResultType(const FString& In)
{
	if (In.Equals(TEXT("Class"), ESearchCase::IgnoreCase) ||
		In.Equals(TEXT("ClassResult"), ESearchCase::IgnoreCase))
	{
		return EObjectChooserResultType::ClassResult;
	}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	if (In.Equals(TEXT("None"), ESearchCase::IgnoreCase) ||
		In.Equals(TEXT("NoPrimaryResult"), ESearchCase::IgnoreCase))
	{
		return EObjectChooserResultType::NoPrimaryResult;
	}
#endif
	return EObjectChooserResultType::ObjectResult;
}

static void EnsureProxyGuid(UProxyAsset* Proxy)
{
	if (Proxy && !Proxy->Guid.IsValid())
	{
		Proxy->Guid = FGuid::NewGuid();
	}
}

static void MarkProxyAssetChanged(UProxyAsset* Proxy)
{
	if (!Proxy)
	{
		return;
	}
	Proxy->Modify();
	EnsureProxyGuid(Proxy);
	Proxy->MarkPackageDirty();
}

static void RefreshProxyTable(UProxyTable* Table)
{
	if (!Table)
	{
		return;
	}
	Table->Modify();
	Table->PostLoad();
#if WITH_EDITORONLY_DATA
	Table->OnProxyTableChanged.Broadcast();
#endif
	Table->MarkPackageDirty();
}

static void AddProxyValueInfo(sol::table& Entry, const FInstancedStruct& Value)
{
	if (!Value.IsValid())
	{
		Entry["value_type"] = "(invalid)";
		return;
	}

	const FString TypeName = Value.GetScriptStruct() ? Value.GetScriptStruct()->GetName() : TEXT("Unknown");
	Entry["value_type"] = TCHAR_TO_UTF8(*TypeName);

	if (const FAssetChooser* AssetValue = Value.GetPtr<FAssetChooser>())
	{
		if (UObject* Asset = AssetValue->Asset.Get())
		{
			Entry["value_kind"] = "asset";
			Entry["value_asset"] = TCHAR_TO_UTF8(*Asset->GetName());
			Entry["value_asset_path"] = TCHAR_TO_UTF8(*Asset->GetPathName());
			Entry["value_asset_class"] = TCHAR_TO_UTF8(*Asset->GetClass()->GetName());
		}
	}
	else if (const FSoftAssetChooser* SoftValue = Value.GetPtr<FSoftAssetChooser>())
	{
		const FSoftObjectPath SoftPath = SoftValue->Asset.ToSoftObjectPath();
		if (SoftPath.IsValid())
		{
			Entry["value_kind"] = "soft_asset";
			Entry["value_asset"] = TCHAR_TO_UTF8(*FPackageName::GetShortName(SoftPath.ToString()));
			Entry["value_asset_path"] = TCHAR_TO_UTF8(*SoftPath.ToString());
		}
	}
	else if (const FClassChooser* ClassValue = Value.GetPtr<FClassChooser>())
	{
		if (UClass* Class = ClassValue->Class.Get())
		{
			Entry["value_kind"] = "class";
			Entry["value_class"] = TCHAR_TO_UTF8(*Class->GetName());
			Entry["value_class_path"] = TCHAR_TO_UTF8(*Class->GetPathName());
		}
	}
	else if (const FEvaluateChooser* EvalValue = Value.GetPtr<FEvaluateChooser>())
	{
		if (EvalValue->Chooser)
		{
			Entry["value_kind"] = "evaluate_chooser";
			Entry["value_chooser"] = TCHAR_TO_UTF8(*EvalValue->Chooser->GetName());
			Entry["value_chooser_path"] = TCHAR_TO_UTF8(*EvalValue->Chooser->GetPathName());
		}
	}
}

static sol::table BuildProxyStructOutputsInfo(sol::state_view Lua, const TArray<FProxyStructOutput>& Outputs)
{
	sol::table Result = Lua.create_table();
	for (int32 Index = 0; Index < Outputs.Num(); ++Index)
	{
		const FProxyStructOutput& Output = Outputs[Index];
		sol::table Row = Lua.create_table();
		Row["index"] = Index + 1;
		Row["context_index"] = Output.Binding.ContextIndex + 1;
		Row["is_root"] = Output.Binding.IsBoundToRoot;
		Row["chain_count"] = Output.Binding.PropertyBindingChain.Num();
#if WITH_EDITORONLY_DATA
		if (Output.Binding.StructType)
		{
			Row["binding_struct"] = TCHAR_TO_UTF8(*Output.Binding.StructType->GetName());
			Row["binding_struct_path"] = TCHAR_TO_UTF8(*Output.Binding.StructType->GetPathName());
		}
#endif
		if (Output.Value.IsValid() && Output.Value.GetScriptStruct())
		{
			UScriptStruct* Struct = const_cast<UScriptStruct*>(Output.Value.GetScriptStruct());
			Row["value_struct"] = TCHAR_TO_UTF8(*Struct->GetName());
			Row["value_struct_path"] = TCHAR_TO_UTF8(*Struct->GetPathName());
			Row["value"] = NeoLuaProperty::ReadStructMemoryAsSol(Struct, Output.Value.GetMemory(), Lua);
		}
		Result[Index + 1] = Row;
	}
	return Result;
}

static sol::table BuildProxyContextList(sol::state_view Lua, const TArray<FInstancedStruct>& ContextData)
{
	sol::table Result = Lua.create_table();
	int32 OutIdx = 1;
	for (int32 Index = 0; Index < ContextData.Num(); ++Index)
	{
		const FInstancedStruct& ContextStruct = ContextData[Index];
		sol::table Entry = Lua.create_table();
		Entry["index"] = Index + 1;
		if (const FContextObjectTypeClass* ClassContext = ContextStruct.GetPtr<FContextObjectTypeClass>())
		{
			Entry["kind"] = "class";
			Entry["direction"] = GetChooserContextDirectionName(ClassContext->Direction);
			if (ClassContext->Class)
			{
				Entry["class_name"] = TCHAR_TO_UTF8(*ClassContext->Class->GetName());
				Entry["class_path"] = TCHAR_TO_UTF8(*ClassContext->Class->GetPathName());
			}
		}
		else if (const FContextObjectTypeStruct* StructContext = ContextStruct.GetPtr<FContextObjectTypeStruct>())
		{
			Entry["kind"] = "struct";
			Entry["direction"] = GetChooserContextDirectionName(StructContext->Direction);
			if (StructContext->Struct)
			{
				Entry["struct_name"] = TCHAR_TO_UTF8(*StructContext->Struct->GetName());
				Entry["struct_path"] = TCHAR_TO_UTF8(*StructContext->Struct->GetPathName());
			}
		}
		else
		{
			Entry["kind"] = ContextStruct.GetScriptStruct() ? TCHAR_TO_UTF8(*ContextStruct.GetScriptStruct()->GetName()) : "(invalid)";
		}
		Result[OutIdx++] = Entry;
	}
	return Result;
}

static bool BuildProxyContextStruct(const sol::table& Opts, FInstancedStruct& OutStruct, FString& OutError)
{
	FString Kind;
	TryGetChooserString(Opts, "kind", Kind);
	if (Kind.IsEmpty())
	{
		Kind = (Opts["struct"].valid() || Opts["struct_path"].valid()) ? TEXT("struct") : TEXT("class");
	}

	FString DirectionStr;
	TryGetChooserString(Opts, "direction", DirectionStr);
	const EContextObjectDirection Direction = ParseChooserContextDirection(DirectionStr);

	if (Kind.Equals(TEXT("class"), ESearchCase::IgnoreCase) || Kind.Equals(TEXT("object"), ESearchCase::IgnoreCase))
	{
		FString ClassPath;
		if (!TryGetChooserString(Opts, "class", ClassPath))
		{
			TryGetChooserString(Opts, "class_path", ClassPath);
		}
		UClass* Class = nullptr;
		if (!TryResolveChooserClass(ClassPath, Class))
		{
			OutError = FString::Printf(TEXT("class not found: %s"), *ClassPath);
			return false;
		}
		OutStruct.InitializeAs<FContextObjectTypeClass>();
		FContextObjectTypeClass& Context = OutStruct.GetMutable<FContextObjectTypeClass>();
		Context.Class = Class;
		Context.Direction = Direction;
		return true;
	}

	if (Kind.Equals(TEXT("struct"), ESearchCase::IgnoreCase))
	{
		FString StructPath;
		if (!TryGetChooserString(Opts, "struct", StructPath))
		{
			TryGetChooserString(Opts, "struct_path", StructPath);
		}
		UScriptStruct* Struct = nullptr;
		if (!TryResolveChooserStruct(StructPath, Struct))
		{
			OutError = FString::Printf(TEXT("struct not found: %s"), *StructPath);
			return false;
		}
		OutStruct.InitializeAs<FContextObjectTypeStruct>();
		FContextObjectTypeStruct& Context = OutStruct.GetMutable<FContextObjectTypeStruct>();
		Context.Struct = Struct;
		Context.Direction = Direction;
		return true;
	}

	OutError = TEXT("context kind must be class or struct");
	return false;
}

static UProxyAsset* ResolveProxyAssetForLua(const sol::table& Opts, const char* Key)
{
	std::string PathStr = Opts.get_or<std::string>(Key, "");
	FString Path = NeoLuaStr::ToFString(PathStr);
	return Path.IsEmpty() ? nullptr : NeoLuaAsset::ResolveWithRegistry<UProxyAsset>(Path);
}

static void BindProxyAssetAndTable(sol::state& Lua, FLuaSessionData& Session)
{
	Lua.set_function("_enrich_proxy_asset", [&Session](sol::table AssetObj, sol::this_state S)
	{
		sol::state_view LuaView(S);
		std::string PathStr = AssetObj.get<std::string>("path");
		FString FPath = NeoLuaStr::ToFString(PathStr);
		UProxyAsset* Proxy = NeoLuaAsset::Resolve<UProxyAsset>(FPath);
		if (!Proxy)
		{
			return;
		}
		TWeakObjectPtr<UProxyAsset> WeakProxy(Proxy);

		AssetObj["_help_text"] =
			"ProxyAsset enrichment.\n"
			"info() -> type/result/context/guid state\n"
			"configure({type|class=\"/Script/...\", result_type=\"Object|Class|NoPrimaryResult\"})\n"
			"add_context({kind=\"class\", class=\"/Script/Engine.Actor\", direction=\"ReadWrite\"})\n"
			"list(\"context\") -> context parameter rows\n";

		AssetObj.set_function("info", [WeakProxy, &Session](sol::table, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyAsset* Current = WeakProxy.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy info -> asset no longer valid"));
				return sol::lua_nil;
			}
			sol::table Result = Lua.create_table();
			Result["type"] = "ProxyAsset";
			Result["result_type"] = GetResultTypeName(Current->ResultType);
			Result["context_count"] = Current->ContextData.Num();
			Result["guid_valid"] = Current->Guid.IsValid();
			Result["guid"] = TCHAR_TO_UTF8(*Current->Guid.ToString(EGuidFormats::DigitsWithHyphens));
			if (Current->Type)
			{
				Result["output_class"] = TCHAR_TO_UTF8(*Current->Type->GetName());
				Result["output_class_path"] = TCHAR_TO_UTF8(*Current->Type->GetPathName());
			}
			return Result;
		});

		AssetObj.set_function("list", [WeakProxy, &Session](sol::table, sol::optional<std::string> TypeOpt, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyAsset* Current = WeakProxy.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy list -> asset no longer valid"));
				return sol::lua_nil;
			}
			const FString Type = NeoLuaStr::ToFStringOpt(TypeOpt, TEXT("context"));
			if (Type.Equals(TEXT("context"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("contexts"), ESearchCase::IgnoreCase))
			{
				return BuildProxyContextList(Lua, Current->ContextData);
			}
			Session.Log(TEXT("[FAIL] proxy list -> valid category: context"));
			return sol::lua_nil;
		});

		AssetObj.set_function("configure", [WeakProxy, &Session](sol::table, sol::table Opts, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyAsset* Current = WeakProxy.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy configure -> asset no longer valid"));
				return sol::lua_nil;
			}

			Current->Modify();
			FString ClassPath;
			if (!TryGetChooserString(Opts, "type", ClassPath))
			{
				TryGetChooserString(Opts, "class", ClassPath);
			}
			if (!ClassPath.IsEmpty())
			{
				UClass* Class = nullptr;
				if (!TryResolveChooserClass(ClassPath, Class))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] proxy configure -> class not found: %s"), *ClassPath));
					return sol::lua_nil;
				}
				Current->Type = Class;
			}

			FString ResultType;
			if (TryGetChooserString(Opts, "result_type", ResultType))
			{
				Current->ResultType = ParseProxyResultType(ResultType);
			}

			MarkProxyAssetChanged(Current);
			sol::protected_function InfoFn = Lua["open_asset"];
			(void)InfoFn;
			sol::table Result = Lua.create_table();
			Result["result_type"] = GetResultTypeName(Current->ResultType);
			Result["guid_valid"] = Current->Guid.IsValid();
			if (Current->Type)
			{
				Result["output_class"] = TCHAR_TO_UTF8(*Current->Type->GetName());
				Result["output_class_path"] = TCHAR_TO_UTF8(*Current->Type->GetPathName());
			}
			Session.Log(TEXT("[OK] proxy configure"));
			return Result;
		});

		AssetObj.set_function("add_context", [WeakProxy, &Session](sol::table, sol::table Opts, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyAsset* Current = WeakProxy.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy add_context -> asset no longer valid"));
				return sol::lua_nil;
			}
			FInstancedStruct Context;
			FString Error;
			if (!BuildProxyContextStruct(Opts, Context, Error))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] proxy add_context -> %s"), *Error));
				return sol::lua_nil;
			}
			Current->Modify();
			Current->ContextData.Add(MoveTemp(Context));
			MarkProxyAssetChanged(Current);
			sol::table Result = Lua.create_table();
			Result["index"] = Current->ContextData.Num();
			Result["context_count"] = Current->ContextData.Num();
			Session.Log(FString::Printf(TEXT("[OK] proxy add_context -> %d"), Current->ContextData.Num()));
			return Result;
		});
	});

	Lua.set_function("_enrich_proxy_table", [&Session](sol::table AssetObj, sol::this_state S)
	{
		sol::state_view LuaView(S);
		std::string PathStr = AssetObj.get<std::string>("path");
		FString FPath = NeoLuaStr::ToFString(PathStr);
		UProxyTable* Table = NeoLuaAsset::Resolve<UProxyTable>(FPath);
		if (!Table)
		{
			return;
		}
		TWeakObjectPtr<UProxyTable> WeakTable(Table);

		AssetObj["_help_text"] =
			"ProxyTable enrichment.\n"
			"info() -> entry/inheritance/runtime counts\n"
			"list(\"entries\"|\"inherited\"|\"runtime\")\n"
			"add_entry({proxy_asset=\"/Game/...\", value={kind=\"asset\", asset_path=\"/Engine/...\"}, key?=\"Legacy\"})\n"
			"set_entry(index, {proxy_asset?, value?, key?}) / remove_entry(index)\n"
			"add_inherited(table_path) / remove_inherited(index) / compile()\n"
			"evaluate(proxy_asset_path, context_object_path?) -> selected object path or nil\n";

		AssetObj.set_function("info", [WeakTable, &Session](sol::table, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table info -> asset no longer valid"));
				return sol::lua_nil;
			}
			sol::table Result = Lua.create_table();
			Result["type"] = "ProxyTable";
#if WITH_EDITORONLY_DATA
			Result["entry_count"] = Current->Entries.Num();
			Result["inherited_count"] = Current->InheritEntriesFrom.Num();
#else
			Result["entry_count"] = 0;
			Result["inherited_count"] = 0;
#endif
			Result["runtime_key_count"] = Current->Keys.Num();
			Result["runtime_value_count"] = Current->RuntimeValues.Num();
			return Result;
		});

		AssetObj.set_function("list", [WeakTable, &Session](sol::table, sol::optional<std::string> TypeOpt, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table list -> asset no longer valid"));
				return sol::lua_nil;
			}
			const FString Type = NeoLuaStr::ToFStringOpt(TypeOpt, TEXT("entries"));
			sol::table Result = Lua.create_table();
			int32 OutIdx = 1;
#if WITH_EDITORONLY_DATA
			if (Type.Equals(TEXT("entries"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("entry"), ESearchCase::IgnoreCase))
			{
				for (int32 Index = 0; Index < Current->Entries.Num(); ++Index)
				{
					const FProxyEntry& Entry = Current->Entries[Index];
					sol::table Row = Lua.create_table();
					Row["index"] = Index + 1;
					Row["key"] = TCHAR_TO_UTF8(*Entry.Key.ToString());
					const FGuid Guid = Entry.GetGuid();
					Row["guid_valid"] = Guid.IsValid();
					Row["guid"] = TCHAR_TO_UTF8(*Guid.ToString(EGuidFormats::DigitsWithHyphens));
					if (Entry.Proxy)
					{
						Row["proxy_asset"] = TCHAR_TO_UTF8(*Entry.Proxy->GetName());
						Row["proxy_asset_path"] = TCHAR_TO_UTF8(*Entry.Proxy->GetPathName());
					}
					AddProxyValueInfo(Row, Entry.ValueStruct);
					Row["struct_output_count"] = Entry.OutputStructData.Num();
					Row["struct_outputs"] = BuildProxyStructOutputsInfo(Lua, Entry.OutputStructData);
					Result[OutIdx++] = Row;
				}
				return Result;
			}
			if (Type.Equals(TEXT("inherited"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("inheritance"), ESearchCase::IgnoreCase))
			{
				for (int32 Index = 0; Index < Current->InheritEntriesFrom.Num(); ++Index)
				{
					sol::table Row = Lua.create_table();
					Row["index"] = Index + 1;
					if (UProxyTable* Parent = Current->InheritEntriesFrom[Index])
					{
						Row["table"] = TCHAR_TO_UTF8(*Parent->GetName());
						Row["table_path"] = TCHAR_TO_UTF8(*Parent->GetPathName());
					}
					Result[OutIdx++] = Row;
				}
				return Result;
			}
#endif
			if (Type.Equals(TEXT("runtime"), ESearchCase::IgnoreCase))
			{
				for (int32 Index = 0; Index < Current->RuntimeValues.Num(); ++Index)
				{
					const FRuntimeProxyValue& Value = Current->RuntimeValues[Index];
					sol::table Row = Lua.create_table();
					Row["index"] = Index + 1;
					if (Current->Keys.IsValidIndex(Index))
					{
						Row["guid"] = TCHAR_TO_UTF8(*Current->Keys[Index].ToString(EGuidFormats::DigitsWithHyphens));
						Row["guid_valid"] = Current->Keys[Index].IsValid();
					}
					if (Value.ProxyAsset)
					{
						Row["proxy_asset"] = TCHAR_TO_UTF8(*Value.ProxyAsset->GetName());
						Row["proxy_asset_path"] = TCHAR_TO_UTF8(*Value.ProxyAsset->GetPathName());
					}
					AddProxyValueInfo(Row, Value.Value);
					Row["struct_output_count"] = Value.OutputStructData.Num();
					Row["struct_outputs"] = BuildProxyStructOutputsInfo(Lua, Value.OutputStructData);
					Result[OutIdx++] = Row;
				}
				return Result;
			}
			Session.Log(TEXT("[FAIL] proxy_table list -> valid categories: entries, inherited, runtime"));
			return sol::lua_nil;
		});

		AssetObj.set_function("add_entry", [WeakTable, &Session](sol::table, sol::table Opts, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table add_entry -> asset no longer valid"));
				return sol::lua_nil;
			}
#if WITH_EDITORONLY_DATA
			UProxyAsset* Proxy = ResolveProxyAssetForLua(Opts, "proxy_asset");
			if (!Proxy)
			{
				Proxy = ResolveProxyAssetForLua(Opts, "proxy_path");
			}
			if (!Proxy)
			{
				Session.Log(TEXT("[FAIL] proxy_table add_entry -> proxy_asset path required"));
				return sol::lua_nil;
			}
			EnsureProxyGuid(Proxy);
			sol::object ValueObj = Opts.get<sol::object>("value");
			if (!ValueObj.valid() || ValueObj == sol::lua_nil)
			{
				Session.Log(TEXT("[FAIL] proxy_table add_entry -> value required"));
				return sol::lua_nil;
			}
			FInstancedStruct Value;
			FString Summary;
			FString Error;
			if (!BuildResultInstancedStruct(ValueObj, nullptr, Value, Summary, Error))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] proxy_table add_entry -> %s"), *Error));
				return sol::lua_nil;
			}
			Current->Modify();
			FProxyEntry& Entry = Current->Entries.AddDefaulted_GetRef();
			Entry.Proxy = Proxy;
			Entry.ValueStruct = MoveTemp(Value);
			std::string KeyStr = Opts.get_or<std::string>("key", "");
			Entry.Key = FName(*NeoLuaStr::ToFString(KeyStr));
			RefreshProxyTable(Current);
			sol::table Result = Lua.create_table();
			Result["index"] = Current->Entries.Num();
			Result["entry_count"] = Current->Entries.Num();
			Result["runtime_key_count"] = Current->Keys.Num();
			Result["proxy_asset_path"] = TCHAR_TO_UTF8(*Proxy->GetPathName());
			AddProxyValueInfo(Result, Entry.ValueStruct);
			Session.Log(FString::Printf(TEXT("[OK] proxy_table add_entry -> %d (%s)"), Current->Entries.Num(), *Summary));
			return Result;
#else
			Session.Log(TEXT("[FAIL] proxy_table add_entry -> editor-only data unavailable"));
			return sol::lua_nil;
#endif
		});

		AssetObj.set_function("set_entry", [WeakTable, &Session](sol::table, int32 Index, sol::table Opts, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table set_entry -> asset no longer valid"));
				return sol::lua_nil;
			}
#if WITH_EDITORONLY_DATA
			if (!Current->Entries.IsValidIndex(Index - 1))
			{
				Session.Log(TEXT("[FAIL] proxy_table set_entry -> index out of range"));
				return sol::lua_nil;
			}
			Current->Modify();
			FProxyEntry& Entry = Current->Entries[Index - 1];
			if (UProxyAsset* Proxy = ResolveProxyAssetForLua(Opts, "proxy_asset"))
			{
				EnsureProxyGuid(Proxy);
				Entry.Proxy = Proxy;
			}
			if (Opts["key"].valid())
			{
				std::string KeyStr = Opts.get_or<std::string>("key", "");
				Entry.Key = FName(*NeoLuaStr::ToFString(KeyStr));
			}
			sol::object ValueObj = Opts.get<sol::object>("value");
			if (ValueObj.valid() && ValueObj != sol::lua_nil)
			{
				FInstancedStruct Value;
				FString Summary;
				FString Error;
				if (!BuildResultInstancedStruct(ValueObj, nullptr, Value, Summary, Error))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] proxy_table set_entry -> %s"), *Error));
					return sol::lua_nil;
				}
				Entry.ValueStruct = MoveTemp(Value);
			}
			RefreshProxyTable(Current);
			sol::table Result = Lua.create_table();
			Result["index"] = Index;
			AddProxyValueInfo(Result, Entry.ValueStruct);
			Session.Log(FString::Printf(TEXT("[OK] proxy_table set_entry -> %d"), Index));
			return Result;
#else
			Session.Log(TEXT("[FAIL] proxy_table set_entry -> editor-only data unavailable"));
			return sol::lua_nil;
#endif
		});

		AssetObj.set_function("remove_entry", [WeakTable, &Session](sol::table, int32 Index) -> bool
		{
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table remove_entry -> asset no longer valid"));
				return false;
			}
#if WITH_EDITORONLY_DATA
			if (!Current->Entries.IsValidIndex(Index - 1))
			{
				Session.Log(TEXT("[FAIL] proxy_table remove_entry -> index out of range"));
				return false;
			}
			Current->Modify();
			Current->Entries.RemoveAt(Index - 1);
			RefreshProxyTable(Current);
			Session.Log(FString::Printf(TEXT("[OK] proxy_table remove_entry -> %d"), Index));
			return true;
#else
			Session.Log(TEXT("[FAIL] proxy_table remove_entry -> editor-only data unavailable"));
			return false;
#endif
		});

		AssetObj.set_function("add_struct_output", [WeakTable, &Session](sol::table, int32 Index, sol::table Opts, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table add_struct_output -> asset no longer valid"));
				return sol::lua_nil;
			}
#if WITH_EDITORONLY_DATA
			if (!Current->Entries.IsValidIndex(Index - 1))
			{
				Session.Log(TEXT("[FAIL] proxy_table add_struct_output -> entry index out of range"));
				return sol::lua_nil;
			}
			FString StructPath;
			if (!TryGetChooserString(Opts, "struct", StructPath))
			{
				TryGetChooserString(Opts, "struct_path", StructPath);
			}
			UScriptStruct* Struct = nullptr;
			if (!TryResolveChooserStruct(StructPath, Struct))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] proxy_table add_struct_output -> struct not found: %s"), *StructPath));
				return sol::lua_nil;
			}
			sol::object ValueObj = Opts.get<sol::object>("value");
			if (!ValueObj.valid() || ValueObj == sol::lua_nil)
			{
				Session.Log(TEXT("[FAIL] proxy_table add_struct_output -> value table required"));
				return sol::lua_nil;
			}

			FProxyEntry& Entry = Current->Entries[Index - 1];
			if (!Entry.Proxy)
			{
				Session.Log(TEXT("[FAIL] proxy_table add_struct_output -> entry has no ProxyAsset"));
				return sol::lua_nil;
			}

			int32 ContextIndex = Opts.get_or("context_index", 1) - 1;
			if (!Entry.Proxy->ContextData.IsValidIndex(ContextIndex))
			{
				Session.Log(TEXT("[FAIL] proxy_table add_struct_output -> context_index out of range for ProxyAsset"));
				return sol::lua_nil;
			}
			const FInstancedStruct& ContextDef = Entry.Proxy->ContextData[ContextIndex];
			const FContextObjectTypeStruct* StructContext = ContextDef.GetPtr<FContextObjectTypeStruct>();
			if (!StructContext || StructContext->Struct != Struct)
			{
				Session.Log(TEXT("[FAIL] proxy_table add_struct_output -> target proxy context must be a matching struct context"));
				return sol::lua_nil;
			}

			const bool bRoot = Opts.get_or("root", true) || Opts.get_or("is_root", false);
			if (!bRoot)
			{
				Session.Log(TEXT("[FAIL] proxy_table add_struct_output -> only root struct bindings are supported headlessly"));
				return sol::lua_nil;
			}

			FProxyStructOutput Output;
			Output.Binding.ContextIndex = ContextIndex;
			Output.Binding.IsBoundToRoot = true;
			Output.Binding.PropertyBindingChain.Reset();
			Output.Binding.StructType = Struct;
			Output.Binding.Compile(Entry.Proxy, true);
			Output.Value.InitializeAs(Struct);
			FString Error;
			if (!NeoLuaProperty::ApplyValueToStructMemory(Struct, Output.Value.GetMutableMemory(), ValueObj, Current, Error))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] proxy_table add_struct_output -> %s"), *Error));
				return sol::lua_nil;
			}

			Current->Modify();
			Entry.OutputStructData.Add(MoveTemp(Output));
			RefreshProxyTable(Current);
			sol::table Result = Lua.create_table();
			Result["entry_index"] = Index;
			Result["struct_output_count"] = Entry.OutputStructData.Num();
			Result["runtime_key_count"] = Current->Keys.Num();
			Result["struct_outputs"] = BuildProxyStructOutputsInfo(Lua, Entry.OutputStructData);
			Session.Log(FString::Printf(TEXT("[OK] proxy_table add_struct_output -> entry %d count %d"),
				Index, Entry.OutputStructData.Num()));
			return Result;
#else
			Session.Log(TEXT("[FAIL] proxy_table add_struct_output -> editor-only data unavailable"));
			return sol::lua_nil;
#endif
		});

		AssetObj.set_function("add_inherited", [WeakTable, &Session](sol::table, const std::string& ParentPath, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table add_inherited -> asset no longer valid"));
				return sol::lua_nil;
			}
#if WITH_EDITORONLY_DATA
			FString FParentPath = NeoLuaStr::ToFString(ParentPath);
			UProxyTable* Parent = NeoLuaAsset::ResolveWithRegistry<UProxyTable>(FParentPath);
			if (!Parent)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] proxy_table add_inherited -> table not found: %s"), *FParentPath));
				return sol::lua_nil;
			}
			if (Parent == Current || Current->InheritEntriesFrom.Contains(Parent))
			{
				Session.Log(TEXT("[FAIL] proxy_table add_inherited -> duplicate or self inheritance"));
				return sol::lua_nil;
			}
			Current->Modify();
			Current->InheritEntriesFrom.Add(Parent);
			RefreshProxyTable(Current);
			sol::table Result = Lua.create_table();
			Result["index"] = Current->InheritEntriesFrom.Num();
			Result["inherited_count"] = Current->InheritEntriesFrom.Num();
			Result["runtime_key_count"] = Current->Keys.Num();
			Result["table_path"] = TCHAR_TO_UTF8(*Parent->GetPathName());
			Session.Log(FString::Printf(TEXT("[OK] proxy_table add_inherited -> %s"), *Parent->GetPathName()));
			return Result;
#else
			Session.Log(TEXT("[FAIL] proxy_table add_inherited -> editor-only data unavailable"));
			return sol::lua_nil;
#endif
		});

		AssetObj.set_function("remove_inherited", [WeakTable, &Session](sol::table, int32 Index) -> bool
		{
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table remove_inherited -> asset no longer valid"));
				return false;
			}
#if WITH_EDITORONLY_DATA
			if (!Current->InheritEntriesFrom.IsValidIndex(Index - 1))
			{
				Session.Log(TEXT("[FAIL] proxy_table remove_inherited -> index out of range"));
				return false;
			}
			Current->Modify();
			Current->InheritEntriesFrom.RemoveAt(Index - 1);
			RefreshProxyTable(Current);
			Session.Log(FString::Printf(TEXT("[OK] proxy_table remove_inherited -> %d"), Index));
			return true;
#else
			Session.Log(TEXT("[FAIL] proxy_table remove_inherited -> editor-only data unavailable"));
			return false;
#endif
		});

		AssetObj.set_function("compile", [WeakTable, &Session](sol::table, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table compile -> asset no longer valid"));
				return sol::lua_nil;
			}
			RefreshProxyTable(Current);
			sol::table Result = Lua.create_table();
			Result["runtime_key_count"] = Current->Keys.Num();
			Result["runtime_value_count"] = Current->RuntimeValues.Num();
			Session.Log(FString::Printf(TEXT("[OK] proxy_table compile -> %d runtime values"), Current->RuntimeValues.Num()));
			return Result;
		});

		AssetObj.set_function("evaluate", [WeakTable, &Session](sol::table, const std::string& ProxyPath, sol::optional<std::string> ContextPathOpt, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table evaluate -> asset no longer valid"));
				return sol::lua_nil;
			}
			UProxyAsset* Proxy = NeoLuaAsset::ResolveWithRegistry<UProxyAsset>(NeoLuaStr::ToFString(ProxyPath));
			if (!Proxy)
			{
				Session.Log(TEXT("[FAIL] proxy_table evaluate -> proxy asset not found"));
				return sol::lua_nil;
			}
			UObject* ContextObj = Current;
			if (ContextPathOpt.has_value())
			{
				FString ContextPath = NeoLuaStr::ToFString(ContextPathOpt.value());
				if (UObject* Resolved = NeoLuaAsset::ResolveWithRegistry<UObject>(ContextPath))
				{
					ContextObj = Resolved;
				}
				else
				{
					Session.Log(FString::Printf(TEXT("[FAIL] proxy_table evaluate -> context object not found: %s"), *ContextPath));
					return sol::lua_nil;
				}
			}
			RefreshProxyTable(Current);
			FChooserEvaluationContext Context(ContextObj);
			UObject* Selected = Current->FindProxyObject(Proxy->Guid, Context);
			if (!Selected)
			{
				Session.Log(TEXT("[OK] proxy_table evaluate -> nil"));
				return sol::lua_nil;
			}
			Session.Log(FString::Printf(TEXT("[OK] proxy_table evaluate -> %s"), *Selected->GetPathName()));
			return sol::make_object(Lua, TCHAR_TO_UTF8(*Selected->GetPathName()));
		});

		AssetObj.set_function("evaluate_with_struct", [WeakTable, &Session](sol::table, const std::string& ProxyPath, sol::table Opts, sol::this_state S2) -> sol::object
		{
			sol::state_view Lua(S2);
			UProxyTable* Current = WeakTable.Get();
			if (!Current)
			{
				Session.Log(TEXT("[FAIL] proxy_table evaluate_with_struct -> asset no longer valid"));
				return sol::lua_nil;
			}
			UProxyAsset* Proxy = NeoLuaAsset::ResolveWithRegistry<UProxyAsset>(NeoLuaStr::ToFString(ProxyPath));
			if (!Proxy)
			{
				Session.Log(TEXT("[FAIL] proxy_table evaluate_with_struct -> proxy asset not found"));
				return sol::lua_nil;
			}
			FString StructPath;
			if (!TryGetChooserString(Opts, "struct", StructPath))
			{
				TryGetChooserString(Opts, "struct_path", StructPath);
			}
			UScriptStruct* Struct = nullptr;
			if (!TryResolveChooserStruct(StructPath, Struct))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] proxy_table evaluate_with_struct -> struct not found: %s"), *StructPath));
				return sol::lua_nil;
			}
			FInstancedStruct ContextStruct;
			ContextStruct.InitializeAs(Struct);
			sol::object InitialObj = Opts.get<sol::object>("value");
			if (InitialObj.valid() && InitialObj != sol::lua_nil)
			{
				FString Error;
				if (!NeoLuaProperty::ApplyValueToStructMemory(Struct, ContextStruct.GetMutableMemory(), InitialObj, Current, Error))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] proxy_table evaluate_with_struct -> %s"), *Error));
					return sol::lua_nil;
				}
			}
			RefreshProxyTable(Current);
			FChooserEvaluationContext Context;
			Context.AddStructViewParam(FStructView(Struct, ContextStruct.GetMutableMemory()));
			UObject* Selected = Current->FindProxyObject(Proxy->Guid, Context);
			sol::table Result = Lua.create_table();
			if (Selected)
			{
				Result["selected_path"] = TCHAR_TO_UTF8(*Selected->GetPathName());
				Result["selected_name"] = TCHAR_TO_UTF8(*Selected->GetName());
			}
			Result["struct"] = NeoLuaProperty::ReadStructMemoryAsSol(Struct, ContextStruct.GetMemory(), Lua);
			Session.Log(FString::Printf(TEXT("[OK] proxy_table evaluate_with_struct -> %s"),
				Selected ? *Selected->GetPathName() : TEXT("nil")));
			return Result;
		});
	});
}

#else
// Chooser binding not supported on UE 5.4 — API changed significantly in 5.5
static TArray<FLuaFunctionDoc> ChooserTableDocs = {};
#endif // ENGINE_MINOR_VERSION >= 5

static void ChooserTable_TryBind(sol::state& Lua, FLuaSessionData& Session)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("Chooser")))
	{
		Session.Log(TEXT("[WARN] Chooser plugin is not loaded. Enable it in Edit > Plugins to use this feature."));
		return;
	}
	BindChooserTable(Lua, Session);
	BindProxyAssetAndTable(Lua, Session);
#else
	Session.Log(TEXT("[WARN] Chooser editing requires UE 5.5+. Binding not available in this engine version."));
#endif
}

namespace NSAIChooserExtension
{
	const TArray<FLuaFunctionDoc>& GetChooserLuaDocs()
	{
		return ChooserTableDocs;
	}

	void BindChooserLua(sol::state& Lua, FLuaSessionData& Session)
	{
		ChooserTable_TryBind(Lua, Session);
	}
}
