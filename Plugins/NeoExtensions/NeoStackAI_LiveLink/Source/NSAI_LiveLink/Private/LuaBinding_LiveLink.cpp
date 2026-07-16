// Copyright 2025-2026 Betide Studio. All Rights Reserved.

#include "Lua/LuaBindingRegistry.h"
#include "Lua/LuaAssetResolver.h"
#include "Lua/LuaActorResolver.h"
#include "Lua/LuaPropertyTable.h"
#include "Lua/LuaStr.h"
#include "Editor.h"
#include "Features/IModularFeatures.h"
#include "ILiveLinkClient.h"
#include "LiveLinkPresetTypes.h"
#include "LiveLinkTypes.h"
#include "LiveLinkSourceSettings.h"
#include "LiveLinkSubjectSettings.h"
#include "LiveLinkPreset.h"
#include "LiveLinkRole.h"
#include "LiveLinkFramePreProcessor.h"
#include "LiveLinkVirtualSubject.h"
#include "LiveLinkAnimationVirtualSubject.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkTransformRole.h"
#include "Roles/LiveLinkCameraRole.h"
#include "Roles/LiveLinkLightRole.h"
#include "Roles/LiveLinkBasicRole.h"
#include "Visualizers/LiveLinkDataPreviewComponent.h"
#include "Components/ActorComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

// ============================================================================
// HELPERS
// ============================================================================

namespace
{

static ILiveLinkClient* GetLiveLinkClient()
{
	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		return &IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	}
	return nullptr;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
static FString SubjectStateToString(ELiveLinkSubjectState State)
{
	switch (State)
	{
	case ELiveLinkSubjectState::Connected:        return TEXT("Connected");
	case ELiveLinkSubjectState::Unresponsive:     return TEXT("Unresponsive");
	case ELiveLinkSubjectState::Disconnected:     return TEXT("Disconnected");
	case ELiveLinkSubjectState::InvalidOrDisabled: return TEXT("InvalidOrDisabled");
#if ENGINE_MINOR_VERSION >= 6
	case ELiveLinkSubjectState::Paused:           return TEXT("Paused");
#endif
	case ELiveLinkSubjectState::Unknown:          return TEXT("Unknown");
	default:                                       return TEXT("Unknown");
	}
}
#endif // ENGINE_MINOR_VERSION >= 5

static TSubclassOf<ULiveLinkRole> FindRoleClassByName(const FString& RoleName)
{
	if (RoleName.IsEmpty())
	{
		return nullptr;
	}

	if (UClass* ExactClass = FindObject<UClass>(nullptr, *RoleName))
	{
		if (ExactClass->IsChildOf(ULiveLinkRole::StaticClass()) && !ExactClass->HasAnyClassFlags(CLASS_Abstract))
		{
			return ExactClass;
		}
	}

	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(ULiveLinkRole::StaticClass(), DerivedClasses, true);
	for (UClass* RoleClass : DerivedClasses)
	{
		if (RoleClass->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}
		if (RoleClass->GetName().Equals(RoleName, ESearchCase::IgnoreCase) ||
			RoleClass->GetName().Replace(TEXT("ULiveLink"), TEXT("")).Replace(TEXT("Role"), TEXT("")).Equals(RoleName, ESearchCase::IgnoreCase))
		{
			return RoleClass;
		}
	}
	return nullptr;
}

static UWorld* GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

static UActorComponent* FindComponentByName(AActor* Actor, const FString& ComponentName)
{
	if (!Actor || ComponentName.IsEmpty())
	{
		return nullptr;
	}

	TArray<UActorComponent*> Components;
	Actor->GetComponents(Components);
	for (UActorComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}

		if (Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase) ||
			Component->GetFName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			return Component;
		}
	}
	return nullptr;
}

static const TCHAR* LiveLinkVisualBoneTypeToString(ELiveLinkVisualBoneType Type)
{
	switch (Type)
	{
	case ELiveLinkVisualBoneType::Joint:
		return TEXT("Joint");
	case ELiveLinkVisualBoneType::Bone:
	default:
		return TEXT("Bone");
	}
}

static bool ParseLiveLinkVisualBoneType(const FString& Value, ELiveLinkVisualBoneType& OutType)
{
	if (Value.Equals(TEXT("Joint"), ESearchCase::IgnoreCase) ||
		Value.Equals(TEXT("ELiveLinkVisualBoneType::Joint"), ESearchCase::IgnoreCase))
	{
		OutType = ELiveLinkVisualBoneType::Joint;
		return true;
	}
	if (Value.Equals(TEXT("Bone"), ESearchCase::IgnoreCase) ||
		Value.Equals(TEXT("ELiveLinkVisualBoneType::Bone"), ESearchCase::IgnoreCase))
	{
		OutType = ELiveLinkVisualBoneType::Bone;
		return true;
	}
	return false;
}

static sol::object GetOptionValue(const sol::table& Options, const char* PrimaryKey, const char* SecondaryKey = nullptr)
{
	sol::object Value = Options.get<sol::object>(PrimaryKey);
	if ((Value == sol::lua_nil) && SecondaryKey)
	{
		Value = Options.get<sol::object>(SecondaryKey);
	}
	return Value;
}

static sol::table MakeDataPreviewComponentInfo(sol::state_view LuaView, const ULiveLinkDataPreviewComponent* Component)
{
	sol::table Result = LuaView.create_table();
	if (!Component)
	{
		return Result;
	}

	Result["ok"] = true;
	Result["class"] = std::string(TCHAR_TO_UTF8(*Component->GetClass()->GetName()));
	Result["component"] = std::string(TCHAR_TO_UTF8(*Component->GetName()));
	Result["subject_name"] = std::string(TCHAR_TO_UTF8(*Component->SubjectName.Name.ToString()));
	Result["evaluate_live_link"] = Component->bEvaluateLiveLink;
	Result["draw_labels"] = Component->bDrawLabels;
	Result["bone_visual_type"] = std::string(TCHAR_TO_UTF8(LiveLinkVisualBoneTypeToString(Component->BoneVisualType)));
	return Result;
}

static bool MatchesShortClassName(const UClass* Class, const FString& Query, const TCHAR* Prefix, const TCHAR* Suffix)
{
	if (!Class || Query.IsEmpty())
	{
		return false;
	}

	if (Class->GetName().Equals(Query, ESearchCase::IgnoreCase) ||
		Class->GetPathName().Equals(Query, ESearchCase::IgnoreCase))
	{
		return true;
	}

	FString ShortName = Class->GetName();
	ShortName.RemoveFromStart(Prefix, ESearchCase::IgnoreCase);
	ShortName.RemoveFromEnd(Suffix, ESearchCase::IgnoreCase);
	if (ShortName.Equals(Query, ESearchCase::IgnoreCase))
	{
		return true;
	}

	const FString DisplayName = Class->GetDisplayNameText().ToString();
	return DisplayName.Equals(Query, ESearchCase::IgnoreCase);
}

static TSubclassOf<ULiveLinkVirtualSubject> FindVirtualSubjectClassByName(const FString& ClassName)
{
	if (ClassName.IsEmpty())
	{
		return nullptr;
	}

	if (ClassName.Equals(TEXT("Animation"), ESearchCase::IgnoreCase) ||
		ClassName.Equals(TEXT("AnimationVirtualSubject"), ESearchCase::IgnoreCase) ||
		ClassName.Equals(TEXT("LiveLinkAnimationVirtualSubject"), ESearchCase::IgnoreCase) ||
		ClassName.Equals(ULiveLinkAnimationVirtualSubject::StaticClass()->GetPathName(), ESearchCase::IgnoreCase))
	{
		return ULiveLinkAnimationVirtualSubject::StaticClass();
	}

	if (UClass* ExactClass = FindObject<UClass>(nullptr, *ClassName))
	{
		if (ExactClass->IsChildOf(ULiveLinkVirtualSubject::StaticClass()) && !ExactClass->HasAnyClassFlags(CLASS_Abstract))
		{
			return ExactClass;
		}
	}

	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(ULiveLinkVirtualSubject::StaticClass(), DerivedClasses, true);
	for (UClass* VSClass : DerivedClasses)
	{
		if (VSClass->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}
		if (MatchesShortClassName(VSClass, ClassName, TEXT("ULiveLink"), TEXT("VirtualSubject")))
		{
			return VSClass;
		}
	}
	return nullptr;
}

static TSubclassOf<ULiveLinkFrameTranslator> FindFrameTranslatorClassByName(const FString& ClassName)
{
	if (ClassName.IsEmpty())
	{
		return nullptr;
	}

	if (UClass* ExactClass = FindObject<UClass>(nullptr, *ClassName))
	{
		if (ExactClass->IsChildOf(ULiveLinkFrameTranslator::StaticClass()) && !ExactClass->HasAnyClassFlags(CLASS_Abstract))
		{
			return ExactClass;
		}
	}

	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(ULiveLinkFrameTranslator::StaticClass(), DerivedClasses, true);
	for (UClass* TranslatorClass : DerivedClasses)
	{
		if (TranslatorClass->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}
		if (MatchesShortClassName(TranslatorClass, ClassName, TEXT("ULiveLink"), TEXT("")))
		{
			return TranslatorClass;
		}
	}
	return nullptr;
}

static bool ReadStringArray(const sol::object& Value, TArray<FString>& OutValues, FString& OutError)
{
	OutValues.Reset();

	if (!Value.valid() || Value.is<sol::lua_nil_t>())
	{
		return true;
	}

	if (Value.is<std::string>())
	{
		OutValues.Add(NeoLuaStr::ToFString(Value.as<std::string>()));
		return true;
	}

	if (!Value.is<sol::table>())
	{
		OutError = TEXT("expected a string or Lua sequence");
		return false;
	}

	sol::table Table = Value.as<sol::table>();
	for (int32 Index = 1; Table[Index].valid(); ++Index)
	{
		sol::object Item = Table[Index];
		if (Item.is<std::string>())
		{
			OutValues.Add(NeoLuaStr::ToFString(Item.as<std::string>()));
			continue;
		}

		if (Item.is<sol::table>())
		{
			sol::table ItemTable = Item.as<sol::table>();
			sol::object NameObj = ItemTable["name"];
			if (NameObj.is<std::string>())
			{
				OutValues.Add(NeoLuaStr::ToFString(NameObj.as<std::string>()));
				continue;
			}
		}

		OutError = FString::Printf(TEXT("entry %d must be a string or { name = ... }"), Index);
		return false;
	}

	return true;
}

static bool ReadSubjectNames(const sol::object& Value, TArray<FLiveLinkSubjectName>& OutSubjects, FString& OutError)
{
	TArray<FString> Names;
	if (!ReadStringArray(Value, Names, OutError))
	{
		return false;
	}

	OutSubjects.Reset(Names.Num());
	for (const FString& Name : Names)
	{
		FLiveLinkSubjectName SubjectName;
		SubjectName.Name = FName(*Name);
		OutSubjects.Add(SubjectName);
	}
	return true;
}

