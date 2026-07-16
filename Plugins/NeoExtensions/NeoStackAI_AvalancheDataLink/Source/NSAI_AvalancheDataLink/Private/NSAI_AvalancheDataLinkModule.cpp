#include "Modules/ModuleManager.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "Lua/LuaActorResolver.h"
#include "Lua/LuaBindingRegistry.h"

#include "AvaDataLinkActor.h"
#include "AvaDataLinkInstance.h"
#include "AvaScene.h"
#include "Controller/RCController.h"
#include "DataLinkCoreTypes.h"
#include "DataLinkEdGraph.h"
#include "DataLinkEdGraphSchema.h"
#include "DataLinkGraph.h"
#include "DataLinkNames.h"
#include "DataLinkPin.h"
#include "DataLinkUtils.h"
#include "DataLinkProcessor.h"
#include "EdGraph/EdGraphPin.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "NSAIDataLinkEchoNode.h"
#include "Nodes/DataLinkConstant.h"
#include "Nodes/DataLinkEdNode.h"
#include "Nodes/DataLinkEdOutputNode.h"
#include "Nodes/Script/DataLinkScriptNode.h"
#include "Nodes/Script/DataLinkScriptNodeWrapper.h"
#include "Nodes/String/DataLinkReplaceString.h"
#include "Nodes/String/DataLinkNodeStringBuilder.h"
#include "Nodes/String/DataLinkStringBuilder.h"
#include "RCVirtualProperty.h"
#include "RemoteControlPreset.h"
#include "ScopedTransaction.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#include <sol/sol.hpp>

namespace
{
	const TCHAR* AvalancheDataLinkExtensionId = TEXT("neostack.avalanche_datalink");

	FNeoStackExtensionDescriptor BuildAvalancheDataLinkDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = AvalancheDataLinkExtensionId;
		Descriptor.DisplayName = TEXT("Avalanche Data Link");
		Descriptor.ModuleName = TEXT("NSAI_AvalancheDataLink");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("motion-design");
		Descriptor.Description = TEXT("Author Motion Design Data Link actors from Lua.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("motion-design"), TEXT("data-link"), TEXT("beta") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}

	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	AAvaScene* GetMotionDesignScene(bool bCreate)
	{
		UWorld* World = GetEditorWorld();
		ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
		return Level ? AAvaScene::GetScene(Level, bCreate) : nullptr;
	}

	FString ToFString(const std::string& Value)
	{
		return UTF8_TO_TCHAR(Value.c_str());
	}

	UClass* ResolveClass(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* LoadedClass = LoadObject<UClass>(nullptr, *Name))
		{
			return LoadedClass;
		}

		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (Class
				&& (Class->GetName().Equals(Name, ESearchCase::IgnoreCase)
					|| Class->GetPathName().Equals(Name, ESearchCase::IgnoreCase)
					|| Class->GetClassPathName().ToString().Equals(Name, ESearchCase::IgnoreCase)))
			{
				return Class;
			}
		}

