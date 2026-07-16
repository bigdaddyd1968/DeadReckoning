#include "Modules/ModuleManager.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "Lua/LuaBindingRegistry.h"

#include "AvaTag.h"
#include "AvaTagCollection.h"
#include "AvaTransitionEnums.h"
#include "AvaTransitionTree.h"
#include "ScopedTransaction.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"

#include <sol/sol.hpp>

namespace
{
	const TCHAR* AvalancheTransitionExtensionId = TEXT("neostack.avalanche_transition");

	FNeoStackExtensionDescriptor BuildAvalancheTransitionDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = AvalancheTransitionExtensionId;
		Descriptor.DisplayName = TEXT("Avalanche Transition");
		Descriptor.ModuleName = TEXT("NSAI_AvalancheTransition");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("motion-design");
		Descriptor.Description = TEXT("Author Motion Design transition trees from Lua.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("motion-design"), TEXT("transition"), TEXT("statetree") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}

	EAvaTransitionInstancingMode ParseInstancingMode(const FString& Value, EAvaTransitionInstancingMode DefaultValue)
	{
		if (Value.Equals(TEXT("Reuse"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("EAvaTransitionInstancingMode::Reuse"), ESearchCase::IgnoreCase))
		{
			return EAvaTransitionInstancingMode::Reuse;
		}
		if (Value.Equals(TEXT("New"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("EAvaTransitionInstancingMode::New"), ESearchCase::IgnoreCase))
		{
			return EAvaTransitionInstancingMode::New;
		}
		return DefaultValue;
	}

	const TCHAR* InstancingModeToString(EAvaTransitionInstancingMode Mode)
	{
		switch (Mode)
		{
		case EAvaTransitionInstancingMode::New:
			return TEXT("New");
		case EAvaTransitionInstancingMode::Reuse:
			return TEXT("Reuse");
		default:
			return TEXT("Unknown");
		}
	}

	UAvaTransitionTree* ResolveTransitionTree(const sol::table& AssetObj)
	{
		std::string Path = AssetObj.get_or<std::string>("path", "");
		if (Path.empty())
		{
			return nullptr;
		}
		return LoadObject<UAvaTransitionTree>(nullptr, UTF8_TO_TCHAR(Path.c_str()));
	}

	UAvaTagCollection* LoadTagCollection(const FString& Path)
	{
		if (Path.IsEmpty())
		{
			return nullptr;
		}
		return Cast<UAvaTagCollection>(FSoftObjectPath(Path).TryLoad());
	}

	FAvaTagHandle FindTagHandle(UAvaTagCollection* Collection, const FName TagName)
	{
		if (!Collection || TagName.IsNone())
		{
			return FAvaTagHandle();
		}

		for (const FAvaTagId& TagId : Collection->GetTagIds(false))
		{
			if (TagId.IsValid() && Collection->GetTagName(TagId) == TagName)
			{
				return FAvaTagHandle(Collection, TagId);
			}
		}
		return FAvaTagHandle();
	}

	bool EnsureTagInCollection(UAvaTagCollection* Collection, const FName TagName)
	{
		if (!Collection || TagName.IsNone())
		{
			return false;
		}
		if (FindTagHandle(Collection, TagName).IsValid())
		{
			return true;
		}

		FMapProperty* TagsProperty = FindFProperty<FMapProperty>(UAvaTagCollection::StaticClass(), UAvaTagCollection::GetTagMapName());
		if (!TagsProperty || !TagsProperty->KeyProp || !TagsProperty->ValueProp)
		{
			return false;
		}

		FStructProperty* KeyStructProperty = CastField<FStructProperty>(TagsProperty->KeyProp);
		FStructProperty* ValueStructProperty = CastField<FStructProperty>(TagsProperty->ValueProp);
		if (!KeyStructProperty || KeyStructProperty->Struct != FAvaTagId::StaticStruct() ||
			!ValueStructProperty || ValueStructProperty->Struct != FAvaTag::StaticStruct())
		{
			return false;
		}

		Collection->Modify();
		FScriptMapHelper TagsHelper(TagsProperty, TagsProperty->ContainerPtrToValuePtr<void>(Collection));
		const int32 InternalIndex = TagsHelper.AddDefaultValue_Invalid_NeedsRehash();
		if (!TagsHelper.IsValidIndex(InternalIndex))
		{
			return false;
		}

		FAvaTagId TagId(ForceInit);
		FAvaTag Tag;
		Tag.TagName = TagName;
		TagsProperty->KeyProp->CopyCompleteValue(TagsHelper.GetKeyPtr(InternalIndex), &TagId);
		TagsProperty->ValueProp->CopyCompleteValue(TagsHelper.GetValuePtr(InternalIndex), &Tag);
		TagsHelper.Rehash();
		Collection->PostEditChange();
		Collection->MarkPackageDirty();
		return true;
	}

	void FillTransitionInfo(sol::state_view& Lua, sol::table& Out, const UAvaTransitionTree* Tree)
	{
		if (!Tree)
		{
			return;
		}

		Out["enabled"] = Tree->IsEnabled();
		Out["instancing_mode"] = TCHAR_TO_UTF8(InstancingModeToString(Tree->GetInstancingMode()));
		const FAvaTagHandle TransitionLayer = Tree->GetTransitionLayer();
		Out["transition_layer_valid"] = TransitionLayer.IsValid();
		Out["transition_layer"] = TCHAR_TO_UTF8(*TransitionLayer.ToString());
		Out["transition_layer_name"] = TCHAR_TO_UTF8(*TransitionLayer.ToName().ToString());
		Out["transition_layer_debug"] = TCHAR_TO_UTF8(*TransitionLayer.ToDebugString());
		Out["transition_layer_source"] = TransitionLayer.Source ? TCHAR_TO_UTF8(*TransitionLayer.Source->GetPathName()) : "";
		Out["contains_delay_task"] = Tree->ContainsTask(FindObject<UScriptStruct>(nullptr, TEXT("/Script/AvalancheTransition.AvaTransitionDelayTask")));
		Out["contains_discard_scene_task"] = Tree->ContainsTask(FindObject<UScriptStruct>(nullptr, TEXT("/Script/AvalancheTransition.AvaTransitionDiscardSceneTask")));
		Out["contains_wait_for_layer_task"] = Tree->ContainsTask(FindObject<UScriptStruct>(nullptr, TEXT("/Script/AvalancheTransition.AvaTransitionWaitForLayerTask")));
	}

	void BindAvalancheTransitionLua(sol::state& Lua, FLuaSessionData& Session)
	{
		Lua.set_function("_enrich_avalanche_transition_tree", [&Session](sol::table AssetObj, sol::this_state S)
		{
			sol::state_view LuaView(S);

			sol::protected_function EnrichStateTree = LuaView["_enrich_state_tree"];
			if (EnrichStateTree.valid())
			{
				sol::protected_function_result Result = EnrichStateTree(AssetObj);
				if (!Result.valid())
				{
					sol::error Error = Result;
					Session.Log(FString::Printf(TEXT("[FAIL] _enrich_avalanche_transition_tree -> StateTree enrichment error: %s"),
						UTF8_TO_TCHAR(Error.what())));
				}
			}

			AssetObj.set_function("transition_info", [&Session](sol::table Self, sol::this_state InfoState) -> sol::object
			{
				sol::state_view InfoLua(InfoState);
				UAvaTransitionTree* Tree = ResolveTransitionTree(Self);
				if (!Tree)
				{
					Session.Log(TEXT("[FAIL] transition_info() -> asset is not a UAvaTransitionTree"));
					return sol::lua_nil;
				}

				sol::table Result = InfoLua.create_table();
				FillTransitionInfo(InfoLua, Result, Tree);
				Session.Log(FString::Printf(TEXT("[OK] transition_info() -> enabled=%s, instancing=%s"),
					Tree->IsEnabled() ? TEXT("true") : TEXT("false"),
					InstancingModeToString(Tree->GetInstancingMode())));
				return Result;
			});

			AssetObj.set_function("configure_transition_tree", [&Session](sol::table Self, sol::table Params, sol::this_state ConfigState) -> sol::object
			{
				sol::state_view ConfigLua(ConfigState);
				UAvaTransitionTree* Tree = ResolveTransitionTree(Self);
				if (!Tree)
				{
					Session.Log(TEXT("[FAIL] configure_transition_tree() -> asset is not a UAvaTransitionTree"));
					return sol::lua_nil;
				}

				int32 SetCount = 0;
				const FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheTransition", "ConfigureTransitionTree", "Configure Motion Design Transition Tree"));
				Tree->Modify();

				sol::optional<bool> EnabledOpt = Params.get<sol::optional<bool>>("enabled");
				if (EnabledOpt.has_value())
				{
					Tree->SetEnabled(EnabledOpt.value());
					++SetCount;
				}

				std::string ModeValue = Params.get_or<std::string>("instancing_mode", "");
				if (ModeValue.empty())
				{
					ModeValue = Params.get_or<std::string>("InstancingMode", "");
				}
				if (!ModeValue.empty())
				{
					Tree->SetInstancingMode(ParseInstancingMode(UTF8_TO_TCHAR(ModeValue.c_str()), Tree->GetInstancingMode()));
					++SetCount;
				}

				sol::object LayerObj = Params.get<sol::object>("transition_layer");
				if (LayerObj.valid() && LayerObj != sol::lua_nil)
				{
					if (!LayerObj.is<sol::table>())
					{
						Session.Log(TEXT("[FAIL] configure_transition_tree() -> transition_layer must be {collection=..., tag=..., create_tag=true?}"));
						return sol::make_object(ConfigLua, false);
					}

					sol::table Layer = LayerObj.as<sol::table>();
					FString CollectionPath = UTF8_TO_TCHAR(Layer.get_or<std::string>("collection", "").c_str());
					if (CollectionPath.IsEmpty())
					{
						CollectionPath = UTF8_TO_TCHAR(Layer.get_or<std::string>("collection_path", "").c_str());
					}
					const FString TagNameValue = UTF8_TO_TCHAR(Layer.get_or<std::string>("tag", "").c_str());
					UAvaTagCollection* Collection = LoadTagCollection(CollectionPath);
					if (!Collection)
					{
						Session.Log(FString::Printf(TEXT("[FAIL] configure_transition_tree() -> transition_layer collection '%s' not found"), *CollectionPath));
						return sol::make_object(ConfigLua, false);
					}

					const FName TagName(*TagNameValue);
					if (Layer.get_or("create_tag", false) && !EnsureTagInCollection(Collection, TagName))
					{
						Session.Log(FString::Printf(TEXT("[FAIL] configure_transition_tree() -> failed to create transition layer tag '%s'"), *TagNameValue));
						return sol::make_object(ConfigLua, false);
					}

					const FAvaTagHandle LayerHandle = FindTagHandle(Collection, TagName);
					if (!LayerHandle.IsValid())
					{
						Session.Log(FString::Printf(TEXT("[FAIL] configure_transition_tree() -> transition layer tag '%s' not found in '%s'"), *TagNameValue, *CollectionPath));
						return sol::make_object(ConfigLua, false);
					}

					Tree->SetTransitionLayer(LayerHandle);
					++SetCount;
				}

				if (SetCount == 0)
				{
					Session.Log(TEXT("[FAIL] configure_transition_tree() -> no supported fields requested"));
					return sol::make_object(ConfigLua, false);
				}

				Tree->MarkPackageDirty();
				Session.Log(FString::Printf(TEXT("[OK] configure_transition_tree() -> %d fields set"), SetCount));
				return sol::make_object(ConfigLua, true);
			});
		});
	}
}

class FNSAI_AvalancheTransitionModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildAvalancheTransitionDescriptor());
		Registrar.RegisterAssetCapability(
			AvalancheTransitionExtensionId,
			TEXT("AvalancheTransitionTree"),
			TEXT("_enrich_avalanche_transition_tree"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->IsA<UAvaTransitionTree>();
			});
		Registrar.RegisterLuaBinding(
			AvalancheTransitionExtensionId,
			TEXT("AvalancheTransition"),
			TArray<FLuaFunctionDoc>(),
			FLuaBindingFunc::CreateStatic(&BindAvalancheTransitionLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(AvalancheTransitionExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_AvalancheTransitionModule, NSAI_AvalancheTransition)