static bool SetVirtualSubjectSubjects(ULiveLinkVirtualSubject* VirtualSubject, const TArray<FLiveLinkSubjectName>& Subjects, FString& OutError)
{
	if (!VirtualSubject)
	{
		OutError = TEXT("null virtual subject");
		return false;
	}

	FProperty* SubjectsProperty = VirtualSubject->GetClass()->FindPropertyByName(TEXT("Subjects"));
	if (!SubjectsProperty || !CastField<FArrayProperty>(SubjectsProperty))
	{
		OutError = TEXT("ULiveLinkVirtualSubject::Subjects property was not found");
		return false;
	}

	TArray<FLiveLinkSubjectName>* SubjectArray = SubjectsProperty->ContainerPtrToValuePtr<TArray<FLiveLinkSubjectName>>(VirtualSubject);
	if (!SubjectArray)
	{
		OutError = TEXT("could not access virtual subject source array");
		return false;
	}

	*SubjectArray = Subjects;
	return true;
}

static bool SetVirtualSubjectRebroadcast(ULiveLinkVirtualSubject* VirtualSubject, bool bRebroadcast, FString& OutError)
{
	if (!VirtualSubject)
	{
		OutError = TEXT("null virtual subject");
		return false;
	}

	FProperty* RebroadcastProperty = VirtualSubject->GetClass()->FindPropertyByName(TEXT("bRebroadcastSubject"));
	FBoolProperty* BoolProperty = CastField<FBoolProperty>(RebroadcastProperty);
	if (!BoolProperty)
	{
		OutError = TEXT("ULiveLinkVirtualSubject::bRebroadcastSubject property was not found");
		return false;
	}

	BoolProperty->SetPropertyValue_InContainer(VirtualSubject, bRebroadcast);
	return true;
}

static bool GetVirtualSubjectRebroadcast(ULiveLinkVirtualSubject* VirtualSubject)
{
	if (!VirtualSubject)
	{
		return false;
	}

	FProperty* RebroadcastProperty = VirtualSubject->GetClass()->FindPropertyByName(TEXT("bRebroadcastSubject"));
	FBoolProperty* BoolProperty = CastField<FBoolProperty>(RebroadcastProperty);
	return BoolProperty ? BoolProperty->GetPropertyValue_InContainer(VirtualSubject) : false;
}