		return nullptr;
	}

	UAvaDataLinkInstance* GetInstance(AAvaDataLinkActor* Actor, int32 LuaIndex)
	{
		if (!Actor || LuaIndex <= 0 || LuaIndex > Actor->DataLinkInstances.Num())
		{
			return nullptr;
		}
		return Actor->DataLinkInstances[LuaIndex - 1];
	}

	TArray<UDataLinkProcessor*> GetProcessors(UAvaDataLinkInstance* Instance)
	{
		TArray<UDataLinkProcessor*> Result;
		if (!Instance)
		{
			return Result;
		}

		FArrayProperty* OutputProcessorsProperty = FindFProperty<FArrayProperty>(Instance->GetClass(), UAvaDataLinkInstance::GetOutputProcessorsPropertyName());
		FObjectPropertyBase* ObjectInner = OutputProcessorsProperty ? CastField<FObjectPropertyBase>(OutputProcessorsProperty->Inner) : nullptr;
		if (!OutputProcessorsProperty || !ObjectInner)
		{
			return Result;
		}

		FScriptArrayHelper Helper(OutputProcessorsProperty, OutputProcessorsProperty->ContainerPtrToValuePtr<void>(Instance));
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			if (UDataLinkProcessor* Processor = Cast<UDataLinkProcessor>(ObjectInner->GetObjectPropertyValue(Helper.GetRawPtr(Index))))
			{
				Result.Add(Processor);
			}
		}
		return Result;
	}

	bool AddProcessor(UAvaDataLinkInstance* Instance, UDataLinkProcessor* Processor)
	{
		if (!Instance || !Processor)
		{
			return false;
		}

		FArrayProperty* OutputProcessorsProperty = FindFProperty<FArrayProperty>(Instance->GetClass(), UAvaDataLinkInstance::GetOutputProcessorsPropertyName());
		FObjectPropertyBase* ObjectInner = OutputProcessorsProperty ? CastField<FObjectPropertyBase>(OutputProcessorsProperty->Inner) : nullptr;
		if (!OutputProcessorsProperty || !ObjectInner)
		{
			return false;
		}

		Instance->Modify();
		FScriptArrayHelper Helper(OutputProcessorsProperty, OutputProcessorsProperty->ContainerPtrToValuePtr<void>(Instance));
		const int32 NewIndex = Helper.AddValue();
		ObjectInner->SetObjectPropertyValue(Helper.GetRawPtr(NewIndex), Processor);
		return true;
	}

	bool SetProcessors(UAvaDataLinkInstance* Instance, const TArray<UDataLinkProcessor*>& Processors)
	{
		if (!Instance)
		{
			return false;
		}

		FArrayProperty* OutputProcessorsProperty = FindFProperty<FArrayProperty>(Instance->GetClass(), UAvaDataLinkInstance::GetOutputProcessorsPropertyName());
		FObjectPropertyBase* ObjectInner = OutputProcessorsProperty ? CastField<FObjectPropertyBase>(OutputProcessorsProperty->Inner) : nullptr;
		if (!OutputProcessorsProperty || !ObjectInner)
		{
			return false;
		}

		Instance->Modify();
		FScriptArrayHelper Helper(OutputProcessorsProperty, OutputProcessorsProperty->ContainerPtrToValuePtr<void>(Instance));
		Helper.EmptyValues();
		for (UDataLinkProcessor* Processor : Processors)
		{
			if (!Processor)
			{
				continue;
			}
			const int32 NewIndex = Helper.AddValue();
			ObjectInner->SetObjectPropertyValue(Helper.GetRawPtr(NewIndex), Processor);
		}
		return true;
	}

	bool RemoveProcessor(UAvaDataLinkInstance* Instance, int32 LuaIndex)
	{
		TArray<UDataLinkProcessor*> Processors = GetProcessors(Instance);
		if (!Processors.IsValidIndex(LuaIndex - 1))
		{
			return false;
		}
		Processors.RemoveAt(LuaIndex - 1);
		return SetProcessors(Instance, Processors);
	}

	bool MoveProcessor(UAvaDataLinkInstance* Instance, int32 FromLuaIndex, int32 ToLuaIndex)
	{
		TArray<UDataLinkProcessor*> Processors = GetProcessors(Instance);
		const int32 FromIndex = FromLuaIndex - 1;
		const int32 ToIndex = ToLuaIndex - 1;
		if (!Processors.IsValidIndex(FromIndex) || !Processors.IsValidIndex(ToIndex))
		{
			return false;
		}

		UDataLinkProcessor* Processor = Processors[FromIndex];
		Processors.RemoveAt(FromIndex);
		Processors.Insert(Processor, ToIndex);
		return SetProcessors(Instance, Processors);
	}

	FProperty* FindStructField(FStructProperty* StructProperty, const TCHAR* Name)
	{
		return StructProperty && StructProperty->Struct ? FindFProperty<FProperty>(StructProperty->Struct, Name) : nullptr;
	}

	int32 GetInputDataCount(UAvaDataLinkInstance* Instance)
	{
		FStructProperty* DataLinkInstanceProperty = Instance
			? FindFProperty<FStructProperty>(Instance->GetClass(), UAvaDataLinkInstance::GetDataLinkInstancePropertyName())
			: nullptr;
		FArrayProperty* InputDataProperty = CastField<FArrayProperty>(FindStructField(DataLinkInstanceProperty, TEXT("InputData")));
		if (!DataLinkInstanceProperty || !InputDataProperty)
		{
			return 0;
		}

		void* DataLinkInstancePtr = DataLinkInstanceProperty->ContainerPtrToValuePtr<void>(Instance);
		FScriptArrayHelper Helper(InputDataProperty, InputDataProperty->ContainerPtrToValuePtr<void>(DataLinkInstancePtr));
		return Helper.Num();
	}

	TArray<FDataLinkInputData>* GetInputDataArray(UAvaDataLinkInstance* Instance)
	{
		FStructProperty* DataLinkInstanceProperty = Instance
			? FindFProperty<FStructProperty>(Instance->GetClass(), UAvaDataLinkInstance::GetDataLinkInstancePropertyName())
			: nullptr;
		FArrayProperty* InputDataProperty = CastField<FArrayProperty>(FindStructField(DataLinkInstanceProperty, TEXT("InputData")));
		if (!DataLinkInstanceProperty || !InputDataProperty)
		{
			return nullptr;
		}
		void* DataLinkInstancePtr = DataLinkInstanceProperty->ContainerPtrToValuePtr<void>(Instance);
		return InputDataProperty->ContainerPtrToValuePtr<TArray<FDataLinkInputData>>(DataLinkInstancePtr);
	}

	sol::table BuildStructFieldSummary(sol::state_view Lua, const FInstancedStruct& Data)
	{
		sol::table Fields = Lua.create_table();
		const UScriptStruct* Struct = Data.GetScriptStruct();
		const void* Memory = Data.GetMemory();
		if (!Struct || !Memory)
		{
			return Fields;
		}

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Memory);
			if (const FStrProperty* StrProperty = CastField<FStrProperty>(Property))
			{
				Fields[TCHAR_TO_UTF8(*Property->GetName())] = TCHAR_TO_UTF8(*StrProperty->GetPropertyValue(ValuePtr));
			}
			else if (const FNameProperty* NameProperty = CastField<FNameProperty>(Property))
			{
				Fields[TCHAR_TO_UTF8(*Property->GetName())] = TCHAR_TO_UTF8(*NameProperty->GetPropertyValue(ValuePtr).ToString());
			}
			else if (const FTextProperty* TextProperty = CastField<FTextProperty>(Property))
			{
				Fields[TCHAR_TO_UTF8(*Property->GetName())] = TCHAR_TO_UTF8(*TextProperty->GetPropertyValue(ValuePtr).ToString());
			}
			else if (const FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				Fields[TCHAR_TO_UTF8(*Property->GetName())] = BoolProperty->GetPropertyValue(ValuePtr);
			}
			else if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
			{
				Fields[TCHAR_TO_UTF8(*Property->GetName())] = IntProperty->GetPropertyValue(ValuePtr);
			}
			else if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
			{
				Fields[TCHAR_TO_UTF8(*Property->GetName())] = FloatProperty->GetPropertyValue(ValuePtr);
			}
			else if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
			{
				Fields[TCHAR_TO_UTF8(*Property->GetName())] = DoubleProperty->GetPropertyValue(ValuePtr);
			}
			else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
			{
				const FStructProperty* StructInner = CastField<FStructProperty>(ArrayProperty->Inner);
				if (!StructInner || !StructInner->Struct)
				{
					continue;
				}

				sol::table Rows = Lua.create_table();
				FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
				for (int32 RowIndex = 0; RowIndex < Helper.Num(); ++RowIndex)
				{
					void* RowPtr = Helper.GetRawPtr(RowIndex);
					sol::table Row = Lua.create_table();
					for (TFieldIterator<FProperty> RowIt(StructInner->Struct); RowIt; ++RowIt)
					{
						const FProperty* RowProperty = *RowIt;
						if (!RowProperty)
						{
							continue;
						}
						const void* RowValuePtr = RowProperty->ContainerPtrToValuePtr<void>(RowPtr);
						if (const FStrProperty* RowStrProperty = CastField<FStrProperty>(RowProperty))
						{
							Row[TCHAR_TO_UTF8(*RowProperty->GetName())] = TCHAR_TO_UTF8(*RowStrProperty->GetPropertyValue(RowValuePtr));
						}
						else if (const FNameProperty* RowNameProperty = CastField<FNameProperty>(RowProperty))
						{
							Row[TCHAR_TO_UTF8(*RowProperty->GetName())] = TCHAR_TO_UTF8(*RowNameProperty->GetPropertyValue(RowValuePtr).ToString());
						}
						else if (const FTextProperty* RowTextProperty = CastField<FTextProperty>(RowProperty))
						{
							Row[TCHAR_TO_UTF8(*RowProperty->GetName())] = TCHAR_TO_UTF8(*RowTextProperty->GetPropertyValue(RowValuePtr).ToString());
						}
						else if (const FBoolProperty* RowBoolProperty = CastField<FBoolProperty>(RowProperty))
						{
							Row[TCHAR_TO_UTF8(*RowProperty->GetName())] = RowBoolProperty->GetPropertyValue(RowValuePtr);
						}
						else if (const FIntProperty* RowIntProperty = CastField<FIntProperty>(RowProperty))
						{
							Row[TCHAR_TO_UTF8(*RowProperty->GetName())] = RowIntProperty->GetPropertyValue(RowValuePtr);
						}
						else if (const FFloatProperty* RowFloatProperty = CastField<FFloatProperty>(RowProperty))
						{
							Row[TCHAR_TO_UTF8(*RowProperty->GetName())] = RowFloatProperty->GetPropertyValue(RowValuePtr);
						}
						else if (const FDoubleProperty* RowDoubleProperty = CastField<FDoubleProperty>(RowProperty))
						{
							Row[TCHAR_TO_UTF8(*RowProperty->GetName())] = RowDoubleProperty->GetPropertyValue(RowValuePtr);
						}
					}
					Rows[RowIndex + 1] = Row;
				}
				Fields[TCHAR_TO_UTF8(*Property->GetName())] = Rows;
			}
		}

		return Fields;
	}

	bool SetSimplePropertyValue(FProperty* Property, void* ContainerPtr, const sol::object& Value, FString& OutError)
	{
		if (!Property || !ContainerPtr)
		{
			OutError = TEXT("property or container unavailable");
			return false;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerPtr);
		if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
		{
			if (!Value.is<std::string>())
			{
				OutError = FString::Printf(TEXT("field '%s' requires a string value"), *Property->GetName());
				return false;
			}
			StrProperty->SetPropertyValue(ValuePtr, ToFString(Value.as<std::string>()));
			return true;
		}
		if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
		{
			if (!Value.is<std::string>())
			{
				OutError = FString::Printf(TEXT("field '%s' requires a string value"), *Property->GetName());
				return false;
			}
			NameProperty->SetPropertyValue(ValuePtr, FName(*ToFString(Value.as<std::string>())));
			return true;
		}
		if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
		{
			if (!Value.is<std::string>())
			{
				OutError = FString::Printf(TEXT("field '%s' requires a string value"), *Property->GetName());
				return false;
			}
			TextProperty->SetPropertyValue(ValuePtr, FText::FromString(ToFString(Value.as<std::string>())));
			return true;
		}
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			if (!Value.is<bool>())
			{
				OutError = FString::Printf(TEXT("field '%s' requires a boolean value"), *Property->GetName());
				return false;
			}
			BoolProperty->SetPropertyValue(ValuePtr, Value.as<bool>());
			return true;
		}
		if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
		{
			if (!Value.is<int32>())
			{
				OutError = FString::Printf(TEXT("field '%s' requires an integer value"), *Property->GetName());
				return false;
			}
			IntProperty->SetPropertyValue(ValuePtr, Value.as<int32>());
			return true;
		}
		if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
		{
			if (!Value.is<double>() && !Value.is<int32>())
			{
				OutError = FString::Printf(TEXT("field '%s' requires a numeric value"), *Property->GetName());
				return false;
			}
			FloatProperty->SetPropertyValue(ValuePtr, Value.is<double>() ? static_cast<float>(Value.as<double>()) : static_cast<float>(Value.as<int32>()));
			return true;
		}
		if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
		{
			if (!Value.is<double>() && !Value.is<int32>())
			{
				OutError = FString::Printf(TEXT("field '%s' requires a numeric value"), *Property->GetName());
				return false;
			}
			DoubleProperty->SetPropertyValue(ValuePtr, Value.is<double>() ? Value.as<double>() : static_cast<double>(Value.as<int32>()));
			return true;
		}

		OutError = FString::Printf(TEXT("field '%s' type is not supported for Data Link input authoring"), *Property->GetName());
		return false;
	}

	bool SetStructFieldValue(FInstancedStruct& Data, const FString& FieldName, const sol::object& Value, FString& OutError)
	{
		const UScriptStruct* Struct = Data.GetScriptStruct();
		void* Memory = Data.GetMutableMemory();
		if (!Struct || !Memory)
		{
			OutError = TEXT("input row has no valid struct data");
			return false;
		}

		FProperty* Property = FindFProperty<FProperty>(Struct, *FieldName);
		if (!Property)
		{
			OutError = FString::Printf(TEXT("field '%s' was not found on %s"), *FieldName, *Struct->GetName());
			return false;
		}

		if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FStructProperty* StructInner = CastField<FStructProperty>(ArrayProperty->Inner);
			if (!StructInner || !StructInner->Struct)
			{
				OutError = FString::Printf(TEXT("field '%s' only supports arrays of structs"), *FieldName);
				return false;
			}
			if (!Value.is<sol::table>())
			{
				OutError = FString::Printf(TEXT("field '%s' requires an array table"), *FieldName);
				return false;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Memory);
			FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
			Helper.EmptyValues();
			sol::table Rows = Value.as<sol::table>();
			for (auto& RowPair : Rows)
			{
				if (!RowPair.second.is<sol::table>())
				{
					continue;
				}
				const int32 NewIndex = Helper.AddValue();
				void* RowPtr = Helper.GetRawPtr(NewIndex);
				sol::table RowTable = RowPair.second.as<sol::table>();
				for (auto& FieldPair : RowTable)
				{
					if (!FieldPair.first.is<std::string>())
					{
						continue;
					}
					const FString EntryFieldName = ToFString(FieldPair.first.as<std::string>());
					FProperty* EntryProperty = FindFProperty<FProperty>(StructInner->Struct, *EntryFieldName);
					if (!EntryProperty)
					{
						OutError = FString::Printf(TEXT("field '%s.%s' was not found on %s"), *FieldName, *EntryFieldName, *StructInner->Struct->GetName());
						return false;
					}
					if (!SetSimplePropertyValue(EntryProperty, RowPtr, FieldPair.second, OutError))
					{
						return false;
					}
				}
			}
			return true;
		}

		return SetSimplePropertyValue(Property, Memory, Value, OutError);
	}

	bool ConfigureInputDataRows(UAvaDataLinkInstance* Instance, const sol::table& Params, FString& OutError)
	{
		TArray<FDataLinkInputData>* InputData = GetInputDataArray(Instance);
		if (!InputData)
		{
			OutError = TEXT("input data array unavailable");
			return false;
		}

		sol::optional<sol::table> InputsOpt = Params.get<sol::optional<sol::table>>("inputs");
		if (!InputsOpt.has_value())
		{
			return true;
		}

		Instance->Modify();
		for (auto& Pair : InputsOpt.value())
		{
			if (!Pair.second.is<sol::table>())
			{
				continue;
			}

			sol::table InputTable = Pair.second.as<sol::table>();
			int32 RowIndex = InputTable.get_or("index", 0) - 1;
			if (RowIndex < 0)
			{
				const FString Name = ToFString(InputTable.get_or<std::string>("name", ""));
				if (Name.IsEmpty())
				{
					OutError = TEXT("input row requires index or name");
					return false;
				}
				for (int32 Index = 0; Index < InputData->Num(); ++Index)
				{
					if ((*InputData)[Index].DisplayName.ToString().Equals(Name, ESearchCase::IgnoreCase))
					{
						RowIndex = Index;
						break;
					}
				}
			}
			if (!InputData->IsValidIndex(RowIndex))
			{
				OutError = FString::Printf(TEXT("input row index %d is out of range"), RowIndex + 1);
				return false;
			}

			FDataLinkInputData& Row = (*InputData)[RowIndex];
			if (sol::optional<sol::object> Value = InputTable.get<sol::optional<sol::object>>("value"))
			{
				if (!SetStructFieldValue(Row.Data, TEXT("Value"), *Value, OutError))
				{
					return false;
				}
			}
			if (sol::optional<sol::table> Fields = InputTable.get<sol::optional<sol::table>>("fields"))
			{
				for (auto& FieldPair : *Fields)
				{
					if (!FieldPair.first.is<std::string>())
					{
						continue;
					}
					if (!SetStructFieldValue(Row.Data, ToFString(FieldPair.first.as<std::string>()), FieldPair.second, OutError))
					{
						return false;
					}
				}
			}
		}

		return true;
	}

		UObject* GetDataLinkGraph(UAvaDataLinkInstance* Instance)
		{
			FStructProperty* DataLinkInstanceProperty = Instance
				? FindFProperty<FStructProperty>(Instance->GetClass(), UAvaDataLinkInstance::GetDataLinkInstancePropertyName())
			: nullptr;
		FObjectPropertyBase* GraphProperty = CastField<FObjectPropertyBase>(FindStructField(DataLinkInstanceProperty, TEXT("DataLinkGraph")));
		if (!DataLinkInstanceProperty || !GraphProperty)
		{
			return nullptr;
		}

			void* DataLinkInstancePtr = DataLinkInstanceProperty->ContainerPtrToValuePtr<void>(Instance);
			return GraphProperty->GetObjectPropertyValue(GraphProperty->ContainerPtrToValuePtr<void>(DataLinkInstancePtr));
		}

		UDataLinkGraph* ResolveDataLinkGraphAsset(const FString& Path)
		{
			if (Path.IsEmpty())
			{
				return nullptr;
			}
			if (UDataLinkGraph* Graph = LoadObject<UDataLinkGraph>(nullptr, *Path))
			{
				return Graph;
			}
			if (Path.StartsWith(TEXT("/")) && !Path.Contains(TEXT(".")))
			{
				const FString AssetName = FPackageName::GetLongPackageAssetName(Path);
				if (!AssetName.IsEmpty())
				{
					return LoadObject<UDataLinkGraph>(nullptr, *(Path + TEXT(".") + AssetName));
				}
			}
			return nullptr;
		}

		bool SetDataLinkGraph(UAvaDataLinkInstance* Instance, UDataLinkGraph* Graph)
		{
			FStructProperty* DataLinkInstanceProperty = Instance
				? FindFProperty<FStructProperty>(Instance->GetClass(), UAvaDataLinkInstance::GetDataLinkInstancePropertyName())
				: nullptr;
			FObjectPropertyBase* GraphProperty = CastField<FObjectPropertyBase>(FindStructField(DataLinkInstanceProperty, TEXT("DataLinkGraph")));
			FArrayProperty* InputDataProperty = CastField<FArrayProperty>(FindStructField(DataLinkInstanceProperty, TEXT("InputData")));
			if (!Instance || !DataLinkInstanceProperty || !GraphProperty || !InputDataProperty)
			{
				return false;
			}

			Instance->Modify();
			void* DataLinkInstancePtr = DataLinkInstanceProperty->ContainerPtrToValuePtr<void>(Instance);
			GraphProperty->SetObjectPropertyValue(GraphProperty->ContainerPtrToValuePtr<void>(DataLinkInstancePtr), Graph);
			TArray<FDataLinkInputData>* InputData = InputDataProperty->ContainerPtrToValuePtr<TArray<FDataLinkInputData>>(DataLinkInstancePtr);
			if (!InputData)
			{
				return false;
			}
			UE::DataLink::SetInputData(Graph, *InputData);
			Instance->MarkPackageDirty();
			return GetDataLinkGraph(Instance) == Graph;
		}

	UDataLinkGraph* ResolveGraphAsset(const sol::table& AssetObj)
	{
		const std::string Path = AssetObj.get_or<std::string>("path", "");
		if (Path.empty())
		{
			return nullptr;
		}
		return LoadObject<UDataLinkGraph>(nullptr, UTF8_TO_TCHAR(Path.c_str()));
	}

	UScriptStruct* ResolveScriptStruct(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return FDataLinkString::StaticStruct();
		}
		if (UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *Name))
		{
			return Struct;
		}
		for (TObjectIterator<UScriptStruct> It; It; ++It)
		{
			UScriptStruct* Struct = *It;
			if (Struct
				&& (Struct->GetName().Equals(Name, ESearchCase::IgnoreCase)
					|| Struct->GetPathName().Equals(Name, ESearchCase::IgnoreCase)))
			{
				return Struct;
			}
		}
		return nullptr;
	}

	TArray<UDataLinkEdNode*> GetDataLinkEdNodes(UDataLinkGraph* Graph)
	{
		TArray<UDataLinkEdNode*> Result;
		UDataLinkEdGraph* EdGraph = Graph ? Cast<UDataLinkEdGraph>(Graph->GetEdGraph()) : nullptr;
		if (!EdGraph)
		{
			return Result;
		}
		for (UEdGraphNode* Node : EdGraph->Nodes)
		{
			if (UDataLinkEdNode* EdNode = Cast<UDataLinkEdNode>(Node))
			{
				if (!EdNode->IsA<UDataLinkEdOutputNode>())
				{
					Result.Add(EdNode);
				}
			}
		}
		return Result;
	}

	UDataLinkEdNode* FindDataLinkEdNodeByLuaIndex(UDataLinkGraph* Graph, int32 LuaIndex)
	{
		const TArray<UDataLinkEdNode*> Nodes = GetDataLinkEdNodes(Graph);
		return Nodes.IsValidIndex(LuaIndex - 1) ? Nodes[LuaIndex - 1] : nullptr;
	}

	TArray<FString> ReadStringArray(sol::table Values)
	{
		TArray<FString> Result;
		for (int32 LuaIndex = 1;; ++LuaIndex)
		{
			sol::object Entry = Values[LuaIndex];
			if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
			{
				break;
			}
			if (Entry.is<std::string>())
			{
				Result.Add(ToFString(Entry.as<std::string>()));
			}
		}
		return Result;
	}

	bool ConfigureStringBuilderNode(UDataLinkNodeStringBuilder* Node, const TArray<FString>& Segments)
	{
		FArrayProperty* SegmentsProperty = Node ? FindFProperty<FArrayProperty>(Node->GetClass(), TEXT("Segments")) : nullptr;
		FArrayProperty* TokensProperty = Node ? FindFProperty<FArrayProperty>(Node->GetClass(), TEXT("Tokens")) : nullptr;
		if (!Node || !SegmentsProperty || !TokensProperty)
		{
			return false;
		}

		TArray<FString>* NodeSegments = SegmentsProperty->ContainerPtrToValuePtr<TArray<FString>>(Node);
		TArray<FDataLinkStringBuilderToken>* Tokens = TokensProperty->ContainerPtrToValuePtr<TArray<FDataLinkStringBuilderToken>>(Node);
		if (!NodeSegments || !Tokens)
		{
			return false;
		}

		*NodeSegments = Segments;
		FDataLinkStringBuilder::GatherTokens(*NodeSegments, *Tokens);
		return true;
	}

	bool ConfigureObjectScalarProperties(UObject* Object, sol::table Properties, FString& OutError)
	{
		if (!Object)
		{
			OutError = TEXT("object unavailable");
			return false;
		}

		for (auto& Pair : Properties)
		{
			if (!Pair.first.is<std::string>())
			{
				continue;
			}

			const FString PropertyName = ToFString(Pair.first.as<std::string>());
			FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), *PropertyName);
			if (!Property)
			{
				OutError = FString::Printf(TEXT("property '%s' was not found on %s"), *PropertyName, *Object->GetClass()->GetName());
				return false;
			}

			if (!SetSimplePropertyValue(Property, Object, Pair.second, OutError))
			{
				return false;
			}
		}

		return true;
	}

	UDataLinkScriptNode* GetScriptTemplateNode(UDataLinkScriptNodeWrapper* Wrapper)
	{
		FObjectPropertyBase* TemplateNodeProperty = Wrapper
			? FindFProperty<FObjectPropertyBase>(Wrapper->GetClass(), TEXT("TemplateNode"))
			: nullptr;
		if (!TemplateNodeProperty)
		{
			return nullptr;
		}
		return Cast<UDataLinkScriptNode>(TemplateNodeProperty->GetObjectPropertyValue(TemplateNodeProperty->ContainerPtrToValuePtr<void>(Wrapper)));
	}

	bool ReadScriptPin(sol::table PinTable, FDataLinkScriptPin& OutPin, FString& OutError)
	{
		const FString PinName = ToFString(PinTable.get_or<std::string>("name", ""));
		const FString StructName = ToFString(PinTable.get_or<std::string>("struct", ""));
		if (PinName.IsEmpty())
		{
			OutError = TEXT("script pin requires a non-empty name");
			return false;
		}
		UScriptStruct* Struct = ResolveScriptStruct(StructName);
		if (!Struct)
		{
			OutError = FString::Printf(TEXT("script pin struct '%s' was not found"), *StructName);
			return false;
		}

		OutPin.Name = FName(*PinName);
		OutPin.Struct = Struct;
		return true;
	}

	bool ConfigureScriptNodePins(UDataLinkScriptNode* ScriptNode, sol::table Options, FString& OutError)
	{
		FArrayProperty* InputPinsProperty = ScriptNode ? FindFProperty<FArrayProperty>(ScriptNode->GetClass(), TEXT("InputPins")) : nullptr;
		FStructProperty* OutputPinProperty = ScriptNode ? FindFProperty<FStructProperty>(ScriptNode->GetClass(), TEXT("OutputPin")) : nullptr;
		if (!ScriptNode || !InputPinsProperty || !OutputPinProperty)
		{
			OutError = TEXT("script node pin properties unavailable");
			return false;
		}

		TArray<FDataLinkScriptPin>* InputPins = InputPinsProperty->ContainerPtrToValuePtr<TArray<FDataLinkScriptPin>>(ScriptNode);
		FDataLinkScriptPin* OutputPin = OutputPinProperty->ContainerPtrToValuePtr<FDataLinkScriptPin>(ScriptNode);
		if (!InputPins || !OutputPin)
		{
			OutError = TEXT("script node pin storage unavailable");
			return false;
		}

		InputPins->Reset();
		if (sol::optional<sol::table> Inputs = Options.get<sol::optional<sol::table>>("inputs"))
		{
			for (int32 LuaIndex = 1;; ++LuaIndex)
			{
				sol::object Entry = (*Inputs)[LuaIndex];
				if (!Entry.valid() || Entry.get_type() == sol::type::lua_nil)
				{
					break;
				}
				if (!Entry.is<sol::table>())
				{
					continue;
				}
				FDataLinkScriptPin Pin;
				if (!ReadScriptPin(Entry.as<sol::table>(), Pin, OutError))
				{
					return false;
				}
				InputPins->Add(Pin);
			}
		}

		if (!Options.get<sol::optional<sol::table>>("output"))
		{
			OutError = TEXT("script node output pin is required");
			return false;
		}

		if (!ReadScriptPin(Options.get<sol::table>("output"), *OutputPin, OutError))
		{
			return false;
		}

		return true;
	}

	TArray<FDataLinkPin>* GetMutableNodePins(UDataLinkNode* Node, const TCHAR* PropertyName)
	{
		FArrayProperty* PinsProperty = Node ? FindFProperty<FArrayProperty>(Node->GetClass(), PropertyName) : nullptr;
		if (!PinsProperty)
		{
			return nullptr;
		}
		return PinsProperty->ContainerPtrToValuePtr<TArray<FDataLinkPin>>(Node);
	}

	bool SetGraphObjectArray(UDataLinkGraph* Graph, const TCHAR* PropertyName, const TArray<UDataLinkNode*>& Values)
	{
		FArrayProperty* ArrayProperty = Graph ? FindFProperty<FArrayProperty>(Graph->GetClass(), PropertyName) : nullptr;
		FObjectPropertyBase* ObjectInner = ArrayProperty ? CastField<FObjectPropertyBase>(ArrayProperty->Inner) : nullptr;
		if (!ArrayProperty || !ObjectInner)
		{
			return false;
		}

		FScriptArrayHelper Helper(ArrayProperty, ArrayProperty->ContainerPtrToValuePtr<void>(Graph));
		Helper.EmptyValues();
		for (UDataLinkNode* Value : Values)
		{
			const int32 NewIndex = Helper.AddValue();
			ObjectInner->SetObjectPropertyValue(Helper.GetRawPtr(NewIndex), Value);
		}
		return true;
	}

	bool SetGraphObject(UDataLinkGraph* Graph, const TCHAR* PropertyName, UDataLinkNode* Value)
	{
		FObjectPropertyBase* ObjectProperty = Graph ? FindFProperty<FObjectPropertyBase>(Graph->GetClass(), PropertyName) : nullptr;
		if (!ObjectProperty)
		{
			return false;
		}
		ObjectProperty->SetObjectPropertyValue(ObjectProperty->ContainerPtrToValuePtr<void>(Graph), Value);
		return true;
	}

	bool SetGraphName(UDataLinkGraph* Graph, const TCHAR* PropertyName, FName Value)
	{
		FNameProperty* NameProperty = Graph ? FindFProperty<FNameProperty>(Graph->GetClass(), PropertyName) : nullptr;
		if (!NameProperty)
		{
			return false;
		}
		NameProperty->SetPropertyValue(NameProperty->ContainerPtrToValuePtr<void>(Graph), Value);
		return true;
	}

	bool CompileDataLinkGraphRuntime(UDataLinkGraph* Graph)
	{
		UDataLinkEdGraph* EdGraph = Graph ? Cast<UDataLinkEdGraph>(Graph->GetEdGraph()) : nullptr;
		UDataLinkEdOutputNode* OutputEdNode = EdGraph ? EdGraph->FindOutputNode() : nullptr;
		UEdGraphPin* OutputResultPin = OutputEdNode ? OutputEdNode->GetOutputResultPin() : nullptr;
		UEdGraphPin* LinkedOutputPin = OutputResultPin && !OutputResultPin->LinkedTo.IsEmpty() ? OutputResultPin->LinkedTo[0] : nullptr;
		UDataLinkEdNode* RuntimeOutputEdNode = LinkedOutputPin ? Cast<UDataLinkEdNode>(LinkedOutputPin->GetOwningNode()) : nullptr;
		if (!Graph || !EdGraph || !OutputEdNode || !OutputResultPin || !LinkedOutputPin || !RuntimeOutputEdNode)
		{
			return false;
		}

		TMap<UDataLinkEdNode*, UDataLinkNode*> CompiledByEdNode;
		TArray<UDataLinkNode*> CompiledNodes;
		for (UDataLinkEdNode* EdNode : GetDataLinkEdNodes(Graph))
		{
			UDataLinkNode* TemplateNode = EdNode ? EdNode->GetTemplateNode() : nullptr;
			if (!TemplateNode)
			{
				continue;
			}

			UDataLinkNode* CompiledNode = NewObject<UDataLinkNode>(Graph, TemplateNode->GetClass(), NAME_None, RF_NoFlags, TemplateNode);
			if (!CompiledNode)
			{
				continue;
			}

			TArray<FDataLinkPin> InputPins;
			TArray<FDataLinkPin> OutputPins;
			CompiledNode->BuildPins(InputPins, OutputPins);
			if (TArray<FDataLinkPin>* MutableInputPins = GetMutableNodePins(CompiledNode, TEXT("InputPins")))
			{
				*MutableInputPins = MoveTemp(InputPins);
			}
			if (TArray<FDataLinkPin>* MutableOutputPins = GetMutableNodePins(CompiledNode, TEXT("OutputPins")))
			{
				*MutableOutputPins = MoveTemp(OutputPins);
			}
			CompiledNode->FixupNode();

			CompiledByEdNode.Add(EdNode, CompiledNode);
			CompiledNodes.Add(CompiledNode);
		}

		UDataLinkNode* RuntimeOutputNode = CompiledByEdNode.FindRef(RuntimeOutputEdNode);
		if (!RuntimeOutputNode)
		{
			return false;
		}

		for (TPair<UDataLinkEdNode*, UDataLinkNode*>& Pair : CompiledByEdNode)
		{
			UDataLinkEdNode* EdNode = Pair.Key;
			UDataLinkNode* CompiledNode = Pair.Value;
			if (!EdNode || !CompiledNode)
			{
				continue;
			}
			EdNode->ForEachPinConnection([&CompiledByEdNode, CompiledNode](const UEdGraphPin& Pin, const UDataLinkEdNode& LinkedEdNode, const UEdGraphPin& LinkedPin)
			{
				TArray<FDataLinkPin>* Pins = Pin.Direction == EGPD_Input
					? GetMutableNodePins(CompiledNode, TEXT("InputPins"))
					: GetMutableNodePins(CompiledNode, TEXT("OutputPins"));
				if (!Pins)
				{
					return;
				}

				FDataLinkPin* CompiledPin = Pins->FindByKey(Pin.PinName);
				UDataLinkNode* LinkedNode = CompiledByEdNode.FindRef(const_cast<UDataLinkEdNode*>(&LinkedEdNode));
				if (!CompiledPin || !LinkedNode)
				{
					return;
				}

				const TArray<FDataLinkPin>* LinkedPins = Pin.Direction == EGPD_Input
					? GetMutableNodePins(LinkedNode, TEXT("OutputPins"))
					: GetMutableNodePins(LinkedNode, TEXT("InputPins"));
				const int32 LinkedIndex = LinkedPins
					? LinkedPins->IndexOfByPredicate([&LinkedPin](const FDataLinkPin& Candidate) { return Candidate.Name == LinkedPin.PinName; })
					: INDEX_NONE;
				CompiledPin->LinkedNode = LinkedNode;
				CompiledPin->LinkedIndex = LinkedIndex;
			});
		}

		TArray<UDataLinkNode*> InputNodes;
		for (UDataLinkNode* CompiledNode : CompiledNodes)
		{
			TArray<FDataLinkPin>* InputPins = GetMutableNodePins(CompiledNode, TEXT("InputPins"));
			if (!InputPins || InputPins->IsEmpty())
			{
				InputNodes.AddUnique(CompiledNode);
				continue;
			}
			for (const FDataLinkPin& Pin : *InputPins)
			{
				if (!Pin.LinkedNode)
				{
					InputNodes.AddUnique(CompiledNode);
					break;
				}
			}
		}

		Graph->Modify();
		const bool bSetNodes = SetGraphObjectArray(Graph, TEXT("Nodes"), CompiledNodes);
		const bool bSetInputs = SetGraphObjectArray(Graph, TEXT("InputNodes"), InputNodes);
		const bool bSetOutput = SetGraphObject(Graph, TEXT("OutputNode"), RuntimeOutputNode);
		const bool bSetPin = SetGraphName(Graph, TEXT("OutputPinName"), LinkedOutputPin->PinName);
		if (bSetNodes && bSetInputs && bSetOutput && bSetPin)
		{
			Graph->MarkPackageDirty();
			return true;
		}
		return false;
	}

	sol::table BuildPinArray(sol::state_view Lua, TConstArrayView<FDataLinkPin> Pins)
	{
		sol::table Result = Lua.create_table();
		int32 Count = 0;
		for (const FDataLinkPin& Pin : Pins)
		{
			sol::table PinInfo = Lua.create_table();
			PinInfo["name"] = TCHAR_TO_UTF8(*Pin.Name.ToString());
			PinInfo["struct"] = Pin.Struct ? TCHAR_TO_UTF8(*Pin.Struct->GetPathName()) : "";
			PinInfo["has_linked_node"] = Pin.LinkedNode != nullptr;
			PinInfo["linked_index"] = Pin.LinkedIndex;
			Result[++Count] = PinInfo;
		}
		return Result;
	}

	sol::table BuildDataLinkEdNodeInfo(sol::state_view Lua, UDataLinkEdNode* EdNode, int32 Index)
	{
		sol::table Result = Lua.create_table();
		Result["index"] = Index;
		Result["class"] = EdNode ? TCHAR_TO_UTF8(*EdNode->GetClass()->GetName()) : "";
		Result["template_class"] = EdNode && EdNode->GetTemplateNode() ? TCHAR_TO_UTF8(*EdNode->GetTemplateNode()->GetClass()->GetName()) : "";
		Result["script_node_class"] = "";
		Result["x"] = EdNode ? EdNode->NodePosX : 0;
		Result["y"] = EdNode ? EdNode->NodePosY : 0;
		if (UDataLinkScriptNodeWrapper* Wrapper = EdNode ? Cast<UDataLinkScriptNodeWrapper>(EdNode->GetTemplateNode()) : nullptr)
		{
			if (FClassProperty* NodeClassProperty = FindFProperty<FClassProperty>(Wrapper->GetClass(), UDataLinkScriptNodeWrapper::GetNodeClassPropertyName()))
			{
				if (UClass* ScriptNodeClass = Cast<UClass>(NodeClassProperty->GetPropertyValue(NodeClassProperty->ContainerPtrToValuePtr<void>(Wrapper))))
				{
					Result["script_node_class"] = TCHAR_TO_UTF8(*ScriptNodeClass->GetName());
				}
			}
		}
		int32 InputCount = 0;
		int32 OutputCount = 0;
		sol::table Inputs = Lua.create_table();
		sol::table Outputs = Lua.create_table();
		if (EdNode)
		{
			for (UEdGraphPin* Pin : EdNode->Pins)
			{
				if (!Pin)
				{
					continue;
				}
				sol::table PinInfo = Lua.create_table();
				PinInfo["name"] = TCHAR_TO_UTF8(*Pin->PinName.ToString());
				UObject* PinStruct = Pin->PinType.PinSubCategoryObject.Get();
				PinInfo["struct"] = PinStruct ? TCHAR_TO_UTF8(*PinStruct->GetPathName()) : "";
				PinInfo["link_count"] = Pin->LinkedTo.Num();
				if (Pin->Direction == EGPD_Input)
				{
					Inputs[++InputCount] = PinInfo;
				}
				else if (Pin->Direction == EGPD_Output)
				{
					Outputs[++OutputCount] = PinInfo;
				}
			}
		}
		Result["input_count"] = InputCount;
		Result["output_count"] = OutputCount;
		Result["inputs"] = Inputs;
		Result["outputs"] = Outputs;
		return Result;
	}

	sol::table BuildDataLinkGraphInfo(sol::state_view Lua, UDataLinkGraph* Graph)
	{
		sol::table Result = Lua.create_table();
		Result["valid"] = Graph != nullptr;
		Result["class"] = Graph ? TCHAR_TO_UTF8(*Graph->GetClass()->GetName()) : "";
		Result["path"] = Graph ? TCHAR_TO_UTF8(*Graph->GetPathName()) : "";
		UDataLinkEdGraph* EdGraph = Graph ? Cast<UDataLinkEdGraph>(Graph->GetEdGraph()) : nullptr;
		Result["has_ed_graph"] = EdGraph != nullptr;

		sol::table EdNodes = Lua.create_table();
		int32 EdNodeCount = 0;
		for (UDataLinkEdNode* EdNode : GetDataLinkEdNodes(Graph))
		{
			EdNodes[++EdNodeCount] = BuildDataLinkEdNodeInfo(Lua, EdNode, EdNodeCount);
		}
		Result["ed_node_count"] = EdNodeCount;
		Result["ed_nodes"] = EdNodes;
		Result["has_output_ed_node"] = EdGraph && EdGraph->FindOutputNode();

		FArrayProperty* NodesProperty = Graph ? FindFProperty<FArrayProperty>(Graph->GetClass(), TEXT("Nodes")) : nullptr;
		FArrayProperty* InputNodesProperty = Graph ? FindFProperty<FArrayProperty>(Graph->GetClass(), TEXT("InputNodes")) : nullptr;
		const int32 CompiledCount = NodesProperty ? FScriptArrayHelper(NodesProperty, NodesProperty->ContainerPtrToValuePtr<void>(Graph)).Num() : 0;
		const int32 InputNodeCount = InputNodesProperty ? FScriptArrayHelper(InputNodesProperty, InputNodesProperty->ContainerPtrToValuePtr<void>(Graph)).Num() : 0;
		Result["compiled_node_count"] = CompiledCount;
		Result["input_node_count"] = InputNodeCount;
		Result["input_pin_count"] = Graph ? Graph->GetInputPinCount() : 0;
		const UDataLinkNode* OutputNode = Graph ? Graph->GetOutputNode() : nullptr;
		Result["has_output_node"] = OutputNode != nullptr;
		Result["output_node_class"] = OutputNode ? TCHAR_TO_UTF8(*OutputNode->GetClass()->GetName()) : "";
		Result["output_pin_name"] = Graph ? TCHAR_TO_UTF8(*Graph->GetOutputPinName().ToString()) : "";
		Result["output_pins"] = OutputNode ? BuildPinArray(Lua, OutputNode->GetOutputPins()) : Lua.create_table();
		return Result;
	}

	sol::table MappingSummary(sol::state_view Lua, void* MappingPtr, FStructProperty* MappingProperty, int32 Index)
	{
		sol::table Result = Lua.create_table();
		Result["index"] = Index;
		Result["output_field"] = "";
		Result["controller"] = "";
		Result["target_controller"] = "";
		if (!MappingPtr || !MappingProperty || !MappingProperty->Struct)
		{
			return Result;
		}

		if (FStrProperty* OutputFieldProperty = FindFProperty<FStrProperty>(MappingProperty->Struct, TEXT("OutputFieldName")))
		{
			Result["output_field"] = TCHAR_TO_UTF8(*OutputFieldProperty->GetPropertyValue(OutputFieldProperty->ContainerPtrToValuePtr<void>(MappingPtr)));
		}

		if (FStructProperty* TargetControllerProperty = FindFProperty<FStructProperty>(MappingProperty->Struct, TEXT("TargetController")))
		{
			if (FNameProperty* NameProperty = TargetControllerProperty->Struct
				? FindFProperty<FNameProperty>(TargetControllerProperty->Struct, TEXT("Name"))
				: nullptr)
			{
					void* TargetControllerPtr = TargetControllerProperty->ContainerPtrToValuePtr<void>(MappingPtr);
					const FString ControllerName = NameProperty->GetPropertyValue(NameProperty->ContainerPtrToValuePtr<void>(TargetControllerPtr)).ToString();
					Result["controller"] = TCHAR_TO_UTF8(*ControllerName);
					Result["target_controller"] = TCHAR_TO_UTF8(*ControllerName);
				}
			}
		return Result;
	}

	sol::table ProcessorSummary(sol::state_view Lua, UDataLinkProcessor* Processor, int32 Index)
	{
		sol::table Result = Lua.create_table();
		Result["index"] = Index;
		Result["valid"] = Processor != nullptr;
		Result["class"] = Processor ? TCHAR_TO_UTF8(*Processor->GetClass()->GetName()) : "";
		Result["path"] = Processor ? TCHAR_TO_UTF8(*Processor->GetPathName()) : "";

		sol::table Mappings = Lua.create_table();
		int32 MappingCount = 0;
		if (Processor)
		{
			FArrayProperty* MappingsProperty = FindFProperty<FArrayProperty>(Processor->GetClass(), TEXT("ControllerMappings"));
			FStructProperty* MappingProperty = MappingsProperty ? CastField<FStructProperty>(MappingsProperty->Inner) : nullptr;
			if (MappingsProperty && MappingProperty)
			{
				FScriptArrayHelper Helper(MappingsProperty, MappingsProperty->ContainerPtrToValuePtr<void>(Processor));
				for (int32 MappingIndex = 0; MappingIndex < Helper.Num(); ++MappingIndex)
				{
					Mappings[++MappingCount] = MappingSummary(Lua, Helper.GetRawPtr(MappingIndex), MappingProperty, MappingIndex + 1);
				}
			}
		}
		Result["mapping_count"] = MappingCount;
		Result["mappings"] = Mappings;
		return Result;
	}

	sol::table BuildInputDataRows(sol::state_view Lua, UAvaDataLinkInstance* Instance)
	{
		sol::table Rows = Lua.create_table();
		TArray<FDataLinkInputData>* InputData = GetInputDataArray(Instance);
		if (!InputData)
		{
			return Rows;
		}
		for (int32 Index = 0; Index < InputData->Num(); ++Index)
		{
			const FDataLinkInputData& Row = (*InputData)[Index];
			sol::table RowInfo = Lua.create_table();
			RowInfo["index"] = Index + 1;
			RowInfo["display_name"] = TCHAR_TO_UTF8(*Row.DisplayName.ToString());
			RowInfo["struct"] = Row.Data.GetScriptStruct() ? TCHAR_TO_UTF8(*Row.Data.GetScriptStruct()->GetPathName()) : "";
			RowInfo["is_valid"] = Row.Data.IsValid();
			RowInfo["fields"] = BuildStructFieldSummary(Lua, Row.Data);
			Rows[Index + 1] = RowInfo;
		}
		return Rows;
	}

	sol::table InstanceSummary(sol::state_view Lua, UAvaDataLinkInstance* Instance, int32 Index)
	{
		sol::table Result = Lua.create_table();
		Result["index"] = Index;
		Result["valid"] = Instance != nullptr;
		Result["class"] = Instance ? TCHAR_TO_UTF8(*Instance->GetClass()->GetName()) : "";
		Result["path"] = Instance ? TCHAR_TO_UTF8(*Instance->GetPathName()) : "";
		Result["input_data_count"] = GetInputDataCount(Instance);
		Result["input_data"] = BuildInputDataRows(Lua, Instance);
		if (UObject* Graph = GetDataLinkGraph(Instance))
		{
			Result["graph"] = TCHAR_TO_UTF8(*Graph->GetPathName());
			Result["has_graph"] = true;
		}
		else
		{
			Result["graph"] = "";
			Result["has_graph"] = false;
		}

		sol::table Processors = Lua.create_table();
		TArray<UDataLinkProcessor*> ProcessorObjects = GetProcessors(Instance);
		for (int32 ProcessorIndex = 0; ProcessorIndex < ProcessorObjects.Num(); ++ProcessorIndex)
		{
			Processors[ProcessorIndex + 1] = ProcessorSummary(Lua, ProcessorObjects[ProcessorIndex], ProcessorIndex + 1);
		}
		Result["processor_count"] = ProcessorObjects.Num();
		Result["processors"] = Processors;
		return Result;
	}

	sol::table ActorSummary(sol::state_view Lua, AAvaDataLinkActor* Actor)
	{
		sol::table Result = Lua.create_table();
		Result["valid"] = Actor != nullptr;
		Result["actor"] = Actor ? TCHAR_TO_UTF8(*Actor->GetActorLabel()) : "";
		Result["class"] = Actor ? TCHAR_TO_UTF8(*Actor->GetClass()->GetName()) : "";
		Result["execute_on_begin_play"] = Actor ? Actor->bExecuteOnBeginPlay : false;

		sol::table Instances = Lua.create_table();
		int32 Count = 0;
		if (Actor)
		{
			for (UAvaDataLinkInstance* Instance : Actor->DataLinkInstances)
			{
				Instances[++Count] = InstanceSummary(Lua, Instance, Count);
			}
		}
		Result["instance_count"] = Count;
		Result["instances"] = Instances;
		return Result;
	}

	URemoteControlPreset* GetMotionDesignRemoteControlPreset(bool bCreate)
	{
		AAvaScene* Scene = GetMotionDesignScene(bCreate);
		return Scene ? Scene->GetRemoteControlPreset() : nullptr;
	}

	sol::table ControllerSummary(sol::state_view Lua, URCVirtualPropertyBase* Controller)
	{
		sol::table Result = Lua.create_table();
		Result["valid"] = Controller != nullptr;
		Result["class"] = Controller ? TCHAR_TO_UTF8(*Controller->GetClass()->GetName()) : "";
		Result["property_name"] = Controller ? TCHAR_TO_UTF8(*Controller->GetPropertyName().ToString()) : "";
		Result["display_name"] = Controller ? TCHAR_TO_UTF8(*Controller->DisplayName.ToString()) : "";
		Result["id"] = Controller ? TCHAR_TO_UTF8(*Controller->Id.ToString(EGuidFormats::DigitsWithHyphens)) : "";
		Result["value_type"] = Controller ? static_cast<int32>(Controller->GetValueType()) : 0;
		FString Value;
		const bool bHasStringValue = Controller ? Controller->GetValueString(Value) : false;
		Result["has_string_value"] = bHasStringValue;
		Result["value"] = bHasStringValue ? TCHAR_TO_UTF8(*Value) : "";
		return Result;
	}

	sol::table RemoteControlSummary(sol::state_view Lua, URemoteControlPreset* Preset)
	{
		sol::table Result = Lua.create_table();
		Result["valid"] = Preset != nullptr;
		Result["preset"] = Preset ? TCHAR_TO_UTF8(*Preset->GetPathName()) : "";
		Result["controller_count"] = Preset ? Preset->GetNumControllers() : 0;

		sol::table Controllers = Lua.create_table();
		int32 Count = 0;
		if (Preset)
		{
			for (URCVirtualPropertyBase* Controller : Preset->GetControllers())
			{
				Controllers[++Count] = ControllerSummary(Lua, Controller);
			}
		}
		Result["controllers"] = Controllers;
		return Result;
	}

	URCVirtualPropertyBase* FindControllerByName(URemoteControlPreset* Preset, const FString& Name)
	{
		return Preset && !Name.IsEmpty() ? Preset->GetControllerByDisplayName(FName(*Name)) : nullptr;
	}

	URCVirtualPropertyBase* EnsureStringController(URemoteControlPreset* Preset, const FString& Name, const FString& Value, bool bSetValue)
	{
		if (!Preset || Name.IsEmpty())
		{
			return nullptr;
		}

		URCVirtualPropertyBase* Controller = FindControllerByName(Preset, Name);
		if (!Controller)
		{
			Controller = Preset->AddController(URCController::StaticClass(), EPropertyBagPropertyType::String, nullptr, FName(*Name));
			if (Controller)
			{
				Preset->SetControllerDisplayName(Controller->Id, FName(*Name));
			}
		}

		if (Controller && bSetValue)
		{
			Controller->SetValueString(Value);
			Preset->OnControllerModified().Broadcast(Preset, { Controller->Id });
		}
		return Controller;
	}

	bool RemoveControllerByName(URemoteControlPreset* Preset, const FString& Name)
	{
		URCVirtualPropertyBase* Controller = FindControllerByName(Preset, Name);
		return Preset && Controller ? Preset->RemoveController(Controller->GetPropertyName()) : false;
	}

	UClass* ResolveProcessorClass(const FString& Type)
	{
		if (Type.IsEmpty() || Type.Equals(TEXT("remote_control"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("rc"), ESearchCase::IgnoreCase))
		{
			return ResolveClass(TEXT("/Script/AvalancheDataLink.AvaDataLinkRCProcessor"));
		}
		return ResolveClass(Type);
	}

	bool ConfigureMappings(UDataLinkProcessor* Processor, const sol::table& Params, FString& OutError)
	{
		if (!Processor)
		{
			OutError = TEXT("processor is null");
			return false;
		}

		FArrayProperty* MappingsProperty = FindFProperty<FArrayProperty>(Processor->GetClass(), TEXT("ControllerMappings"));
		FStructProperty* MappingProperty = MappingsProperty ? CastField<FStructProperty>(MappingsProperty->Inner) : nullptr;
		if (!MappingsProperty || !MappingProperty || !MappingProperty->Struct)
		{
			OutError = FString::Printf(TEXT("processor '%s' does not expose ControllerMappings"), *Processor->GetClass()->GetName());
			return false;
		}

		sol::optional<sol::table> MappingsOpt = Params.get<sol::optional<sol::table>>("mappings");
		if (!MappingsOpt.has_value())
		{
			OutError = TEXT("mappings table is required");
			return false;
		}

		FStrProperty* OutputFieldProperty = FindFProperty<FStrProperty>(MappingProperty->Struct, TEXT("OutputFieldName"));
		FStructProperty* TargetControllerProperty = FindFProperty<FStructProperty>(MappingProperty->Struct, TEXT("TargetController"));
		FNameProperty* TargetNameProperty = TargetControllerProperty && TargetControllerProperty->Struct
			? FindFProperty<FNameProperty>(TargetControllerProperty->Struct, TEXT("Name"))
			: nullptr;
		if (!OutputFieldProperty || !TargetControllerProperty || !TargetNameProperty)
		{
			OutError = TEXT("ControllerMappings fields did not match UE 5.7 headers");
			return false;
		}

		Processor->Modify();
		FScriptArrayHelper Helper(MappingsProperty, MappingsProperty->ContainerPtrToValuePtr<void>(Processor));
		Helper.EmptyValues();
		for (auto& Pair : MappingsOpt.value())
		{
			if (!Pair.second.is<sol::table>())
			{
				continue;
			}
			sol::table MappingTable = Pair.second.as<sol::table>();
			const FString OutputField = ToFString(MappingTable.get_or<std::string>("output_field", ""));
			const FString Controller = ToFString(MappingTable.get_or<std::string>("controller", ""));
			if (OutputField.IsEmpty() || Controller.IsEmpty())
			{
				continue;
			}

			const int32 NewIndex = Helper.AddValue();
			void* MappingPtr = Helper.GetRawPtr(NewIndex);
			OutputFieldProperty->SetPropertyValue(OutputFieldProperty->ContainerPtrToValuePtr<void>(MappingPtr), OutputField);
			void* TargetControllerPtr = TargetControllerProperty->ContainerPtrToValuePtr<void>(MappingPtr);
			TargetNameProperty->SetPropertyValue(TargetNameProperty->ContainerPtrToValuePtr<void>(TargetControllerPtr), FName(*Controller));
		}

		return true;
	}

	bool InvokeActorFunction(AAvaDataLinkActor* Actor, const TCHAR* FunctionName)
	{
		if (!Actor)
		{
			return false;
		}

		if (UFunction* Function = Actor->FindFunction(FunctionName))
		{
			Actor->ProcessEvent(Function, nullptr);
			return true;
		}
		return false;
	}

	void BindAvalancheDataLinkLua(sol::state& Lua, FLuaSessionData& Session)
	{
		Lua.set_function("_enrich_avalanche_datalink_graph", [&Session](sol::table AssetObj, sol::this_state S)
		{
			sol::state_view LuaView(S);

			AssetObj.set_function("info", [](sol::this_state InnerS, sol::table Self) -> sol::object
			{
				return sol::make_object(InnerS, BuildDataLinkGraphInfo(sol::state_view(InnerS), ResolveGraphAsset(Self)));
			});

			AssetObj.set_function("add_constant", [&Session](sol::this_state InnerS, sol::table Self, sol::optional<sol::table> Options) -> sol::object
			{
				sol::state_view InnerLua(InnerS);
				UDataLinkGraph* Graph = ResolveGraphAsset(Self);
				UDataLinkEdGraph* EdGraph = Graph ? Cast<UDataLinkEdGraph>(Graph->GetEdGraph()) : nullptr;
				if (!Graph || !EdGraph)
				{
					Session.Log(TEXT("[FAIL] datalink_graph:add_constant -> graph or EdGraph unavailable"));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				const FString StructName = Options
					? ToFString(Options->get_or<std::string>("struct", "/Script/DataLink.DataLinkString"))
					: TEXT("/Script/DataLink.DataLinkString");
				UScriptStruct* Struct = ResolveScriptStruct(StructName);
				if (!Struct)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] datalink_graph:add_constant -> struct '%s' not found"), *StructName));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "AddDataLinkConstant", "NeoStack Add Data Link Constant Node"));
				Graph->Modify();
				EdGraph->Modify();
				UDataLinkEdNode* EdNode = NewObject<UDataLinkEdNode>(EdGraph, NAME_None, RF_Transactional);
				if (!EdNode)
				{
					return sol::make_object(InnerS, sol::lua_nil);
				}
				EdGraph->AddNode(EdNode, /*bUserAction*/true, /*bSelectNewNode*/false);
				EdNode->CreateNewGuid();
				EdNode->PostPlacedNewNode();
				EdNode->AllocateDefaultPins();
				EdNode->SetTemplateNodeClass(UDataLinkConstant::StaticClass(), /*bInReconstructNode*/false);
				if (UDataLinkConstant* Constant = Cast<UDataLinkConstant>(EdNode->GetTemplateNode()))
				{
					Constant->SetStruct(Struct);
				}
				EdNode->ReconstructNode();
				EdNode->NodePosX = Options ? Options->get_or("x", 0) : 0;
				EdNode->NodePosY = Options ? Options->get_or("y", 0) : 0;
				EdGraph->DirtyGraph();
				Graph->MarkPackageDirty();

				const TArray<UDataLinkEdNode*> Nodes = GetDataLinkEdNodes(Graph);
				const int32 NodeIndex = Nodes.IndexOfByKey(EdNode) + 1;
				return sol::make_object(InnerS, BuildDataLinkEdNodeInfo(InnerLua, EdNode, NodeIndex));
			});

			AssetObj.set_function("add_node_class", [&Session](sol::this_state InnerS, sol::table Self, const std::string& ClassName, sol::optional<sol::table> Options) -> sol::object
			{
				sol::state_view InnerLua(InnerS);
				UDataLinkGraph* Graph = ResolveGraphAsset(Self);
				UDataLinkEdGraph* EdGraph = Graph ? Cast<UDataLinkEdGraph>(Graph->GetEdGraph()) : nullptr;
				if (!Graph || !EdGraph)
				{
					Session.Log(TEXT("[FAIL] datalink_graph:add_node_class -> graph or EdGraph unavailable"));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				UClass* NodeClass = ResolveClass(ToFString(ClassName));
				if (!NodeClass || !NodeClass->IsChildOf(UDataLinkNode::StaticClass()) || NodeClass->HasAnyClassFlags(CLASS_Abstract))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] datalink_graph:add_node_class -> class '%s' is not a concrete UDataLinkNode subclass"), *ToFString(ClassName)));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "AddDataLinkNodeClass", "NeoStack Add Data Link Node Class"));
				Graph->Modify();
				EdGraph->Modify();
				UDataLinkEdNode* EdNode = NewObject<UDataLinkEdNode>(EdGraph, NAME_None, RF_Transactional);
				if (!EdNode)
				{
					return sol::make_object(InnerS, sol::lua_nil);
				}
				EdGraph->AddNode(EdNode, /*bUserAction*/true, /*bSelectNewNode*/false);
				EdNode->CreateNewGuid();
				EdNode->PostPlacedNewNode();
				EdNode->AllocateDefaultPins();
				EdNode->SetTemplateNodeClass(NodeClass, /*bInReconstructNode*/false);
				if (Options)
				{
					if (sol::optional<sol::table> Properties = Options->get<sol::optional<sol::table>>("properties"))
					{
						FString Error;
						if (!ConfigureObjectScalarProperties(EdNode->GetTemplateNode(), *Properties, Error))
						{
							Session.Log(FString::Printf(TEXT("[FAIL] datalink_graph:add_node_class -> %s"), *Error));
							return sol::make_object(InnerS, sol::lua_nil);
						}
					}
				}
				EdNode->ReconstructNode();
				EdNode->NodePosX = Options ? Options->get_or("x", 0) : 0;
				EdNode->NodePosY = Options ? Options->get_or("y", 0) : 0;
				EdGraph->DirtyGraph();
				Graph->MarkPackageDirty();

				const TArray<UDataLinkEdNode*> Nodes = GetDataLinkEdNodes(Graph);
				const int32 NodeIndex = Nodes.IndexOfByKey(EdNode) + 1;
				return sol::make_object(InnerS, BuildDataLinkEdNodeInfo(InnerLua, EdNode, NodeIndex));
			});

			AssetObj.set_function("add_script_node_class", [&Session](sol::this_state InnerS, sol::table Self, const std::string& ClassName, sol::table Options) -> sol::object
			{
				sol::state_view InnerLua(InnerS);
				UDataLinkGraph* Graph = ResolveGraphAsset(Self);
				UDataLinkEdGraph* EdGraph = Graph ? Cast<UDataLinkEdGraph>(Graph->GetEdGraph()) : nullptr;
				if (!Graph || !EdGraph)
				{
					Session.Log(TEXT("[FAIL] datalink_graph:add_script_node_class -> graph or EdGraph unavailable"));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				UClass* ScriptClass = ResolveClass(ToFString(ClassName));
				if (!ScriptClass || !ScriptClass->IsChildOf(UDataLinkScriptNode::StaticClass()) || ScriptClass->HasAnyClassFlags(CLASS_Abstract))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] datalink_graph:add_script_node_class -> class '%s' is not a concrete UDataLinkScriptNode subclass"), *ToFString(ClassName)));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "AddDataLinkScriptNodeClass", "NeoStack Add Data Link Script Node Class"));
				Graph->Modify();
				EdGraph->Modify();
				UDataLinkEdNode* EdNode = NewObject<UDataLinkEdNode>(EdGraph, NAME_None, RF_Transactional);
				if (!EdNode)
				{
					return sol::make_object(InnerS, sol::lua_nil);
				}
				EdGraph->AddNode(EdNode, /*bUserAction*/true, /*bSelectNewNode*/false);
				EdNode->CreateNewGuid();
				EdNode->PostPlacedNewNode();
				EdNode->AllocateDefaultPins();
				EdNode->SetTemplateNodeClass(UDataLinkScriptNodeWrapper::StaticClass(), /*bInReconstructNode*/false);
				UDataLinkScriptNodeWrapper* Wrapper = Cast<UDataLinkScriptNodeWrapper>(EdNode->GetTemplateNode());
				if (!Wrapper)
				{
					Session.Log(TEXT("[FAIL] datalink_graph:add_script_node_class -> wrapper template unavailable"));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				Wrapper->SetNodeClass(ScriptClass);
				UDataLinkScriptNode* ScriptTemplate = GetScriptTemplateNode(Wrapper);
				FString Error;
				if (!ConfigureScriptNodePins(ScriptTemplate, Options, Error))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] datalink_graph:add_script_node_class -> %s"), *Error));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				EdNode->ReconstructNode();
				EdNode->NodePosX = Options.get_or("x", 0);
				EdNode->NodePosY = Options.get_or("y", 0);
				EdGraph->DirtyGraph();
				Graph->MarkPackageDirty();

				const TArray<UDataLinkEdNode*> Nodes = GetDataLinkEdNodes(Graph);
				const int32 NodeIndex = Nodes.IndexOfByKey(EdNode) + 1;
				return sol::make_object(InnerS, BuildDataLinkEdNodeInfo(InnerLua, EdNode, NodeIndex));
			});

			AssetObj.set_function("add_string_builder", [&Session](sol::this_state InnerS, sol::table Self, sol::table Options) -> sol::object
			{
				sol::state_view InnerLua(InnerS);
				UDataLinkGraph* Graph = ResolveGraphAsset(Self);
				UDataLinkEdGraph* EdGraph = Graph ? Cast<UDataLinkEdGraph>(Graph->GetEdGraph()) : nullptr;
				if (!Graph || !EdGraph)
				{
					Session.Log(TEXT("[FAIL] datalink_graph:add_string_builder -> graph or EdGraph unavailable"));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				sol::optional<sol::table> SegmentTable = Options.get<sol::optional<sol::table>>("segments");
				if (!SegmentTable)
				{
					Session.Log(TEXT("[FAIL] datalink_graph:add_string_builder -> segments table is required"));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				TArray<FString> Segments = ReadStringArray(*SegmentTable);
				if (Segments.IsEmpty())
				{
					Session.Log(TEXT("[FAIL] datalink_graph:add_string_builder -> segments table is empty"));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "AddDataLinkStringBuilder", "NeoStack Add Data Link String Builder Node"));
				Graph->Modify();
				EdGraph->Modify();
				UDataLinkEdNode* EdNode = NewObject<UDataLinkEdNode>(EdGraph, NAME_None, RF_Transactional);
				if (!EdNode)
				{
					return sol::make_object(InnerS, sol::lua_nil);
				}
				EdGraph->AddNode(EdNode, /*bUserAction*/true, /*bSelectNewNode*/false);
				EdNode->CreateNewGuid();
				EdNode->PostPlacedNewNode();
				EdNode->AllocateDefaultPins();
				EdNode->SetTemplateNodeClass(UDataLinkNodeStringBuilder::StaticClass(), /*bInReconstructNode*/false);
				if (!ConfigureStringBuilderNode(Cast<UDataLinkNodeStringBuilder>(EdNode->GetTemplateNode()), Segments))
				{
					Session.Log(TEXT("[FAIL] datalink_graph:add_string_builder -> failed to configure segments"));
					return sol::make_object(InnerS, sol::lua_nil);
				}
				EdNode->ReconstructNode();
				EdNode->NodePosX = Options.get_or("x", 0);
				EdNode->NodePosY = Options.get_or("y", 0);
				EdGraph->DirtyGraph();
				Graph->MarkPackageDirty();

				const TArray<UDataLinkEdNode*> Nodes = GetDataLinkEdNodes(Graph);
				const int32 NodeIndex = Nodes.IndexOfByKey(EdNode) + 1;
				return sol::make_object(InnerS, BuildDataLinkEdNodeInfo(InnerLua, EdNode, NodeIndex));
			});

			AssetObj.set_function("add_replace_string", [&Session](sol::this_state InnerS, sol::table Self, sol::optional<sol::table> Options) -> sol::object
			{
				sol::state_view InnerLua(InnerS);
				UDataLinkGraph* Graph = ResolveGraphAsset(Self);
				UDataLinkEdGraph* EdGraph = Graph ? Cast<UDataLinkEdGraph>(Graph->GetEdGraph()) : nullptr;
				if (!Graph || !EdGraph)
				{
					Session.Log(TEXT("[FAIL] datalink_graph:add_replace_string -> graph or EdGraph unavailable"));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "AddDataLinkReplaceString", "NeoStack Add Data Link Replace String Node"));
				Graph->Modify();
				EdGraph->Modify();
				UDataLinkEdNode* EdNode = NewObject<UDataLinkEdNode>(EdGraph, NAME_None, RF_Transactional);
				if (!EdNode)
				{
					return sol::make_object(InnerS, sol::lua_nil);
				}
				EdGraph->AddNode(EdNode, /*bUserAction*/true, /*bSelectNewNode*/false);
				EdNode->CreateNewGuid();
				EdNode->PostPlacedNewNode();
				EdNode->AllocateDefaultPins();
				EdNode->SetTemplateNodeClass(UDataLinkReplaceString::StaticClass(), /*bInReconstructNode*/false);
				EdNode->ReconstructNode();
				EdNode->NodePosX = Options ? Options->get_or("x", 0) : 0;
				EdNode->NodePosY = Options ? Options->get_or("y", 0) : 0;
				EdGraph->DirtyGraph();
				Graph->MarkPackageDirty();

				const TArray<UDataLinkEdNode*> Nodes = GetDataLinkEdNodes(Graph);
				const int32 NodeIndex = Nodes.IndexOfByKey(EdNode) + 1;
				return sol::make_object(InnerS, BuildDataLinkEdNodeInfo(InnerLua, EdNode, NodeIndex));
			});

			AssetObj.set_function("connect_output", [&Session](sol::this_state InnerS, sol::table Self, int32 NodeIndex, sol::optional<std::string> PinNameOpt) -> sol::object
			{
				UDataLinkGraph* Graph = ResolveGraphAsset(Self);
				UDataLinkEdGraph* EdGraph = Graph ? Cast<UDataLinkEdGraph>(Graph->GetEdGraph()) : nullptr;
				UDataLinkEdNode* SourceNode = FindDataLinkEdNodeByLuaIndex(Graph, NodeIndex);
				UDataLinkEdOutputNode* OutputNode = EdGraph ? EdGraph->FindOutputNode() : nullptr;
				UEdGraphPin* OutputResultPin = OutputNode ? OutputNode->GetOutputResultPin() : nullptr;
				if (!Graph || !EdGraph || !SourceNode || !OutputResultPin)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] datalink_graph:connect_output(%d) -> graph, source node, or output node unavailable"), NodeIndex));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				const FName PinName = PinNameOpt ? FName(UTF8_TO_TCHAR(PinNameOpt->c_str())) : FName(UE::DataLink::OutputDefault);
				UEdGraphPin* SourcePin = SourceNode->FindPin(PinName, EGPD_Output);
				if (!SourcePin)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] datalink_graph:connect_output(%d) -> output pin '%s' not found"), NodeIndex, *PinName.ToString()));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				const UEdGraphSchema* Schema = EdGraph->GetSchema();
				if (!Schema)
				{
					return sol::make_object(InnerS, sol::lua_nil);
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "ConnectDataLinkOutput", "NeoStack Connect Data Link Output Node"));
				Graph->Modify();
				EdGraph->Modify();
				SourcePin->Modify();
				OutputResultPin->Modify();
				const bool bConnected = Schema->TryCreateConnection(SourcePin, OutputResultPin)
					|| Schema->TryCreateConnection(OutputResultPin, SourcePin);
				if (!bConnected)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] datalink_graph:connect_output(%d) -> connection rejected"), NodeIndex));
					return sol::make_object(InnerS, sol::lua_nil);
				}
				SourceNode->PinConnectionListChanged(SourcePin);
				OutputNode->PinConnectionListChanged(OutputResultPin);
				EdGraph->DirtyGraph();
				Graph->MarkPackageDirty();
				return sol::make_object(InnerS, BuildDataLinkGraphInfo(sol::state_view(InnerS), Graph));
			});

			AssetObj.set_function("compile", [&Session](sol::this_state InnerS, sol::table Self) -> sol::object
			{
				UDataLinkGraph* Graph = ResolveGraphAsset(Self);
				if (!Graph)
				{
					Session.Log(TEXT("[FAIL] datalink_graph:compile -> graph unavailable"));
					return sol::make_object(InnerS, sol::lua_nil);
				}

				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "CompileDataLinkGraph", "NeoStack Compile Data Link Graph"));
				if (!CompileDataLinkGraphRuntime(Graph))
				{
					Session.Log(TEXT("[FAIL] datalink_graph:compile -> graph has no connected runtime output node"));
					return sol::make_object(InnerS, sol::lua_nil);
				}
				return sol::make_object(InnerS, BuildDataLinkGraphInfo(sol::state_view(InnerS), Graph));
			});
		});

		Lua.set_function("ava_datalink_remote_control",
			[&Session](sol::this_state S) -> sol::object
			{
				sol::state_view LuaView(S);
				URemoteControlPreset* Preset = GetMotionDesignRemoteControlPreset(/*bCreate*/true);
				if (!Preset)
				{
					Session.Log(TEXT("[FAIL] ava_datalink_remote_control -> Motion Design remote-control preset unavailable"));
					return sol::lua_nil;
				}

				TWeakObjectPtr<URemoteControlPreset> PresetPtr = Preset;
				sol::table Handle = LuaView.create_table();
				Handle["info"] = [PresetPtr](sol::table, sol::this_state InnerS) -> sol::table
				{
					return RemoteControlSummary(sol::state_view(InnerS), PresetPtr.Get());
				};

				Handle["ensure_string_controller"] = [PresetPtr, &Session](sol::table, const std::string& Name, sol::optional<std::string> Value, sol::this_state InnerS) -> sol::object
				{
					sol::state_view InnerLua(InnerS);
					URemoteControlPreset* CurrentPreset = PresetPtr.Get();
					const FString ControllerName = ToFString(Name);
					if (!CurrentPreset || ControllerName.IsEmpty())
					{
						Session.Log(TEXT("[FAIL] ava_datalink_remote_control:ensure_string_controller -> preset unavailable or name empty"));
						return sol::lua_nil;
					}

					const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "EnsureRCStringController", "NeoStack Ensure Data Link Remote Control String Controller"));
					CurrentPreset->Modify();
					URCVirtualPropertyBase* Controller = EnsureStringController(
						CurrentPreset,
						ControllerName,
						Value ? ToFString(*Value) : FString(),
						Value.has_value());
					if (!Controller)
					{
						Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_remote_control:ensure_string_controller('%s') -> controller unavailable"), *ControllerName));
						return sol::lua_nil;
					}
					return sol::make_object(InnerLua, ControllerSummary(InnerLua, Controller));
				};

				Handle["controller"] = [PresetPtr](sol::table, const std::string& Name, sol::this_state InnerS) -> sol::object
				{
					sol::state_view InnerLua(InnerS);
					URCVirtualPropertyBase* Controller = FindControllerByName(PresetPtr.Get(), ToFString(Name));
					if (!Controller)
					{
						return sol::make_object(InnerLua, sol::lua_nil);
					}
					return sol::make_object(InnerLua, ControllerSummary(InnerLua, Controller));
				};

				Handle["remove_controller"] = [PresetPtr](sol::table, const std::string& Name, sol::this_state InnerS) -> sol::object
				{
					sol::state_view InnerLua(InnerS);
					URemoteControlPreset* CurrentPreset = PresetPtr.Get();
					if (!CurrentPreset)
					{
						return sol::lua_nil;
					}
					const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "RemoveRCStringController", "NeoStack Remove Data Link Remote Control Controller"));
					CurrentPreset->Modify();
					return sol::make_object(InnerLua, RemoveControllerByName(CurrentPreset, ToFString(Name)));
				};

				return sol::make_object(LuaView, Handle);
			});

		Lua.set_function("ava_datalink_actor",
			[&Session](const std::string& ActorId, sol::this_state S) -> sol::object
			{
				sol::state_view LuaView(S);
				UWorld* World = GetEditorWorld();
				if (!World)
				{
					Session.Log(TEXT("[FAIL] ava_datalink_actor -> no editor world"));
					return sol::lua_nil;
				}

				const FString ActorIdString = ToFString(ActorId);
				AAvaDataLinkActor* Actor = Cast<AAvaDataLinkActor>(NeoLuaActor::FindByNameOrLabel(World, ActorIdString));
				if (!Actor)
				{
					Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor(\"%s\") -> Motion Design Data Link actor not found"), *ActorIdString));
					return sol::lua_nil;
				}

				TWeakObjectPtr<AAvaDataLinkActor> ActorPtr = Actor;
				sol::table Handle = LuaView.create_table();
				Handle["actor"] = TCHAR_TO_UTF8(*Actor->GetActorLabel());

				Handle["info"] = [ActorPtr](sol::table, sol::this_state InnerState) -> sol::table
				{
					sol::state_view InnerLua(InnerState);
					return ActorSummary(InnerLua, ActorPtr.Get());
				};

				Handle["configure"] = [ActorPtr, &Session](sol::table, sol::table Params, sol::this_state InnerState) -> sol::object
				{
					sol::state_view InnerLua(InnerState);
					AAvaDataLinkActor* CurrentActor = ActorPtr.Get();
					if (!CurrentActor)
					{
						Session.Log(TEXT("[FAIL] ava_datalink_actor:configure -> actor unavailable"));
						return sol::lua_nil;
					}

					const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "ConfigureActor", "NeoStack Configure Avalanche Data Link Actor"));
					CurrentActor->Modify();
					if (sol::optional<bool> ExecuteOnBeginPlay = Params.get<sol::optional<bool>>("execute_on_begin_play"))
					{
						CurrentActor->bExecuteOnBeginPlay = ExecuteOnBeginPlay.value();
					}
					return sol::make_object(InnerLua, ActorSummary(InnerLua, CurrentActor));
				};

					Handle["add_instance"] = [ActorPtr, &Session](sol::table, sol::this_state InnerState) -> sol::object
					{
						sol::state_view InnerLua(InnerState);
						AAvaDataLinkActor* CurrentActor = ActorPtr.Get();
					if (!CurrentActor)
					{
						Session.Log(TEXT("[FAIL] ava_datalink_actor:add_instance -> actor unavailable"));
						return sol::lua_nil;
					}

					const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "AddInstance", "NeoStack Add Avalanche Data Link Instance"));
					CurrentActor->Modify();
					UAvaDataLinkInstance* Instance = NewObject<UAvaDataLinkInstance>(CurrentActor, NAME_None, RF_Transactional);
					if (!Instance)
					{
						Session.Log(TEXT("[FAIL] ava_datalink_actor:add_instance -> NewObject failed"));
						return sol::lua_nil;
					}
						CurrentActor->DataLinkInstances.Add(Instance);
						return sol::make_object(InnerLua, InstanceSummary(InnerLua, Instance, CurrentActor->DataLinkInstances.Num()));
					};

					Handle["configure_instance"] = [ActorPtr, &Session](sol::table, int32 InstanceIndex, sol::table Params, sol::this_state InnerState) -> sol::object
					{
						sol::state_view InnerLua(InnerState);
						UAvaDataLinkInstance* Instance = GetInstance(ActorPtr.Get(), InstanceIndex);
						if (!Instance)
						{
							Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:configure_instance(%d) -> instance not found"), InstanceIndex));
							return sol::lua_nil;
						}

						if (sol::optional<std::string> GraphPath = Params.get<sol::optional<std::string>>("graph"))
						{
							UDataLinkGraph* Graph = ResolveDataLinkGraphAsset(ToFString(*GraphPath));
							if (!Graph)
							{
								Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:configure_instance(%d) -> DataLinkGraph '%s' not found"), InstanceIndex, *ToFString(*GraphPath)));
								return sol::lua_nil;
							}

							const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "ConfigureInstance", "NeoStack Configure Avalanche Data Link Instance"));
							if (!SetDataLinkGraph(Instance, Graph))
							{
								Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:configure_instance(%d) -> failed to set DataLinkGraph"), InstanceIndex));
								return sol::lua_nil;
							}
						}
						if (Params.get<sol::optional<sol::table>>("inputs"))
						{
							FString Error;
							const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "ConfigureInstanceInputData", "NeoStack Configure Avalanche Data Link Input Data"));
							if (!ConfigureInputDataRows(Instance, Params, Error))
							{
								Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:configure_instance(%d) -> %s"), InstanceIndex, *Error));
								return sol::lua_nil;
							}
						}
						return sol::make_object(InnerLua, InstanceSummary(InnerLua, Instance, InstanceIndex));
					};

					Handle["remove_instance"] = [ActorPtr, &Session](sol::table, int32 InstanceIndex, sol::this_state InnerState) -> sol::object
					{
						sol::state_view InnerLua(InnerState);
						AAvaDataLinkActor* CurrentActor = ActorPtr.Get();
					UAvaDataLinkInstance* Instance = GetInstance(CurrentActor, InstanceIndex);
					if (!CurrentActor || !Instance)
					{
						Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:remove_instance(%d) -> instance not found"), InstanceIndex));
						return sol::lua_nil;
					}

					const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "RemoveInstance", "NeoStack Remove Avalanche Data Link Instance"));
					CurrentActor->Modify();
					Instance->Stop();
					CurrentActor->DataLinkInstances.RemoveAt(InstanceIndex - 1);
					return sol::make_object(InnerLua, ActorSummary(InnerLua, CurrentActor));
				};

				Handle["add_processor"] = [ActorPtr, &Session](sol::table, int32 InstanceIndex, sol::optional<sol::table> Params, sol::this_state InnerState) -> sol::object
				{
					sol::state_view InnerLua(InnerState);
					UAvaDataLinkInstance* Instance = GetInstance(ActorPtr.Get(), InstanceIndex);
					if (!Instance)
					{
						Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:add_processor(%d) -> instance not found"), InstanceIndex));
						return sol::lua_nil;
					}

					const FString Type = Params.has_value()
						? ToFString(Params->get_or<std::string>("type", "remote_control"))
						: TEXT("remote_control");
					UClass* ProcessorClass = ResolveProcessorClass(Type);
					if (!ProcessorClass || !ProcessorClass->IsChildOf(UDataLinkProcessor::StaticClass()))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:add_processor -> processor class '%s' not found"), *Type));
						return sol::lua_nil;
					}

					const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "AddProcessor", "NeoStack Add Avalanche Data Link Processor"));
					UDataLinkProcessor* Processor = NewObject<UDataLinkProcessor>(Instance, ProcessorClass, NAME_None, RF_Transactional);
					if (!AddProcessor(Instance, Processor))
					{
						Session.Log(TEXT("[FAIL] ava_datalink_actor:add_processor -> failed to add OutputProcessors entry"));
						return sol::lua_nil;
					}

					const TArray<UDataLinkProcessor*> Processors = GetProcessors(Instance);
					return sol::make_object(InnerLua, ProcessorSummary(InnerLua, Processor, Processors.Num()));
				};

				Handle["configure_processor"] = [ActorPtr, &Session](sol::table, int32 InstanceIndex, int32 ProcessorIndex, sol::table Params, sol::this_state InnerState) -> sol::object
				{
					sol::state_view InnerLua(InnerState);
					UAvaDataLinkInstance* Instance = GetInstance(ActorPtr.Get(), InstanceIndex);
					TArray<UDataLinkProcessor*> Processors = GetProcessors(Instance);
					UDataLinkProcessor* Processor = Processors.IsValidIndex(ProcessorIndex - 1) ? Processors[ProcessorIndex - 1] : nullptr;
					if (!Processor)
					{
						Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:configure_processor(%d, %d) -> processor not found"), InstanceIndex, ProcessorIndex));
						return sol::lua_nil;
					}

					FString Error;
					const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "ConfigureProcessor", "NeoStack Configure Avalanche Data Link Processor"));
					if (!ConfigureMappings(Processor, Params, Error))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:configure_processor -> %s"), *Error));
						return sol::lua_nil;
					}
					return sol::make_object(InnerLua, ProcessorSummary(InnerLua, Processor, ProcessorIndex));
				};

				Handle["move_processor"] = [ActorPtr, &Session](sol::table, int32 InstanceIndex, int32 FromIndex, int32 ToIndex, sol::this_state InnerState) -> sol::object
				{
					sol::state_view InnerLua(InnerState);
					UAvaDataLinkInstance* Instance = GetInstance(ActorPtr.Get(), InstanceIndex);
					if (!Instance)
					{
						Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:move_processor(%d) -> instance not found"), InstanceIndex));
						return sol::lua_nil;
					}

					const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "MoveProcessor", "NeoStack Move Avalanche Data Link Processor"));
					if (!MoveProcessor(Instance, FromIndex, ToIndex))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:move_processor(%d, %d, %d) -> move failed"), InstanceIndex, FromIndex, ToIndex));
						return sol::lua_nil;
					}
					return sol::make_object(InnerLua, InstanceSummary(InnerLua, Instance, InstanceIndex));
				};

				Handle["remove_processor"] = [ActorPtr, &Session](sol::table, int32 InstanceIndex, int32 ProcessorIndex, sol::this_state InnerState) -> sol::object
				{
					sol::state_view InnerLua(InnerState);
					UAvaDataLinkInstance* Instance = GetInstance(ActorPtr.Get(), InstanceIndex);
					if (!Instance)
					{
						Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:remove_processor(%d) -> instance not found"), InstanceIndex));
						return sol::lua_nil;
					}

					const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheDataLink", "RemoveProcessor", "NeoStack Remove Avalanche Data Link Processor"));
					if (!RemoveProcessor(Instance, ProcessorIndex))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] ava_datalink_actor:remove_processor(%d, %d) -> remove failed"), InstanceIndex, ProcessorIndex));
						return sol::lua_nil;
					}
					return sol::make_object(InnerLua, InstanceSummary(InnerLua, Instance, InstanceIndex));
				};

				Handle["execute"] = [ActorPtr, &Session](sol::table, sol::this_state InnerState) -> sol::object
				{
					AAvaDataLinkActor* CurrentActor = ActorPtr.Get();
					if (!CurrentActor)
					{
						Session.Log(TEXT("[FAIL] ava_datalink_actor:execute -> actor unavailable"));
						return sol::lua_nil;
					}
					return sol::make_object(sol::state_view(InnerState), InvokeActorFunction(CurrentActor, TEXT("ExecuteDataLinkInstances")));
				};

				Handle["stop"] = [ActorPtr, &Session](sol::table, sol::this_state InnerState) -> sol::object
				{
					AAvaDataLinkActor* CurrentActor = ActorPtr.Get();
					if (!CurrentActor)
					{
						Session.Log(TEXT("[FAIL] ava_datalink_actor:stop -> actor unavailable"));
						return sol::lua_nil;
					}
					return sol::make_object(sol::state_view(InnerState), InvokeActorFunction(CurrentActor, TEXT("StopDataLinkInstances")));
				};

				Session.Log(FString::Printf(TEXT("[OK] ava_datalink_actor(\"%s\")"), *ActorIdString));
				return sol::make_object(LuaView, Handle);
			});
	}
}