static bool SetVirtualSubjectSyncSubject(ULiveLinkVirtualSubject* VirtualSubject, const FString& SyncSubjectName, FString& OutError)
{
	if (!VirtualSubject)
	{
		OutError = TEXT("null virtual subject");
		return false;
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	FLiveLinkSubjectName SyncSubject;
	if (!SyncSubjectName.IsEmpty())
	{
		SyncSubject.Name = FName(*SyncSubjectName);
		const TArray<FLiveLinkSubjectName>& SourceSubjects = VirtualSubject->GetSubjects();
		if (!SourceSubjects.Contains(SyncSubject))
		{
			OutError = FString::Printf(TEXT("sync_subject '%s' is not one of the virtual subject source_subjects"), *SyncSubjectName);
			return false;
		}
	}

	VirtualSubject->SyncSubject = SyncSubject;
	return true;
#else
	if (!SyncSubjectName.IsEmpty())
	{
		OutError = TEXT("sync_subject requires UE 5.7 or newer");
		return false;
	}
	return true;
#endif
}

static bool SetVirtualSubjectTranslators(ULiveLinkVirtualSubject* VirtualSubject, const sol::object& Value, FString& OutError, TArray<FString>& OutWarnings)
{
	if (!VirtualSubject)
	{
		OutError = TEXT("null virtual subject");
		return false;
	}
	if (!Value.valid() || Value.is<sol::lua_nil_t>())
	{
		return true;
	}
	if (!Value.is<sol::table>())
	{
		OutError = TEXT("translators must be a Lua sequence");
		return false;
	}

	FProperty* TranslatorsProperty = VirtualSubject->GetClass()->FindPropertyByName(TEXT("FrameTranslators"));
	if (!TranslatorsProperty || !CastField<FArrayProperty>(TranslatorsProperty))
	{
		OutError = TEXT("ULiveLinkVirtualSubject::FrameTranslators property was not found");
		return false;
	}

	using FVirtualTranslatorArray = TArray<TObjectPtr<ULiveLinkFrameTranslator>>;
	FVirtualTranslatorArray* Translators = TranslatorsProperty->ContainerPtrToValuePtr<FVirtualTranslatorArray>(VirtualSubject);
	if (!Translators)
	{
		OutError = TEXT("could not access virtual subject translators array");
		return false;
	}

	sol::table TranslatorSpecs = Value.as<sol::table>();
	FVirtualTranslatorArray NewTranslators;
	const TSubclassOf<ULiveLinkRole> SubjectRole = VirtualSubject->GetRole();

	for (int32 Index = 1; TranslatorSpecs[Index].valid(); ++Index)
	{
		sol::object Spec = TranslatorSpecs[Index];
		FString ClassName;
		sol::table SpecTable;
		if (Spec.is<std::string>())
		{
			ClassName = NeoLuaStr::ToFString(Spec.as<std::string>());
		}
		else if (Spec.is<sol::table>())
		{
			SpecTable = Spec.as<sol::table>();
			sol::object ClassObj = SpecTable["class"];
			if (!ClassObj.valid() || ClassObj.is<sol::lua_nil_t>())
			{
				ClassObj = SpecTable["translator_class"];
			}
			if (!ClassObj.is<std::string>())
			{
				OutError = FString::Printf(TEXT("translator entry %d requires class or translator_class"), Index);
				return false;
			}
			ClassName = NeoLuaStr::ToFString(ClassObj.as<std::string>());
		}
		else
		{
			OutError = FString::Printf(TEXT("translator entry %d must be a class string or table"), Index);
			return false;
		}

		TSubclassOf<ULiveLinkFrameTranslator> TranslatorClass = FindFrameTranslatorClassByName(ClassName);
		if (!TranslatorClass)
		{
			OutError = FString::Printf(TEXT("LiveLink frame translator class not found: %s"), *ClassName);
			return false;
		}

		ULiveLinkFrameTranslator* Translator = NewObject<ULiveLinkFrameTranslator>(VirtualSubject, TranslatorClass);
		if (!Translator)
		{
			OutError = FString::Printf(TEXT("failed to create LiveLink frame translator: %s"), *ClassName);
			return false;
		}

		if (SubjectRole && Translator->GetFromRole() && !SubjectRole->IsChildOf(Translator->GetFromRole()))
		{
			OutError = FString::Printf(TEXT("translator %s expects from_role %s but virtual subject role is %s"),
				*TranslatorClass->GetName(),
				*Translator->GetFromRole()->GetName(),
				*SubjectRole->GetName());
			return false;
		}

		if (SpecTable.valid())
		{
			sol::object PropertiesObj = SpecTable["properties"];
			sol::table PropertiesTable = PropertiesObj.is<sol::table>() ? PropertiesObj.as<sol::table>() : SpecTable;
			FString PropertyError;
			TArray<FString> PropertyWarnings;
			NeoLuaProperty::ApplyTable(Translator, PropertiesTable, PropertyError, &PropertyWarnings);
			if (!PropertyError.IsEmpty())
			{
				OutWarnings.Add(FString::Printf(TEXT("%s: %s"), *TranslatorClass->GetName(), *PropertyError));
			}
			for (const FString& Warning : PropertyWarnings)
			{
				if (!Warning.StartsWith(TEXT("unknown property 'class'")) && !Warning.StartsWith(TEXT("unknown property 'translator_class'")) && !Warning.StartsWith(TEXT("unknown property 'properties'")))
				{
					OutWarnings.Add(FString::Printf(TEXT("%s: %s"), *TranslatorClass->GetName(), *Warning));
				}
			}
		}

		NewTranslators.Add(Translator);
	}

	*Translators = MoveTemp(NewTranslators);
	return true;
}

static bool ApplyNamedVirtualSubjectProperty(ULiveLinkVirtualSubject* VirtualSubject, const TCHAR* PropertyName, const sol::object& Value, FString& OutError)
{
	if (!Value.valid() || Value.is<sol::lua_nil_t>())
	{
		return true;
	}

	FProperty* Property = VirtualSubject->GetClass()->FindPropertyByName(PropertyName);
	if (!Property)
	{
		OutError = FString::Printf(TEXT("property '%s' was not found on %s"), PropertyName, *VirtualSubject->GetClass()->GetName());
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(VirtualSubject);
	return NeoLuaProperty::ApplySolValueToProperty(Property, ValuePtr, VirtualSubject, Value, OutError);
}

static bool ConfigureVirtualSubjectFromTable(ILiveLinkClient* Client, const FLiveLinkSubjectKey& SubjectKey, ULiveLinkVirtualSubject* VirtualSubject, const sol::table& Options, FString& OutError, TArray<FString>& OutWarnings)
{
	if (!Client || !VirtualSubject)
	{
		OutError = TEXT("missing LiveLink client or virtual subject");
		return false;
	}

	VirtualSubject->Modify();
	bool bChanged = false;

	sol::object SourceSubjectsObj = Options["source_subjects"];
	if (!SourceSubjectsObj.valid() || SourceSubjectsObj.is<sol::lua_nil_t>())
	{
		SourceSubjectsObj = Options["subjects"];
	}
	if (SourceSubjectsObj.valid() && !SourceSubjectsObj.is<sol::lua_nil_t>())
	{
		TArray<FLiveLinkSubjectName> SourceSubjects;
		if (!ReadSubjectNames(SourceSubjectsObj, SourceSubjects, OutError))
		{
			OutError = FString::Printf(TEXT("source_subjects: %s"), *OutError);
			return false;
		}
		if (!SetVirtualSubjectSubjects(VirtualSubject, SourceSubjects, OutError))
		{
			return false;
		}
		bChanged = true;
	}

	sol::object SyncSubjectObj = Options["sync_subject"];
	if (SyncSubjectObj.valid() && !SyncSubjectObj.is<sol::lua_nil_t>())
	{
		if (!SyncSubjectObj.is<std::string>())
		{
			OutError = TEXT("sync_subject must be a string");
			return false;
		}
		if (!SetVirtualSubjectSyncSubject(VirtualSubject, NeoLuaStr::ToFString(SyncSubjectObj.as<std::string>()), OutError))
		{
			return false;
		}
		bChanged = true;
	}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	else if (!VirtualSubject->GetSubjects().Contains(VirtualSubject->SyncSubject))
	{
		VirtualSubject->SyncSubject = FLiveLinkSubjectName();
		bChanged = true;
	}
#endif

	sol::object RebroadcastObj = Options["rebroadcast"];
	if (!RebroadcastObj.valid() || RebroadcastObj.is<sol::lua_nil_t>())
	{
		RebroadcastObj = Options["rebroadcast_subject"];
	}
	if (RebroadcastObj.valid() && !RebroadcastObj.is<sol::lua_nil_t>())
	{
		if (!RebroadcastObj.is<bool>())
		{
			OutError = TEXT("rebroadcast must be a boolean");
			return false;
		}
		if (!SetVirtualSubjectRebroadcast(VirtualSubject, RebroadcastObj.as<bool>(), OutError))
		{
			return false;
		}
		bChanged = true;
	}

	sol::object TranslatorsObj = Options["translators"];
	if (!TranslatorsObj.valid() || TranslatorsObj.is<sol::lua_nil_t>())
	{
		TranslatorsObj = Options["frame_translators"];
	}
	if (TranslatorsObj.valid() && !TranslatorsObj.is<sol::lua_nil_t>())
	{
		if (!SetVirtualSubjectTranslators(VirtualSubject, TranslatorsObj, OutError, OutWarnings))
		{
			return false;
		}
		bChanged = true;
	}

	const TPair<const char*, const TCHAR*> SugarProperties[] = {
		{ "append_subject_name_to_bones", TEXT("bAppendSubjectNameToBones") },
		{ "location_behavior", TEXT("LocationBehavior") },
		{ "rotation_behavior", TEXT("RotationBehavior") },
		{ "attachments", TEXT("Attachments") },
	};
	for (const TPair<const char*, const TCHAR*>& SugarProperty : SugarProperties)
	{
		sol::object Value = Options[SugarProperty.Key];
		if (Value.valid() && !Value.is<sol::lua_nil_t>())
		{
			if (!ApplyNamedVirtualSubjectProperty(VirtualSubject, SugarProperty.Value, Value, OutError))
			{
				return false;
			}
			bChanged = true;
		}
	}

	sol::object SettingsObj = Options["settings"];
	if (!SettingsObj.valid() || SettingsObj.is<sol::lua_nil_t>())
	{
		SettingsObj = Options["properties"];
	}
	if (SettingsObj.valid() && !SettingsObj.is<sol::lua_nil_t>())
	{
		if (!SettingsObj.is<sol::table>())
		{
			OutError = TEXT("settings/properties must be a Lua table");
			return false;
		}
		FString SettingsError;
		TArray<FString> SettingsWarnings;
		if (!NeoLuaProperty::ApplyTable(VirtualSubject, SettingsObj.as<sol::table>(), SettingsError, &SettingsWarnings))
		{
			OutError = SettingsError.IsEmpty() ? TEXT("no virtual subject settings were applied") : SettingsError;
			return false;
		}
		OutWarnings.Append(SettingsWarnings);
		bChanged = true;
	}

	if (bChanged)
	{
#if WITH_EDITOR
		if (FProperty* TranslatorsProperty = VirtualSubject->GetClass()->FindPropertyByName(TEXT("FrameTranslators")))
		{
			FPropertyChangedEvent ChangeEvent(TranslatorsProperty, EPropertyChangeType::ValueSet);
			VirtualSubject->PostEditChangeProperty(ChangeEvent);
		}
#endif
		VirtualSubject->Update();
		Client->ForceTick();
		Client->OnLiveLinkSubjectsChanged().Broadcast();
	}

	(void)SubjectKey;
	return true;
}

static sol::table BuildLiveLinkPresetInfo(sol::state_view& LuaView, ULiveLinkPreset* Preset)
{
	sol::table Result = LuaView.create_table();
	if (!Preset)
	{
		Result["ok"] = false;
		Result["source_count"] = 0;
		Result["subject_count"] = 0;
		Result["sources"] = LuaView.create_table();
		Result["subjects"] = LuaView.create_table();
		return Result;
	}

	const TArray<FLiveLinkSourcePreset>& SourcePresets = Preset->GetSourcePresets();
	const TArray<FLiveLinkSubjectPreset>& SubjectPresets = Preset->GetSubjectPresets();
	Result["ok"] = true;
	Result["path"] = std::string(TCHAR_TO_UTF8(*Preset->GetPathName()));
	Result["source_count"] = SourcePresets.Num();
	Result["subject_count"] = SubjectPresets.Num();

	sol::table Sources = LuaView.create_table();
	int32 SourceIdx = 1;
	for (const FLiveLinkSourcePreset& SourcePreset : SourcePresets)
	{
		sol::table Entry = LuaView.create_table();
		Entry["guid"] = std::string(TCHAR_TO_UTF8(*SourcePreset.Guid.ToString(EGuidFormats::DigitsWithHyphens)));
		Entry["source_type"] = std::string(TCHAR_TO_UTF8(*SourcePreset.SourceType.ToString()));
		Entry["has_settings"] = SourcePreset.Settings != nullptr;
		if (SourcePreset.Settings)
		{
			Entry["settings_class"] = std::string(TCHAR_TO_UTF8(*SourcePreset.Settings->GetClass()->GetPathName()));
			if (SourcePreset.Settings->Factory.Get())
			{
				Entry["factory_class"] = std::string(TCHAR_TO_UTF8(*SourcePreset.Settings->Factory->GetClass()->GetPathName()));
			}
		}
		Sources[SourceIdx++] = Entry;
	}
	Result["sources"] = Sources;

	sol::table Subjects = LuaView.create_table();
	int32 SubjectIdx = 1;
	for (const FLiveLinkSubjectPreset& SubjectPreset : SubjectPresets)
	{
		sol::table Entry = LuaView.create_table();
		Entry["name"] = std::string(TCHAR_TO_UTF8(*SubjectPreset.Key.SubjectName.Name.ToString()));
		Entry["source_guid"] = std::string(TCHAR_TO_UTF8(*SubjectPreset.Key.Source.ToString(EGuidFormats::DigitsWithHyphens)));
		Entry["enabled"] = SubjectPreset.bEnabled;
		Entry["has_settings"] = SubjectPreset.Settings != nullptr;
		Entry["is_virtual"] = SubjectPreset.VirtualSubject != nullptr;
		if (SubjectPreset.Role)
		{
			Entry["role"] = std::string(TCHAR_TO_UTF8(*SubjectPreset.Role->GetPathName()));
			Entry["role_name"] = std::string(TCHAR_TO_UTF8(*SubjectPreset.Role->GetName()));
		}
		if (SubjectPreset.Settings)
		{
			Entry["settings_class"] = std::string(TCHAR_TO_UTF8(*SubjectPreset.Settings->GetClass()->GetPathName()));
		}
		if (SubjectPreset.VirtualSubject)
		{
			Entry["virtual_subject_class"] = std::string(TCHAR_TO_UTF8(*SubjectPreset.VirtualSubject->GetClass()->GetPathName()));
		}
		Subjects[SubjectIdx++] = Entry;
	}
	Result["subjects"] = Subjects;
	return Result;
}

} // anonymous namespace

// ============================================================================
// DOCUMENTATION
// ============================================================================

static TArray<FLuaFunctionDoc> LiveLinkDocs = {
	{ TEXT("livelink_get_sources()"), TEXT("List all LiveLink sources with guid, type, status"), TEXT("table") },
	{ TEXT("livelink_get_subjects(include_disabled?, include_virtual?)"), TEXT("List all LiveLink subjects with name, role, state"), TEXT("table") },
	{ TEXT("livelink_remove_source(guid_string)"), TEXT("Remove a LiveLink source by GUID string"), TEXT("bool") },
	{ TEXT("livelink_create_source(preset_path)"), TEXT("Create LiveLink sources from a ULiveLinkPreset asset"), TEXT("table") },
	{ TEXT("livelink_preset_info(preset_path)"), TEXT("Inspect ULiveLinkPreset source and subject presets"), TEXT("table") },
	{ TEXT("livelink_preset_build_from_client(preset_path)"), TEXT("Rebuild a ULiveLinkPreset from the current LiveLink client"), TEXT("table") },
	{ TEXT("livelink_preset_add_to_client(preset_path, recreate?)"), TEXT("Add a ULiveLinkPreset to the current LiveLink client"), TEXT("bool") },
	{ TEXT("livelink_pause_subject(subject_name)"), TEXT("Pause a LiveLink subject by name"), TEXT("bool") },
	{ TEXT("livelink_unpause_subject(subject_name)"), TEXT("Unpause a LiveLink subject by name"), TEXT("bool") },
	{ TEXT("livelink_set_subject_enabled(subject_name, source_guid, enabled)"), TEXT("Enable or disable a LiveLink subject"), TEXT("bool") },
	{ TEXT("livelink_is_source_valid(guid_string)"), TEXT("Check if a LiveLink source is still valid"), TEXT("bool") },
	{ TEXT("livelink_get_subject_state(subject_name)"), TEXT("Get subject state: Connected, Unresponsive, Disconnected, etc."), TEXT("string") },
	{ TEXT("livelink_add_virtual_subject(subject_name, virtual_subject_class_or_options?)"), TEXT("Create a virtual subject; options may set source_subjects, sync_subject, translators, and settings"), TEXT("string or nil") },
	{ TEXT("livelink_remove_virtual_subject(subject_name, source_guid)"), TEXT("Remove a virtual subject by name and source GUID"), TEXT("bool") },
	{ TEXT("livelink_get_virtual_subjects()"), TEXT("List all virtual subjects with source subjects"), TEXT("table") },
	{ TEXT("livelink_configure_virtual_subject(subject_name, source_guid, options)"), TEXT("Update virtual subject source_subjects, sync_subject, translators, rebroadcast, and settings"), TEXT("bool") },
	{ TEXT("livelink_list_virtual_subject_classes()"), TEXT("List available LiveLink virtual subject classes"), TEXT("table") },
	{ TEXT("livelink_list_frame_translators()"), TEXT("List available LiveLink frame translator classes and from/to roles"), TEXT("table") },
	{ TEXT("livelink_get_subject_settings(subject_name, source_guid)"), TEXT("Get subject settings: preprocessors, interpolation, translators"), TEXT("table or nil") },
	{ TEXT("livelink_get_source_settings(source_guid)"), TEXT("Get source settings: mode, buffer_settings, connection_string"), TEXT("table or nil") },
	{ TEXT("livelink_is_subject_time_synced(subject_name)"), TEXT("Check if a subject is time synchronized"), TEXT("bool") },
	{ TEXT("livelink_get_subject_frame_times(subject_name)"), TEXT("Get buffered frame times for a subject"), TEXT("table") },
	{ TEXT("livelink_list_roles()"), TEXT("List all available LiveLink role class names"), TEXT("table") },
	{ TEXT("livelink_get_subjects_for_role(role_name, include_disabled?, include_virtual?)"), TEXT("Get subjects that support a specific role"), TEXT("table") },
	{ TEXT("livelink_get_static_data(subject_name, source_guid)"), TEXT("Get static data for a subject (bone_names, property_names, etc.)"), TEXT("table or nil") },
	{ TEXT("livelink_evaluate_frame(subject_name, role_name?)"), TEXT("Evaluate the current frame for a subject"), TEXT("table or nil") },
	{ TEXT("livelink_clear_subject_frames(subject_name, source_guid?)"), TEXT("Clear buffered frames for a subject (by name or key)"), TEXT("bool") },
	{ TEXT("livelink_clear_all_frames()"), TEXT("Clear all buffered frames for all subjects"), TEXT("bool") },
	{ TEXT("livelink_remove_subject(subject_name, source_guid)"), TEXT("Remove a non-virtual subject from a specific source"), TEXT("bool") },
	{ TEXT("livelink_does_subject_support_role(subject_name, role_name)"), TEXT("Check if a subject supports a specific role (directly or via translator)"), TEXT("bool") },
	{ TEXT("livelink_get_virtual_sources()"), TEXT("List all virtual subject source GUIDs"), TEXT("table") },
	{ TEXT("livelink_configure_data_preview_component(actor_label, component_name, options?)"), TEXT("Configure and inspect a ULiveLinkDataPreviewComponent subject and visualization state"), TEXT("table") },
	{ TEXT("livelink_force_tick()"), TEXT("Force a LiveLink client tick outside normal engine tick"), TEXT("bool") },
};

// ============================================================================
// BINDING
// ============================================================================

static sol::object LiveLink_PauseSubject(FLuaSessionData& Session, const std::string& subject_name, sol::this_state S)
{
	sol::state_view LuaView(S);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	ILiveLinkClient* Client = GetLiveLinkClient();
	if (!Client)
	{
		Session.Log(TEXT("[FAIL] LiveLink client not available"));
		return sol::make_object(LuaView, false);
	}
	FLiveLinkSubjectName SubjectName;
	SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
	Client->PauseSubject_AnyThread(SubjectName);
	Session.Log(FString::Printf(TEXT("[OK] livelink_pause_subject(%s)"), UTF8_TO_TCHAR(subject_name.c_str())));
	return sol::make_object(LuaView, true);
#else
	Session.Log(TEXT("[FAIL] livelink_pause_subject requires UE 5.6+"));
	return sol::make_object(LuaView, false);
#endif
}

static sol::object LiveLink_UnpauseSubject(FLuaSessionData& Session, const std::string& subject_name, sol::this_state S)
{
	sol::state_view LuaView(S);
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	ILiveLinkClient* Client = GetLiveLinkClient();
	if (!Client)
	{
		Session.Log(TEXT("[FAIL] LiveLink client not available"));
		return sol::make_object(LuaView, false);
	}
	FLiveLinkSubjectName SubjectName;
	SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
	Client->UnpauseSubject_AnyThread(SubjectName);
	Session.Log(FString::Printf(TEXT("[OK] livelink_unpause_subject(%s)"), UTF8_TO_TCHAR(subject_name.c_str())));
	return sol::make_object(LuaView, true);
#else
	Session.Log(TEXT("[FAIL] livelink_unpause_subject requires UE 5.6+"));
	return sol::make_object(LuaView, false);
#endif
}

static void LiveLink_PopulateTransmitEvaluatedData(sol::table& Result, ULiveLinkSourceSettings* Settings)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	Result["transmit_evaluated_data"] = Settings->bTransmitEvaluatedData;
#endif
}