class FNSAI_AvalancheDataLinkModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildAvalancheDataLinkDescriptor());
		Registrar.RegisterAssetCapability(
			AvalancheDataLinkExtensionId,
			TEXT("DataLinkGraph"),
			TEXT("_enrich_avalanche_datalink_graph"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->IsA<UDataLinkGraph>();
			});
		Registrar.RegisterLuaBinding(
			AvalancheDataLinkExtensionId,
			TEXT("AvalancheDataLink"),
			{
				{ TEXT("open_asset(path):add_constant(opts?)"), TEXT("Add a Data Link Constant editor node with a real struct-backed output pin"), TEXT("node info or nil") },
				{ TEXT("open_asset(path):add_node_class(class, opts?)"), TEXT("Add a concrete native UDataLinkNode subclass and apply supported scalar template properties"), TEXT("node info or nil") },
				{ TEXT("open_asset(path):add_script_node_class(class, opts)"), TEXT("Add a UDataLinkScriptNodeWrapper and configure a concrete script-node class plus pin descriptors"), TEXT("node info or nil") },
				{ TEXT("open_asset(path):add_string_builder(opts)"), TEXT("Add a Data Link String Builder editor node from segment tokens"), TEXT("node info or nil") },
				{ TEXT("open_asset(path):add_replace_string(opts?)"), TEXT("Add a Data Link Replace String native editor node"), TEXT("node info or nil") },
				{ TEXT("open_asset(path):connect_output(node_index, pin?)"), TEXT("Connect a Data Link node output pin to the graph output node through the EdGraph schema"), TEXT("graph info or nil") },
				{ TEXT("open_asset(path):compile()"), TEXT("Compile Data Link editor graph nodes into runtime UDataLinkGraph node/pin state"), TEXT("graph info or nil") },
				{ TEXT("ava_datalink_remote_control()"), TEXT("Open the Motion Design scene Remote Control preset for string controller setup/inspection"), TEXT("handle or nil") },
				{ TEXT("rc:ensure_string_controller(name, value?) / rc:controller(name) / rc:remove_controller(name)"), TEXT("Create, inspect, or remove a real Remote Control string controller"), TEXT("controller table or bool") },
				{ TEXT("ava_datalink_actor(actor)"), TEXT("Open a Motion Design Data Link actor by label/name"), TEXT("handle or nil") },
				{ TEXT("handle:info()"), TEXT("Inspect actor, Data Link instances, output processors, and RC mappings"), TEXT("table") },
					{ TEXT("handle:configure(opts)"), TEXT("Configure actor opts {execute_on_begin_play?}"), TEXT("info table or nil") },
					{ TEXT("handle:add_instance()"), TEXT("Add an instanced Motion Design Data Link Instance"), TEXT("instance table or nil") },
					{ TEXT("handle:configure_instance(index, opts)"), TEXT("Assign DataLinkGraph and refresh fixed input-data rows through DataLink utilities"), TEXT("instance table or nil") },
					{ TEXT("handle:remove_instance(index)"), TEXT("Stop and remove a Data Link instance"), TEXT("info table or nil") },
				{ TEXT("handle:add_processor(instance_index, opts?)"), TEXT("Add an output processor, defaulting to the Remote Control processor"), TEXT("processor table or nil") },
				{ TEXT("handle:configure_processor(instance_index, processor_index, opts)"), TEXT("Configure Remote Control mappings {mappings={{output_field, controller}}}"), TEXT("processor table or nil") },
				{ TEXT("handle:move_processor(instance_index, from_index, to_index)"), TEXT("Reorder output processors on a Data Link instance"), TEXT("instance table or nil") },
				{ TEXT("handle:remove_processor(instance_index, processor_index)"), TEXT("Remove an output processor from a Data Link instance"), TEXT("instance table or nil") },
				{ TEXT("handle:execute() / handle:stop()"), TEXT("Run or stop all Data Link instances through the actor API"), TEXT("true or nil") },
			},
			FLuaBindingFunc::CreateStatic(&BindAvalancheDataLinkLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(AvalancheDataLinkExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_AvalancheDataLinkModule, NSAI_AvalancheDataLink)