// Helper: Get subject state string (avoids #if inside macro)
static std::string LiveLink_GetSubjectStateStr(ILiveLinkClient* Client, FLiveLinkSubjectName SubjectName)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	return std::string(TCHAR_TO_UTF8(*SubjectStateToString(Client->GetSubjectState(SubjectName))));
#else
	(void)Client;
	(void)SubjectName;
	return std::string("Unknown");
#endif
}

// Helper: Get subject state with logging (avoids #if inside macro)
static sol::object LiveLink_GetSubjectStateFull(ILiveLinkClient* Client, const std::string& subject_name, FLuaSessionData& Session, sol::state_view& LuaView)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	FLiveLinkSubjectName SubjectName;
	SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));

	ELiveLinkSubjectState State = Client->GetSubjectState(SubjectName);
	FString StateStr = SubjectStateToString(State);

	Session.Log(FString::Printf(TEXT("[OK] livelink_get_subject_state(%s) -> %s"),
		UTF8_TO_TCHAR(subject_name.c_str()), *StateStr));
	return sol::make_object(LuaView, std::string(TCHAR_TO_UTF8(*StateStr)));
#else
	(void)Client;
	(void)subject_name;
	Session.Log(TEXT("[FAIL] livelink_get_subject_state requires UE 5.5+"));
	return sol::make_object(LuaView, std::string("Unknown"));
#endif
}

// Helper: Populate ParentSubject field (avoids #if inside macro)
static void LiveLink_PopulateParentSubject(sol::table& Result, ULiveLinkSourceSettings* Settings)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	if (!Settings->ParentSubject.IsNone())
	{
		Result["parent_subject"] = std::string(TCHAR_TO_UTF8(*Settings->ParentSubject.Name.ToString()));
	}
#else
	(void)Result;
	(void)Settings;
#endif
}

// Helper: Populate static data for a subject (avoids #if inside macro)
static sol::object LiveLink_PopulateStaticData(ILiveLinkClient* Client, const FLiveLinkSubjectKey& SubjectKey, const std::string& subject_name, FLuaSessionData& Session, sol::state_view& LuaView)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
	const FLiveLinkStaticDataStruct* StaticData = Client->GetSubjectStaticData_AnyThread(SubjectKey);
	if (!StaticData || !StaticData->IsValid())
	{
		Session.Log(FString::Printf(TEXT("[FAIL] livelink_get_static_data -> no static data for %s"), UTF8_TO_TCHAR(subject_name.c_str())));
		return sol::lua_nil;
	}

	sol::table Result = LuaView.create_table();
	if (const UScriptStruct* Struct = StaticData->GetStruct())
	{
		Result["struct_type"] = std::string(TCHAR_TO_UTF8(*Struct->GetName()));
	}

	// Base static data: property names
	const FLiveLinkBaseStaticData* BaseData = StaticData->GetBaseData();
	if (BaseData)
	{
		sol::table PropNames = LuaView.create_table();
		int32 PIdx = 1;
		for (const FName& PropName : BaseData->PropertyNames)
		{
			PropNames[PIdx++] = std::string(TCHAR_TO_UTF8(*PropName.ToString()));
		}
		Result["property_names"] = PropNames;
	}

	// Try to cast to skeleton static data for bone hierarchy
	const FLiveLinkSkeletonStaticData* SkeletonData = StaticData->Cast<FLiveLinkSkeletonStaticData>();
	if (SkeletonData)
	{
		sol::table BoneNames = LuaView.create_table();
		int32 BIdx = 1;
		for (const FName& BoneName : SkeletonData->GetBoneNames())
		{
			BoneNames[BIdx++] = std::string(TCHAR_TO_UTF8(*BoneName.ToString()));
		}
		Result["bone_names"] = BoneNames;

		sol::table BoneParents = LuaView.create_table();
		int32 BPIdx = 1;
		for (int32 ParentIdx : SkeletonData->GetBoneParents())
		{
			BoneParents[BPIdx++] = ParentIdx;
		}
		Result["bone_parents"] = BoneParents;

		Result["bone_count"] = SkeletonData->GetBoneNames().Num();
		Result["root_bone"] = SkeletonData->FindRootBone();
	}

	Session.Log(FString::Printf(TEXT("[OK] livelink_get_static_data(%s)"), UTF8_TO_TCHAR(subject_name.c_str())));
	return Result;
#else
	(void)Client;
	(void)SubjectKey;
	Session.Log(TEXT("[FAIL] livelink_get_static_data requires UE 5.5+"));
	return sol::lua_nil;
#endif
}

static void BindLiveLink(sol::state& Lua, FLuaSessionData& Session)
{
	// ────────────────────────────────────────────────────────────────────
	// livelink_get_sources()
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_get_sources", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		TArray<FGuid> Sources = Client->GetSources();

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (const FGuid& SourceGuid : Sources)
		{
			sol::table Entry = LuaView.create_table();
			Entry["guid"] = std::string(TCHAR_TO_UTF8(*SourceGuid.ToString()));
			Entry["type"] = std::string(TCHAR_TO_UTF8(*Client->GetSourceType(SourceGuid).ToString()));
			Entry["status"] = std::string(TCHAR_TO_UTF8(*Client->GetSourceStatus(SourceGuid).ToString()));
			Entry["machine_name"] = std::string(TCHAR_TO_UTF8(*Client->GetSourceMachineName(SourceGuid).ToString()));
			Entry["is_valid"] = Client->IsSourceStillValid(SourceGuid);
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_get_sources() -> %d sources"), Sources.Num()));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_get_subjects(include_disabled?, include_virtual?)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_get_subjects", [&Session](
		sol::optional<bool> IncludeDisabled,
		sol::optional<bool> IncludeVirtual,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		bool bIncludeDisabled = IncludeDisabled.value_or(false);
		bool bIncludeVirtual = IncludeVirtual.value_or(false);

		TArray<FLiveLinkSubjectKey> Subjects = Client->GetSubjects(bIncludeDisabled, bIncludeVirtual);

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (const FLiveLinkSubjectKey& SubjectKey : Subjects)
		{
			sol::table Entry = LuaView.create_table();
			Entry["name"] = std::string(TCHAR_TO_UTF8(*SubjectKey.SubjectName.Name.ToString()));
			Entry["source_guid"] = std::string(TCHAR_TO_UTF8(*SubjectKey.Source.ToString()));

			FLiveLinkSubjectKey Key = SubjectKey;
			TSubclassOf<ULiveLinkRole> Role = Client->GetSubjectRole_AnyThread(Key);
			Entry["role"] = Role ? std::string(TCHAR_TO_UTF8(*Role->GetName())) : std::string("None");
			Entry["is_valid"] = Client->IsSubjectValid(SubjectKey);
			Entry["is_enabled"] = Client->IsSubjectEnabled(SubjectKey, false);
			Entry["is_virtual"] = Client->IsVirtualSubject(SubjectKey);
			Entry["state"] = LiveLink_GetSubjectStateStr(Client, SubjectKey.SubjectName);
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_get_subjects() -> %d subjects"), Subjects.Num()));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_remove_source(guid_string)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_remove_source", [&Session](const std::string& guid_str, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		FGuid Guid;
		if (!FGuid::Parse(NeoLuaStr::ToFString(guid_str), Guid))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_remove_source -> invalid GUID: %s"), UTF8_TO_TCHAR(guid_str.c_str())));
			return sol::make_object(LuaView, false);
		}

		Client->RemoveSource(Guid);
		Session.Log(FString::Printf(TEXT("[OK] livelink_remove_source(%s) -> removed"), UTF8_TO_TCHAR(guid_str.c_str())));

		return sol::make_object(LuaView, true);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_create_source(preset_path)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_create_source", [&Session](const std::string& preset_path, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		FString FPresetPath = NeoLuaStr::ToFString(preset_path);
		ULiveLinkPreset* Preset = NeoLuaAsset::Resolve<ULiveLinkPreset>(FPresetPath);
		if (!Preset)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_create_source -> preset not found: %s"), *FPresetPath));
			return sol::lua_nil;
		}

		const TArray<FLiveLinkSourcePreset>& SourcePresets = Preset->GetSourcePresets();
		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		int32 Created = 0;

		for (const FLiveLinkSourcePreset& SourcePreset : SourcePresets)
		{
			if (!SourcePreset.Settings || !SourcePreset.Settings->Factory.Get())
			{
				continue;
			}

			bool bSuccess = Client->CreateSource(SourcePreset);
			if (bSuccess)
			{
				sol::table Entry = LuaView.create_table();
				Entry["index"] = Idx;
				Entry["success"] = true;
				Result[Idx++] = Entry;
				Created++;
			}
			else
			{
				sol::table Entry = LuaView.create_table();
				Entry["index"] = Idx;
				Entry["success"] = false;
				Result[Idx++] = Entry;
			}
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_create_source(%s) -> %d/%d sources created"), *FPresetPath, Created, SourcePresets.Num()));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_preset_info(preset_path)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_preset_info", [&Session](const std::string& preset_path, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		FString FPresetPath = NeoLuaStr::ToFString(preset_path);
		ULiveLinkPreset* Preset = NeoLuaAsset::Resolve<ULiveLinkPreset>(FPresetPath);
		if (!Preset)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_preset_info -> preset not found: %s"), *FPresetPath));
			return sol::lua_nil;
		}

		sol::table Info = BuildLiveLinkPresetInfo(LuaView, Preset);
		Session.Log(FString::Printf(TEXT("[OK] livelink_preset_info(%s) -> %d sources, %d subjects"),
			*FPresetPath,
			Preset->GetSourcePresets().Num(),
			Preset->GetSubjectPresets().Num()));
		return sol::make_object(LuaView, Info);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_preset_build_from_client(preset_path)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_preset_build_from_client", [&Session](const std::string& preset_path, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		FString FPresetPath = NeoLuaStr::ToFString(preset_path);
		ULiveLinkPreset* Preset = NeoLuaAsset::Resolve<ULiveLinkPreset>(FPresetPath);
		if (!Preset)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_preset_build_from_client -> preset not found: %s"), *FPresetPath));
			return sol::lua_nil;
		}

		if (!GetLiveLinkClient())
		{
			Session.Log(TEXT("[FAIL] livelink_preset_build_from_client -> LiveLink client not available"));
			return sol::lua_nil;
		}

		Preset->Modify();
		Preset->BuildFromClient();
		Preset->MarkPackageDirty();

		sol::table Info = BuildLiveLinkPresetInfo(LuaView, Preset);
		Session.Log(FString::Printf(TEXT("[OK] livelink_preset_build_from_client(%s) -> %d sources, %d subjects"),
			*FPresetPath,
			Preset->GetSourcePresets().Num(),
			Preset->GetSubjectPresets().Num()));
		return sol::make_object(LuaView, Info);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_preset_add_to_client(preset_path, recreate?)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_preset_add_to_client", [&Session](
		const std::string& preset_path,
		sol::optional<bool> recreate,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		FString FPresetPath = NeoLuaStr::ToFString(preset_path);
		ULiveLinkPreset* Preset = NeoLuaAsset::Resolve<ULiveLinkPreset>(FPresetPath);
		if (!Preset)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_preset_add_to_client -> preset not found: %s"), *FPresetPath));
			return sol::make_object(LuaView, false);
		}

		for (const FLiveLinkSourcePreset& SourcePreset : Preset->GetSourcePresets())
		{
			if (!SourcePreset.Settings || !SourcePreset.Settings->Factory.Get())
			{
				Session.Log(FString::Printf(TEXT("[FAIL] livelink_preset_add_to_client -> source preset %s has no source factory; refusing unsafe AddToClient"),
					*SourcePreset.Guid.ToString(EGuidFormats::DigitsWithHyphens)));
				return sol::make_object(LuaView, false);
			}
		}

		const bool bRecreate = recreate.value_or(true);
		const bool bAdded = Preset->AddToClient(bRecreate);
		if (ILiveLinkClient* Client = GetLiveLinkClient())
		{
			Client->ForceTick();
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_preset_add_to_client(%s, %s) -> %s"),
			*FPresetPath,
			bRecreate ? TEXT("true") : TEXT("false"),
			bAdded ? TEXT("true") : TEXT("false")));
		return sol::make_object(LuaView, bAdded);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_pause_subject(subject_name)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_pause_subject", [&Session](const std::string& subject_name, sol::this_state S) -> sol::object
	{
		return LiveLink_PauseSubject(Session, subject_name, S);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_unpause_subject(subject_name)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_unpause_subject", [&Session](const std::string& subject_name, sol::this_state S) -> sol::object
	{
		return LiveLink_UnpauseSubject(Session, subject_name, S);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_set_subject_enabled(subject_name, source_guid, enabled)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_set_subject_enabled", [&Session](
		const std::string& subject_name,
		const std::string& source_guid_str,
		bool bEnabled,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		FGuid SourceGuid;
		if (!FGuid::Parse(NeoLuaStr::ToFString(source_guid_str), SourceGuid))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_set_subject_enabled -> invalid source GUID: %s"), UTF8_TO_TCHAR(source_guid_str.c_str())));
			return sol::make_object(LuaView, false);
		}

		FLiveLinkSubjectKey SubjectKey;
		SubjectKey.SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
		SubjectKey.Source = SourceGuid;

		Client->SetSubjectEnabled(SubjectKey, bEnabled);

		Session.Log(FString::Printf(TEXT("[OK] livelink_set_subject_enabled(%s, %s, %s)"),
			UTF8_TO_TCHAR(subject_name.c_str()),
			UTF8_TO_TCHAR(source_guid_str.c_str()),
			bEnabled ? TEXT("true") : TEXT("false")));
		return sol::make_object(LuaView, true);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_is_source_valid(guid_string)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_is_source_valid", [&Session](const std::string& guid_str, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		FGuid Guid;
		if (!FGuid::Parse(NeoLuaStr::ToFString(guid_str), Guid))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_is_source_valid -> invalid GUID: %s"), UTF8_TO_TCHAR(guid_str.c_str())));
			return sol::make_object(LuaView, false);
		}

		bool bValid = Client->IsSourceStillValid(Guid);

		Session.Log(FString::Printf(TEXT("[OK] livelink_is_source_valid(%s) -> %s"),
			UTF8_TO_TCHAR(guid_str.c_str()),
			bValid ? TEXT("true") : TEXT("false")));
		return sol::make_object(LuaView, bValid);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_get_subject_state(subject_name)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_get_subject_state", [&Session](const std::string& subject_name, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		return LiveLink_GetSubjectStateFull(Client, subject_name, Session, LuaView);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_add_virtual_subject(subject_name, virtual_subject_class_or_options?)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_add_virtual_subject", [&Session](
		const std::string& subject_name,
		sol::optional<sol::object> options,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		FString VirtualSubjectClassName;
		sol::table OptionsTable;
		if (options.has_value() && options.value().valid() && !options.value().is<sol::lua_nil_t>())
		{
			sol::object OptionsObj = options.value();
			if (OptionsObj.is<std::string>())
			{
				VirtualSubjectClassName = NeoLuaStr::ToFString(OptionsObj.as<std::string>());
			}
			else if (OptionsObj.is<sol::table>())
			{
				OptionsTable = OptionsObj.as<sol::table>();
				sol::object ClassObj = OptionsTable["virtual_subject_class"];
				if (!ClassObj.valid() || ClassObj.is<sol::lua_nil_t>())
				{
					ClassObj = OptionsTable["class"];
				}
				if (ClassObj.valid() && !ClassObj.is<sol::lua_nil_t>())
				{
					if (!ClassObj.is<std::string>())
					{
						Session.Log(TEXT("[FAIL] livelink_add_virtual_subject -> class must be a string"));
						return sol::lua_nil;
					}
					VirtualSubjectClassName = NeoLuaStr::ToFString(ClassObj.as<std::string>());
				}
			}
			else
			{
				Session.Log(TEXT("[FAIL] livelink_add_virtual_subject -> second argument must be class string or options table"));
				return sol::lua_nil;
			}
		}

		// Add a virtual subject source
		FName SourceName = FName(FString::Printf(TEXT("VirtualSubjectSource_%s"), UTF8_TO_TCHAR(subject_name.c_str())));
		FGuid SourceGuid = Client->AddVirtualSubjectSource(SourceName);
		if (!SourceGuid.IsValid())
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_add_virtual_subject -> failed to add virtual subject source for %s"), UTF8_TO_TCHAR(subject_name.c_str())));
			return sol::lua_nil;
		}

		// Resolve virtual subject class
		TSubclassOf<ULiveLinkVirtualSubject> VSClass;
		if (!VirtualSubjectClassName.IsEmpty())
		{
			VSClass = FindVirtualSubjectClassByName(VirtualSubjectClassName);
			if (!VSClass)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] livelink_add_virtual_subject -> virtual subject class not found: %s"), *VirtualSubjectClassName));
				Client->RemoveSource(SourceGuid);
				return sol::lua_nil;
			}
		}
		else
		{
			VSClass = ULiveLinkAnimationVirtualSubject::StaticClass();
		}

		FLiveLinkSubjectKey VirtualSubjectKey;
		VirtualSubjectKey.SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
		VirtualSubjectKey.Source = SourceGuid;

		bool bSuccess = Client->AddVirtualSubject(VirtualSubjectKey, VSClass);
		if (!bSuccess)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_add_virtual_subject -> AddVirtualSubject failed for %s"), UTF8_TO_TCHAR(subject_name.c_str())));
			Client->RemoveSource(SourceGuid);
			return sol::lua_nil;
		}

		if (OptionsTable.valid())
		{
			UObject* SettingsObj = Client->GetSubjectSettings(VirtualSubjectKey);
			ULiveLinkVirtualSubject* VirtualSubject = Cast<ULiveLinkVirtualSubject>(SettingsObj);
			FString Error;
			TArray<FString> Warnings;
			if (!ConfigureVirtualSubjectFromTable(Client, VirtualSubjectKey, VirtualSubject, OptionsTable, Error, Warnings))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] livelink_add_virtual_subject -> configure failed: %s"), *Error));
				Client->RemoveVirtualSubject(VirtualSubjectKey);
				Client->RemoveSource(SourceGuid);
				return sol::lua_nil;
			}
			for (const FString& Warning : Warnings)
			{
				Session.Log(FString::Printf(TEXT("[WARN] livelink_add_virtual_subject -> %s"), *Warning));
			}
		}

		FString GuidStr = SourceGuid.ToString();
		Session.Log(FString::Printf(TEXT("[OK] livelink_add_virtual_subject(%s) -> source_guid=%s"), UTF8_TO_TCHAR(subject_name.c_str()), *GuidStr));
		return sol::make_object(LuaView, std::string(TCHAR_TO_UTF8(*GuidStr)));
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_remove_virtual_subject(subject_name, source_guid)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_remove_virtual_subject", [&Session](
		const std::string& subject_name,
		const std::string& source_guid_str,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		FGuid SourceGuid;
		if (!FGuid::Parse(NeoLuaStr::ToFString(source_guid_str), SourceGuid))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_remove_virtual_subject -> invalid source GUID: %s"), UTF8_TO_TCHAR(source_guid_str.c_str())));
			return sol::make_object(LuaView, false);
		}

		FLiveLinkSubjectKey VirtualSubjectKey;
		VirtualSubjectKey.SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
		VirtualSubjectKey.Source = SourceGuid;

		if (!Client->IsVirtualSubject(VirtualSubjectKey))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_remove_virtual_subject -> %s is not a virtual subject"), UTF8_TO_TCHAR(subject_name.c_str())));
			return sol::make_object(LuaView, false);
		}

		Client->RemoveVirtualSubject(VirtualSubjectKey);

		Session.Log(FString::Printf(TEXT("[OK] livelink_remove_virtual_subject(%s, %s)"), UTF8_TO_TCHAR(subject_name.c_str()), UTF8_TO_TCHAR(source_guid_str.c_str())));
		return sol::make_object(LuaView, true);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_get_virtual_subjects()
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_get_virtual_subjects", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		// Get all subjects including virtual ones
		TArray<FLiveLinkSubjectKey> AllSubjects = Client->GetSubjects(true, true);

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (const FLiveLinkSubjectKey& SubjectKey : AllSubjects)
		{
			if (!Client->IsVirtualSubject(SubjectKey))
			{
				continue;
			}

			sol::table Entry = LuaView.create_table();
			Entry["name"] = std::string(TCHAR_TO_UTF8(*SubjectKey.SubjectName.Name.ToString()));
			Entry["source_guid"] = std::string(TCHAR_TO_UTF8(*SubjectKey.Source.ToString()));

			TSubclassOf<ULiveLinkRole> Role = Client->GetSubjectRole_AnyThread(SubjectKey);
			Entry["role"] = Role ? std::string(TCHAR_TO_UTF8(*Role->GetName())) : std::string("None");

			// Try to get the virtual subject's source subjects
			UObject* Settings = Client->GetSubjectSettings(SubjectKey);
			ULiveLinkVirtualSubject* VirtualSubject = Cast<ULiveLinkVirtualSubject>(Settings);
			if (VirtualSubject)
			{
				Entry["class"] = std::string(TCHAR_TO_UTF8(*VirtualSubject->GetClass()->GetName()));
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
				Entry["sync_subject"] = std::string(TCHAR_TO_UTF8(*VirtualSubject->SyncSubject.Name.ToString()));
#else
				Entry["sync_subject"] = std::string();
#endif
				Entry["rebroadcast"] = GetVirtualSubjectRebroadcast(VirtualSubject);

				const TArray<FLiveLinkSubjectName>& SourceSubjects = VirtualSubject->GetSubjects();
				sol::table SubjectsTable = LuaView.create_table();
				int32 SubIdx = 1;
				for (const FLiveLinkSubjectName& SrcSubject : SourceSubjects)
				{
					SubjectsTable[SubIdx++] = std::string(TCHAR_TO_UTF8(*SrcSubject.Name.ToString()));
				}
				Entry["subjects"] = SubjectsTable;
				Entry["source_subjects"] = SubjectsTable;
				Entry["source_subject_count"] = SourceSubjects.Num();

				const TArray<ULiveLinkFrameTranslator*>& VTrans = VirtualSubject->GetTranslators();
				sol::table TranslatorsTable = LuaView.create_table();
				int32 TranslatorIdx = 1;
				for (const ULiveLinkFrameTranslator* Translator : VTrans)
				{
					if (Translator)
					{
						TranslatorsTable[TranslatorIdx++] = std::string(TCHAR_TO_UTF8(*Translator->GetClass()->GetName()));
					}
				}
				Entry["translators"] = TranslatorsTable;
				Entry["translator_count"] = VTrans.Num();
			}

				Result[Idx++] = Entry;
			}

			Session.Log(FString::Printf(TEXT("[OK] livelink_get_virtual_subjects() -> %d virtual subjects"), Idx - 1));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_configure_virtual_subject(subject_name, source_guid, options)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_configure_virtual_subject", [&Session](
		const std::string& subject_name,
		const std::string& source_guid_str,
		sol::table options,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		FGuid SourceGuid;
		if (!FGuid::Parse(NeoLuaStr::ToFString(source_guid_str), SourceGuid))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_configure_virtual_subject -> invalid source GUID: %s"), UTF8_TO_TCHAR(source_guid_str.c_str())));
			return sol::make_object(LuaView, false);
		}

		FLiveLinkSubjectKey SubjectKey;
		SubjectKey.SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
		SubjectKey.Source = SourceGuid;

		if (!Client->IsVirtualSubject(SubjectKey))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_configure_virtual_subject -> %s is not a virtual subject"), UTF8_TO_TCHAR(subject_name.c_str())));
			return sol::make_object(LuaView, false);
		}

		ULiveLinkVirtualSubject* VirtualSubject = Cast<ULiveLinkVirtualSubject>(Client->GetSubjectSettings(SubjectKey));
		if (!VirtualSubject)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_configure_virtual_subject -> virtual subject settings unavailable for %s"), UTF8_TO_TCHAR(subject_name.c_str())));
			return sol::make_object(LuaView, false);
		}

		FString Error;
		TArray<FString> Warnings;
		if (!ConfigureVirtualSubjectFromTable(Client, SubjectKey, VirtualSubject, options, Error, Warnings))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_configure_virtual_subject -> %s"), *Error));
			return sol::make_object(LuaView, false);
		}
		for (const FString& Warning : Warnings)
		{
			Session.Log(FString::Printf(TEXT("[WARN] livelink_configure_virtual_subject -> %s"), *Warning));
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_configure_virtual_subject(%s, %s)"), UTF8_TO_TCHAR(subject_name.c_str()), UTF8_TO_TCHAR(source_guid_str.c_str())));
		return sol::make_object(LuaView, true);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_list_virtual_subject_classes()
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_list_virtual_subject_classes", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		TArray<UClass*> DerivedClasses;
		GetDerivedClasses(ULiveLinkVirtualSubject::StaticClass(), DerivedClasses, true);

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (UClass* VSClass : DerivedClasses)
		{
			if (!VSClass || VSClass->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}

			sol::table Entry = LuaView.create_table();
			Entry["name"] = std::string(TCHAR_TO_UTF8(*VSClass->GetName()));
			Entry["path"] = std::string(TCHAR_TO_UTF8(*VSClass->GetPathName()));
			Entry["display_name"] = std::string(TCHAR_TO_UTF8(*VSClass->GetDisplayNameText().ToString()));
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_list_virtual_subject_classes() -> %d classes"), Idx - 1));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_list_frame_translators()
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_list_frame_translators", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		TArray<UClass*> DerivedClasses;
		GetDerivedClasses(ULiveLinkFrameTranslator::StaticClass(), DerivedClasses, true);

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (UClass* TranslatorClass : DerivedClasses)
		{
			if (!TranslatorClass || TranslatorClass->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}

			ULiveLinkFrameTranslator* CDO = TranslatorClass->GetDefaultObject<ULiveLinkFrameTranslator>();
			sol::table Entry = LuaView.create_table();
			Entry["name"] = std::string(TCHAR_TO_UTF8(*TranslatorClass->GetName()));
			Entry["path"] = std::string(TCHAR_TO_UTF8(*TranslatorClass->GetPathName()));
			Entry["display_name"] = std::string(TCHAR_TO_UTF8(*TranslatorClass->GetDisplayNameText().ToString()));
			if (CDO)
			{
				if (TSubclassOf<ULiveLinkRole> FromRole = CDO->GetFromRole())
				{
					Entry["from_role"] = std::string(TCHAR_TO_UTF8(*FromRole->GetName()));
				}
				if (TSubclassOf<ULiveLinkRole> ToRole = CDO->GetToRole())
				{
					Entry["to_role"] = std::string(TCHAR_TO_UTF8(*ToRole->GetName()));
				}
			}
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_list_frame_translators() -> %d translators"), Idx - 1));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_get_subject_settings(subject_name, source_guid)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_get_subject_settings", [&Session](
		const std::string& subject_name,
		const std::string& source_guid_str,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		FGuid SourceGuid;
		if (!FGuid::Parse(NeoLuaStr::ToFString(source_guid_str), SourceGuid))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_get_subject_settings -> invalid source GUID: %s"), UTF8_TO_TCHAR(source_guid_str.c_str())));
			return sol::lua_nil;
		}

		FLiveLinkSubjectKey SubjectKey;
		SubjectKey.SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
		SubjectKey.Source = SourceGuid;

		UObject* SettingsObj = Client->GetSubjectSettings(SubjectKey);
		if (!SettingsObj)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_get_subject_settings -> no settings for %s"), UTF8_TO_TCHAR(subject_name.c_str())));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();
		Result["class"] = std::string(TCHAR_TO_UTF8(*SettingsObj->GetClass()->GetName()));
		Result["is_virtual"] = Client->IsVirtualSubject(SubjectKey);
		Result["is_time_synced"] = Client->IsSubjectTimeSynchronized(SubjectKey);

		// If it's a regular subject settings
		ULiveLinkSubjectSettings* SubjectSettings = Cast<ULiveLinkSubjectSettings>(SettingsObj);
		if (SubjectSettings)
		{
			// Preprocessors
			sol::table PreProcessors = LuaView.create_table();
			int32 PPIdx = 1;
			for (const auto& PP : SubjectSettings->PreProcessors)
			{
				if (PP)
				{
					PreProcessors[PPIdx++] = std::string(TCHAR_TO_UTF8(*PP->GetClass()->GetName()));
				}
			}
			Result["preprocessors"] = PreProcessors;

			// Interpolation
			if (SubjectSettings->InterpolationProcessor)
			{
				Result["interpolation"] = std::string(TCHAR_TO_UTF8(*SubjectSettings->InterpolationProcessor->GetClass()->GetName()));
			}

			// Translators
			sol::table Translators = LuaView.create_table();
			int32 TIdx = 1;
			for (const auto& Translator : SubjectSettings->Translators)
			{
				if (Translator)
				{
					Translators[TIdx++] = std::string(TCHAR_TO_UTF8(*Translator->GetClass()->GetName()));
				}
			}
			Result["translators"] = Translators;

			// Frame rate
			Result["frame_rate_numerator"] = SubjectSettings->FrameRate.Numerator;
			Result["frame_rate_denominator"] = SubjectSettings->FrameRate.Denominator;
			Result["rebroadcast"] = SubjectSettings->bRebroadcastSubject;

			if (SubjectSettings->Role)
			{
				Result["role"] = std::string(TCHAR_TO_UTF8(*SubjectSettings->Role->GetName()));
			}
		}

		// If it's a virtual subject
		ULiveLinkVirtualSubject* VirtualSubject = Cast<ULiveLinkVirtualSubject>(SettingsObj);
		if (VirtualSubject)
		{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			Result["sync_subject"] = std::string(TCHAR_TO_UTF8(*VirtualSubject->SyncSubject.Name.ToString()));
#else
			Result["sync_subject"] = std::string();
#endif
			Result["rebroadcast"] = GetVirtualSubjectRebroadcast(VirtualSubject);

			const TArray<FLiveLinkSubjectName>& SourceSubjects = VirtualSubject->GetSubjects();
			sol::table SubjectsTable = LuaView.create_table();
			int32 SIdx = 1;
			for (const FLiveLinkSubjectName& SrcSubject : SourceSubjects)
			{
				SubjectsTable[SIdx++] = std::string(TCHAR_TO_UTF8(*SrcSubject.Name.ToString()));
			}
			Result["source_subjects"] = SubjectsTable;
			Result["source_subject_count"] = SourceSubjects.Num();

			const TArray<ULiveLinkFrameTranslator*>& VTrans = VirtualSubject->GetTranslators();
			sol::table VTransTable = LuaView.create_table();
			int32 VTIdx = 1;
			for (const auto& VT : VTrans)
			{
				if (VT)
				{
					VTransTable[VTIdx++] = std::string(TCHAR_TO_UTF8(*VT->GetClass()->GetName()));
				}
			}
			Result["translators"] = VTransTable;
			Result["translator_count"] = VTrans.Num();
			Result["settings"] = NeoLuaProperty::ReadAsTable(VirtualSubject, LuaView);
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_get_subject_settings(%s)"), UTF8_TO_TCHAR(subject_name.c_str())));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_get_source_settings(source_guid)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_get_source_settings", [&Session](const std::string& guid_str, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		FGuid SourceGuid;
		if (!FGuid::Parse(NeoLuaStr::ToFString(guid_str), SourceGuid))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_get_source_settings -> invalid GUID: %s"), UTF8_TO_TCHAR(guid_str.c_str())));
			return sol::lua_nil;
		}

		ULiveLinkSourceSettings* Settings = Client->GetSourceSettings(SourceGuid);
		if (!Settings)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_get_source_settings -> no settings for source %s"), UTF8_TO_TCHAR(guid_str.c_str())));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();
		Result["class"] = std::string(TCHAR_TO_UTF8(*Settings->GetClass()->GetName()));

		// Mode
		switch (Settings->Mode)
		{
		case ELiveLinkSourceMode::Latest:     Result["mode"] = std::string("Latest"); break;
		case ELiveLinkSourceMode::EngineTime: Result["mode"] = std::string("EngineTime"); break;
		case ELiveLinkSourceMode::Timecode:   Result["mode"] = std::string("Timecode"); break;
		default:                              Result["mode"] = std::string("Unknown"); break;
		}

		// Buffer settings
		sol::table Buffer = LuaView.create_table();
		Buffer["max_frames"] = Settings->BufferSettings.MaxNumberOfFrameToBuffered;
		Buffer["valid_engine_time_enabled"] = Settings->BufferSettings.bValidEngineTimeEnabled;
		Buffer["valid_engine_time"] = Settings->BufferSettings.ValidEngineTime;
		Buffer["engine_time_offset"] = Settings->BufferSettings.EngineTimeOffset;
		Buffer["engine_time_clock_offset"] = Settings->BufferSettings.EngineTimeClockOffset;
		Buffer["smooth_engine_time_offset"] = Settings->BufferSettings.SmoothEngineTimeOffset;
		Buffer["generate_sub_frame"] = Settings->BufferSettings.bGenerateSubFrame;
		Buffer["source_timecode_frame_rate_num"] = Settings->BufferSettings.SourceTimecodeFrameRate.Numerator;
		Buffer["source_timecode_frame_rate_den"] = Settings->BufferSettings.SourceTimecodeFrameRate.Denominator;
		Buffer["detected_frame_rate_num"] = Settings->BufferSettings.DetectedFrameRate.Numerator;
		Buffer["detected_frame_rate_den"] = Settings->BufferSettings.DetectedFrameRate.Denominator;
		Buffer["use_timecode_smooth_latest"] = Settings->BufferSettings.bUseTimecodeSmoothLatest;
		Buffer["valid_timecode_frame_enabled"] = Settings->BufferSettings.bValidTimecodeFrameEnabled;
		Buffer["valid_timecode_frame"] = Settings->BufferSettings.ValidTimecodeFrame;
		Buffer["timecode_frame_offset"] = Settings->BufferSettings.TimecodeFrameOffset;
		Buffer["timecode_clock_offset"] = Settings->BufferSettings.TimecodeClockOffset;
		Buffer["latest_offset"] = Settings->BufferSettings.LatestOffset;
		Buffer["keep_at_least_one_frame"] = Settings->BufferSettings.bKeepAtLeastOneFrame;
		Result["buffer_settings"] = Buffer;

		Result["connection_string"] = std::string(TCHAR_TO_UTF8(*Settings->ConnectionString));
		LiveLink_PopulateTransmitEvaluatedData(Result, Settings);

		LiveLink_PopulateParentSubject(Result, Settings);

		Session.Log(FString::Printf(TEXT("[OK] livelink_get_source_settings(%s)"), UTF8_TO_TCHAR(guid_str.c_str())));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_is_subject_time_synced(subject_name)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_is_subject_time_synced", [&Session](const std::string& subject_name, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		FLiveLinkSubjectName SubjectName;
		SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));

		bool bSynced = Client->IsSubjectTimeSynchronized(SubjectName);

		Session.Log(FString::Printf(TEXT("[OK] livelink_is_subject_time_synced(%s) -> %s"),
			UTF8_TO_TCHAR(subject_name.c_str()),
			bSynced ? TEXT("true") : TEXT("false")));
		return sol::make_object(LuaView, bSynced);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_get_subject_frame_times(subject_name)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_get_subject_frame_times", [&Session](const std::string& subject_name, sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		FLiveLinkSubjectName SubjectName;
		SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));

		TArray<FLiveLinkTime> FrameTimes = Client->GetSubjectFrameTimes(SubjectName);

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (const FLiveLinkTime& FrameTime : FrameTimes)
		{
			sol::table Entry = LuaView.create_table();
			Entry["world_time"] = FrameTime.WorldTime;
			Entry["scene_time_seconds"] = FrameTime.SceneTime.AsSeconds();
			Entry["scene_frame_rate_num"] = FrameTime.SceneTime.Rate.Numerator;
			Entry["scene_frame_rate_den"] = FrameTime.SceneTime.Rate.Denominator;
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_get_subject_frame_times(%s) -> %d frames"),
			UTF8_TO_TCHAR(subject_name.c_str()), FrameTimes.Num()));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_list_roles()
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_list_roles", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		TArray<UClass*> DerivedClasses;
		GetDerivedClasses(ULiveLinkRole::StaticClass(), DerivedClasses, true);

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (UClass* RoleClass : DerivedClasses)
		{
			if (RoleClass->HasAnyClassFlags(CLASS_Abstract))
			{
				continue;
			}

			sol::table Entry = LuaView.create_table();
			Entry["name"] = std::string(TCHAR_TO_UTF8(*RoleClass->GetName()));

			ULiveLinkRole* CDO = RoleClass->GetDefaultObject<ULiveLinkRole>();
			if (CDO)
			{
				Entry["display_name"] = std::string(TCHAR_TO_UTF8(*CDO->GetDisplayName().ToString()));

				UScriptStruct* StaticStruct = CDO->GetStaticDataStruct();
				if (StaticStruct)
				{
					Entry["static_data_struct"] = std::string(TCHAR_TO_UTF8(*StaticStruct->GetName()));
				}
				UScriptStruct* FrameStruct = CDO->GetFrameDataStruct();
				if (FrameStruct)
				{
					Entry["frame_data_struct"] = std::string(TCHAR_TO_UTF8(*FrameStruct->GetName()));
				}
			}
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_list_roles() -> %d roles"), Idx - 1));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_get_subjects_for_role(role_name, include_disabled?, include_virtual?)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_get_subjects_for_role", [&Session](
		const std::string& role_name,
		sol::optional<bool> IncludeDisabled,
		sol::optional<bool> IncludeVirtual,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		TSubclassOf<ULiveLinkRole> RoleClass = FindRoleClassByName(NeoLuaStr::ToFString(role_name));
		if (!RoleClass)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_get_subjects_for_role -> role not found: %s"), UTF8_TO_TCHAR(role_name.c_str())));
			return sol::lua_nil;
		}

		bool bIncludeDisabled = IncludeDisabled.value_or(false);
		bool bIncludeVirtual = IncludeVirtual.value_or(false);

		TArray<FLiveLinkSubjectKey> Subjects = Client->GetSubjectsSupportingRole(RoleClass, bIncludeDisabled, bIncludeVirtual);

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (const FLiveLinkSubjectKey& SubjectKey : Subjects)
		{
			sol::table Entry = LuaView.create_table();
			Entry["name"] = std::string(TCHAR_TO_UTF8(*SubjectKey.SubjectName.Name.ToString()));
			Entry["source_guid"] = std::string(TCHAR_TO_UTF8(*SubjectKey.Source.ToString()));
			Entry["is_virtual"] = Client->IsVirtualSubject(SubjectKey);
			Result[Idx++] = Entry;
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_get_subjects_for_role(%s) -> %d subjects"),
			UTF8_TO_TCHAR(role_name.c_str()), Subjects.Num()));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_get_static_data(subject_name, source_guid)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_get_static_data", [&Session](
		const std::string& subject_name,
		const std::string& source_guid_str,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		FGuid SourceGuid;
		if (!FGuid::Parse(NeoLuaStr::ToFString(source_guid_str), SourceGuid))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_get_static_data -> invalid source GUID: %s"), UTF8_TO_TCHAR(source_guid_str.c_str())));
			return sol::lua_nil;
		}

		FLiveLinkSubjectKey SubjectKey;
		SubjectKey.SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
		SubjectKey.Source = SourceGuid;

		return LiveLink_PopulateStaticData(Client, SubjectKey, subject_name, Session, LuaView);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_evaluate_frame(subject_name, role_name?)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_evaluate_frame", [&Session](
		const std::string& subject_name,
		sol::optional<std::string> role_name,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		FLiveLinkSubjectName SubjectName;
		SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));

		// Resolve role
		TSubclassOf<ULiveLinkRole> RoleClass;
		if (role_name.has_value() && !role_name.value().empty())
		{
			RoleClass = FindRoleClassByName(NeoLuaStr::ToFStringOpt(role_name));
			if (!RoleClass)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] livelink_evaluate_frame -> role not found: %s"), UTF8_TO_TCHAR(role_name.value().c_str())));
				return sol::lua_nil;
			}
		}
		else
		{
			// Use the subject's own role
			RoleClass = Client->GetSubjectRole_AnyThread(SubjectName);
			if (!RoleClass)
			{
				Session.Log(FString::Printf(TEXT("[FAIL] livelink_evaluate_frame -> cannot determine role for %s"), UTF8_TO_TCHAR(subject_name.c_str())));
				return sol::lua_nil;
			}
		}

		FLiveLinkSubjectFrameData FrameData;
		bool bSuccess = Client->EvaluateFrame_AnyThread(SubjectName, RoleClass, FrameData);
		if (!bSuccess)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_evaluate_frame -> evaluation failed for %s"), UTF8_TO_TCHAR(subject_name.c_str())));
			return sol::lua_nil;
		}

		sol::table Result = LuaView.create_table();

		// Frame data base: property values
		const FLiveLinkBaseFrameData* BaseFrameData = FrameData.FrameData.GetBaseData();
		if (BaseFrameData)
		{
			sol::table PropValues = LuaView.create_table();
			int32 PVIdx = 1;
			for (float Val : BaseFrameData->PropertyValues)
			{
				PropValues[PVIdx++] = Val;
			}
			Result["property_values"] = PropValues;
			Result["world_time"] = BaseFrameData->WorldTime.GetOffsettedTime();
		}

		// Static data: property names
		const FLiveLinkBaseStaticData* BaseStaticData = FrameData.StaticData.GetBaseData();
		if (BaseStaticData)
		{
			sol::table PropNames = LuaView.create_table();
			int32 PNIdx = 1;
			for (const FName& PropName : BaseStaticData->PropertyNames)
			{
				PropNames[PNIdx++] = std::string(TCHAR_TO_UTF8(*PropName.ToString()));
			}
			Result["property_names"] = PropNames;
		}

		// Animation-specific: transforms
		const FLiveLinkAnimationFrameData* AnimFrameData = FrameData.FrameData.Cast<FLiveLinkAnimationFrameData>();
		if (AnimFrameData)
		{
			sol::table Transforms = LuaView.create_table();
			int32 TIdx = 1;
			for (const FTransform& Transform : AnimFrameData->Transforms)
			{
				sol::table T = LuaView.create_table();
				FVector Loc = Transform.GetLocation();
				FRotator Rot = Transform.Rotator();
				FVector Scale = Transform.GetScale3D();
				T["location_x"] = Loc.X;
				T["location_y"] = Loc.Y;
				T["location_z"] = Loc.Z;
				T["rotation_pitch"] = Rot.Pitch;
				T["rotation_yaw"] = Rot.Yaw;
				T["rotation_roll"] = Rot.Roll;
				T["scale_x"] = Scale.X;
				T["scale_y"] = Scale.Y;
				T["scale_z"] = Scale.Z;
				Transforms[TIdx++] = T;
			}
			Result["transforms"] = Transforms;
			Result["transform_count"] = AnimFrameData->Transforms.Num();

			// Also include bone names from static data if available
			const FLiveLinkSkeletonStaticData* SkeletonData = FrameData.StaticData.Cast<FLiveLinkSkeletonStaticData>();
			if (SkeletonData)
			{
				sol::table BoneNames = LuaView.create_table();
				int32 BIdx = 1;
				for (const FName& BoneName : SkeletonData->GetBoneNames())
				{
					BoneNames[BIdx++] = std::string(TCHAR_TO_UTF8(*BoneName.ToString()));
				}
				Result["bone_names"] = BoneNames;
			}
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_evaluate_frame(%s)"), UTF8_TO_TCHAR(subject_name.c_str())));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_clear_subject_frames(subject_name, source_guid?)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_clear_subject_frames", [&Session](
		const std::string& subject_name,
		sol::optional<std::string> source_guid_str,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		if (source_guid_str.has_value() && !source_guid_str.value().empty())
		{
			FGuid SourceGuid;
			if (!FGuid::Parse(NeoLuaStr::ToFStringOpt(source_guid_str), SourceGuid))
			{
				Session.Log(FString::Printf(TEXT("[FAIL] livelink_clear_subject_frames -> invalid source GUID: %s"), UTF8_TO_TCHAR(source_guid_str.value().c_str())));
				return sol::make_object(LuaView, false);
			}

			FLiveLinkSubjectKey SubjectKey;
			SubjectKey.SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
			SubjectKey.Source = SourceGuid;
			Client->ClearSubjectsFrames_AnyThread(SubjectKey);
		}
		else
		{
			FLiveLinkSubjectName SubjectName;
			SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
			Client->ClearSubjectsFrames_AnyThread(SubjectName);
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_clear_subject_frames(%s)"), UTF8_TO_TCHAR(subject_name.c_str())));
		return sol::make_object(LuaView, true);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_clear_all_frames()
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_clear_all_frames", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		Client->ClearAllSubjectsFrames_AnyThread();

		Session.Log(TEXT("[OK] livelink_clear_all_frames()"));
		return sol::make_object(LuaView, true);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_remove_subject(subject_name, source_guid)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_remove_subject", [&Session](
		const std::string& subject_name,
		const std::string& source_guid_str,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		FGuid SourceGuid;
		if (!FGuid::Parse(NeoLuaStr::ToFString(source_guid_str), SourceGuid))
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_remove_subject -> invalid source GUID: %s"), UTF8_TO_TCHAR(source_guid_str.c_str())));
			return sol::make_object(LuaView, false);
		}

		FLiveLinkSubjectKey SubjectKey;
		SubjectKey.SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));
		SubjectKey.Source = SourceGuid;

		Client->RemoveSubject_AnyThread(SubjectKey);

		Session.Log(FString::Printf(TEXT("[OK] livelink_remove_subject(%s, %s)"), UTF8_TO_TCHAR(subject_name.c_str()), UTF8_TO_TCHAR(source_guid_str.c_str())));
		return sol::make_object(LuaView, true);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_does_subject_support_role(subject_name, role_name)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_does_subject_support_role", [&Session](
		const std::string& subject_name,
		const std::string& role_name,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		TSubclassOf<ULiveLinkRole> RoleClass = FindRoleClassByName(NeoLuaStr::ToFString(role_name));
		if (!RoleClass)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_does_subject_support_role -> role not found: %s"), UTF8_TO_TCHAR(role_name.c_str())));
			return sol::make_object(LuaView, false);
		}

		FLiveLinkSubjectName SubjectName;
		SubjectName.Name = FName(NeoLuaStr::ToFString(subject_name));

		bool bSupports = Client->DoesSubjectSupportsRole_AnyThread(SubjectName, RoleClass);

		Session.Log(FString::Printf(TEXT("[OK] livelink_does_subject_support_role(%s, %s) -> %s"),
			UTF8_TO_TCHAR(subject_name.c_str()),
			UTF8_TO_TCHAR(role_name.c_str()),
			bSupports ? TEXT("true") : TEXT("false")));
		return sol::make_object(LuaView, bSupports);
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_get_virtual_sources()
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_get_virtual_sources", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::lua_nil;
		}

		TArray<FGuid> VirtualSources = Client->GetVirtualSources();

		sol::table Result = LuaView.create_table();
		int32 Idx = 1;
		for (const FGuid& SourceGuid : VirtualSources)
		{
			Result[Idx++] = std::string(TCHAR_TO_UTF8(*SourceGuid.ToString()));
		}

		Session.Log(FString::Printf(TEXT("[OK] livelink_get_virtual_sources() -> %d sources"), VirtualSources.Num()));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_configure_data_preview_component(actor_label, component_name, options?)
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_configure_data_preview_component", [&Session](
		const std::string& actor_label,
		const std::string& component_name,
		sol::optional<sol::table> options,
		sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		UWorld* World = GetEditorWorld();
		if (!World)
		{
			Session.Log(TEXT("[FAIL] livelink_configure_data_preview_component -> no editor world"));
			return sol::lua_nil;
		}

		const FString ActorLabel = NeoLuaStr::ToFString(actor_label);
		AActor* Actor = NeoLuaActor::FindByNameOrLabel(World, ActorLabel);
		if (!Actor)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_configure_data_preview_component -> actor not found: %s"), *ActorLabel));
			return sol::lua_nil;
		}

		const FString ComponentName = NeoLuaStr::ToFString(component_name);
		ULiveLinkDataPreviewComponent* Component = Cast<ULiveLinkDataPreviewComponent>(FindComponentByName(Actor, ComponentName));
		if (!Component)
		{
			Session.Log(FString::Printf(TEXT("[FAIL] livelink_configure_data_preview_component -> LiveLinkDataPreviewComponent not found: %s.%s"), *ActorLabel, *ComponentName));
			return sol::lua_nil;
		}

		bool bChanged = false;
		if (options.has_value())
		{
			const sol::table Options = options.value();
			Component->Modify();
			Actor->Modify();

			sol::object SubjectName = GetOptionValue(Options, "subject_name", "SubjectName");
			if (SubjectName != sol::lua_nil)
			{
				if (!SubjectName.is<std::string>())
				{
					Session.Log(TEXT("[FAIL] livelink_configure_data_preview_component -> subject_name must be a string"));
					return sol::lua_nil;
				}
				Component->SubjectName.Name = FName(NeoLuaStr::ToFString(SubjectName.as<std::string>()));
				bChanged = true;
			}

			sol::object bEvaluate = GetOptionValue(Options, "evaluate", "bEvaluateLiveLink");
			if (bEvaluate != sol::lua_nil)
			{
				if (!bEvaluate.is<bool>())
				{
					Session.Log(TEXT("[FAIL] livelink_configure_data_preview_component -> evaluate must be a boolean"));
					return sol::lua_nil;
				}
				Component->SetEvaluateLiveLinkData(bEvaluate.as<bool>());
				bChanged = true;
			}

			sol::object bDrawLabels = GetOptionValue(Options, "draw_labels", "bDrawLabels");
			if (bDrawLabels != sol::lua_nil)
			{
				if (!bDrawLabels.is<bool>())
				{
					Session.Log(TEXT("[FAIL] livelink_configure_data_preview_component -> draw_labels must be a boolean"));
					return sol::lua_nil;
				}
				Component->SetDrawLabels(bDrawLabels.as<bool>());
				bChanged = true;
			}

			sol::object BoneVisualType = GetOptionValue(Options, "bone_visual_type", "BoneVisualType");
			if (BoneVisualType != sol::lua_nil)
			{
				if (!BoneVisualType.is<std::string>())
				{
					Session.Log(TEXT("[FAIL] livelink_configure_data_preview_component -> bone_visual_type must be a string"));
					return sol::lua_nil;
				}
				ELiveLinkVisualBoneType ParsedType = Component->BoneVisualType;
				const FString BoneTypeValue = NeoLuaStr::ToFString(BoneVisualType.as<std::string>());
				if (!ParseLiveLinkVisualBoneType(BoneTypeValue, ParsedType))
				{
					Session.Log(FString::Printf(TEXT("[FAIL] livelink_configure_data_preview_component -> invalid BoneVisualType: %s"), *BoneTypeValue));
					return sol::lua_nil;
				}
				Component->BoneVisualType = ParsedType;
				bChanged = true;
			}
		}

		if (bChanged)
		{
			Component->MarkPackageDirty();
			Actor->MarkPackageDirty();
		}

		sol::table Result = MakeDataPreviewComponentInfo(LuaView, Component);
		Result["actor_label"] = std::string(TCHAR_TO_UTF8(*Actor->GetActorLabel()));
		Session.Log(FString::Printf(TEXT("[OK] livelink_configure_data_preview_component(%s, %s)"), *ActorLabel, *ComponentName));
		return Result;
	});

	// ────────────────────────────────────────────────────────────────────
	// livelink_force_tick()
	// ────────────────────────────────────────────────────────────────────
	Lua.set_function("livelink_force_tick", [&Session](sol::this_state S) -> sol::object
	{
		sol::state_view LuaView(S);

		ILiveLinkClient* Client = GetLiveLinkClient();
		if (!Client)
		{
			Session.Log(TEXT("[FAIL] LiveLink client not available"));
			return sol::make_object(LuaView, false);
		}

		Client->ForceTick();

		Session.Log(TEXT("[OK] livelink_force_tick()"));
		return sol::make_object(LuaView, true);
	});
}

namespace NSAILiveLinkExtension
{
	const TArray<FLuaFunctionDoc>& GetLiveLinkLuaDocs()
	{
		return LiveLinkDocs;
	}

	void BindLiveLinkLua(sol::state& Lua, FLuaSessionData& Session)
	{
		BindLiveLink(Lua, Session);
	}
}

