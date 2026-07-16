#include "Modules/ModuleManager.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "Lua/LuaBindingRegistry.h"

#include "AvaScene.h"
#include "AvaSequence.h"
#include "AvaSequenceActor.h"
#include "AvaSequencePlaybackObject.h"
#include "AvaSequencePlayer.h"
#include "ActorFactories/ActorFactory.h"
#include "AssetRegistry/AssetData.h"
#include "AvaTag.h"
#include "AvaTagAlias.h"
#include "AvaTagCollection.h"
#include "AvaTagHandle.h"
#include "AvaTagHandleContainer.h"
#include "AvaTagId.h"
#include "AvaTagLibrary.h"
#include "AvaTagList.h"
#include "AvaTagSoftHandle.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "IAssetTools.h"
#include "Marks/AvaMark.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneMarkedFrame.h"
#include "ScopedTransaction.h"
#include "Settings/AvaSequencePreset.h"
#include "Settings/AvaSequencerSettings.h"
#include "UObject/UnrealType.h"
#include "UObject/SoftObjectPath.h"

#include <sol/sol.hpp>

namespace
{
	const TCHAR* AvalancheSequenceExtensionId = TEXT("neostack.avalanche_sequence");

	FNeoStackExtensionDescriptor BuildAvalancheSequenceDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = AvalancheSequenceExtensionId;
		Descriptor.DisplayName = TEXT("Avalanche Sequence");
		Descriptor.ModuleName = TEXT("NSAI_AvalancheSequence");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("motion-design");
		Descriptor.Description = TEXT("Author embedded Motion Design sequences from Lua.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("motion-design"), TEXT("sequence") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}

	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	AAvaScene* EnsureScene()
	{
		UWorld* World = GetEditorWorld();
		ULevel* Level = World ? World->GetCurrentLevel() : nullptr;
		if (!World || !Level)
		{
			return nullptr;
		}
		const bool bHadScene = AAvaScene::GetScene(Level, false) != nullptr;
		AAvaScene* Scene = AAvaScene::GetScene(Level, true);
		if (Scene && !bHadScene)
		{
			Level->Modify();
			Scene->Modify();
			Level->MarkPackageDirty();
			Scene->MarkPackageDirty();
		}
		return Scene;
	}

	void MarkScenePackageDirty(AAvaScene* Scene)
	{
		if (!Scene)
		{
			return;
		}
		Scene->MarkPackageDirty();
		if (ULevel* Level = Scene->GetLevel())
		{
			Level->MarkPackageDirty();
		}
	}

	IAvaSequenceProvider* GetProvider(AAvaScene* Scene)
	{
		return Scene ? static_cast<IAvaSequenceProvider*>(Scene) : nullptr;
	}

	UAvaSequence* GetSequence(IAvaSequenceProvider* Provider, int32 LuaIndex)
	{
		if (!Provider || LuaIndex < 1)
		{
			return nullptr;
		}

		const TArray<TObjectPtr<UAvaSequence>>& Sequences = Provider->GetSequences();
		const int32 Index = LuaIndex - 1;
		return Sequences.IsValidIndex(Index) ? Sequences[Index].Get() : nullptr;
	}

	FString NormalizeAssetObjectPath(const FString& Path)
	{
		if (Path.IsEmpty() || Path.Contains(TEXT(".")))
		{
			return Path;
		}

		int32 LastSlash = INDEX_NONE;
		if (Path.FindLastChar(TEXT('/'), LastSlash) && LastSlash != INDEX_NONE)
		{
			const FString AssetName = Path.Mid(LastSlash + 1);
			return Path + TEXT(".") + AssetName;
		}
		return Path;
	}

	const TCHAR* PlaybackStatusToString(EMovieScenePlayerStatus::Type Status)
	{
		switch (Status)
		{
		case EMovieScenePlayerStatus::Stopped:
			return TEXT("Stopped");
		case EMovieScenePlayerStatus::Playing:
			return TEXT("Playing");
		case EMovieScenePlayerStatus::Scrubbing:
			return TEXT("Scrubbing");
		case EMovieScenePlayerStatus::Jumping:
			return TEXT("Jumping");
		case EMovieScenePlayerStatus::Stepping:
			return TEXT("Stepping");
		case EMovieScenePlayerStatus::Paused:
			return TEXT("Paused");
		default:
			return TEXT("Unknown");
		}
	}

	EAvaSequencePlayMode ParsePlayMode(const std::string& Value, EAvaSequencePlayMode DefaultValue)
	{
		const FString Mode = UTF8_TO_TCHAR(Value.c_str());
		if (Mode.Equals(TEXT("Reverse"), ESearchCase::IgnoreCase))
		{
			return EAvaSequencePlayMode::Reverse;
		}
		if (Mode.Equals(TEXT("Forward"), ESearchCase::IgnoreCase))
		{
			return EAvaSequencePlayMode::Forward;
		}
		return DefaultValue;
	}

	void ApplyTimeOptions(FAvaSequenceTime& Time, const sol::table& Options, const char* Prefix)
	{
		const FString SecondsKey = FString::Printf(TEXT("%s_seconds"), UTF8_TO_TCHAR(Prefix));
		if (sol::optional<double> Seconds = Options.get<sol::optional<double>>(TCHAR_TO_UTF8(*SecondsKey)))
		{
			Time = FAvaSequenceTime(*Seconds);
			return;
		}

		const FString FrameKey = FString::Printf(TEXT("%s_frame"), UTF8_TO_TCHAR(Prefix));
		if (sol::optional<int32> Frame = Options.get<sol::optional<int32>>(TCHAR_TO_UTF8(*FrameKey)))
		{
			Time.TimeType = EAvaSequenceTimeType::Frame;
			Time.Frame = *Frame;
			Time.SubFrame = 0.0;
			Time.bHasTimeConstraint = true;
			return;
		}

		const FString MarkKey = FString::Printf(TEXT("%s_mark"), UTF8_TO_TCHAR(Prefix));
		const std::string Mark = Options.get_or<std::string>(TCHAR_TO_UTF8(*MarkKey), "");
		if (!Mark.empty())
		{
			Time = FAvaSequenceTime(FString(UTF8_TO_TCHAR(Mark.c_str())));
		}
	}

	FAvaSequencePlayParams BuildPlayParams(const sol::optional<sol::table>& Options)
	{
		FAvaSequencePlayParams Params;
		if (!Options)
		{
			return Params;
		}

		const std::string Mode = Options->get_or<std::string>("mode", "");
		if (!Mode.empty())
		{
			Params.PlayMode = ParsePlayMode(Mode, Params.PlayMode);
		}
		ApplyTimeOptions(Params.Start, *Options, "start");
		ApplyTimeOptions(Params.End, *Options, "end");
		if (sol::optional<int32> LoopCount = Options->get<sol::optional<int32>>("loop_count"))
		{
			Params.AdvancedSettings.LoopCount = *LoopCount;
		}
		if (sol::optional<float> PlaybackSpeed = Options->get<sol::optional<float>>("playback_speed"))
		{
			Params.AdvancedSettings.PlaybackSpeed = *PlaybackSpeed;
		}
		if (sol::optional<bool> bRestoreState = Options->get<sol::optional<bool>>("restore_state"))
		{
			Params.AdvancedSettings.bRestoreState = *bRestoreState;
		}
		return Params;
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

	TMap<FName, FAvaTagId> BuildTagIdByName(UAvaTagCollection* Collection, bool bIncludeAliases)
	{
		TMap<FName, FAvaTagId> Result;
		if (!Collection)
		{
			return Result;
		}

		for (const FAvaTagId& TagId : Collection->GetTagIds(bIncludeAliases))
		{
			const FName Name = Collection->GetTagName(TagId);
			if (!Name.IsNone())
			{
				Result.Add(Name, TagId);
			}
		}
		return Result;
	}

	bool IsTagIdInArray(const TArray<FAvaTagId>& TagIds, const FAvaTagId& Candidate)
	{
		for (const FAvaTagId& TagId : TagIds)
		{
			if (TagId == Candidate)
			{
				return true;
			}
		}
		return false;
	}

	bool ConfigureTagCollection(UAvaTagCollection* Collection, const sol::table& Options)
	{
		if (!Collection)
		{
			return false;
		}

		sol::optional<sol::table> TagsOption = Options.get<sol::optional<sol::table>>("tags");
		sol::optional<sol::table> AliasesOption = Options.get<sol::optional<sol::table>>("aliases");

		FMapProperty* TagsProperty = FindFProperty<FMapProperty>(UAvaTagCollection::StaticClass(), UAvaTagCollection::GetTagMapName());
		if (!TagsProperty || !TagsProperty->KeyProp || !TagsProperty->ValueProp)
		{
			return false;
		}

		FStructProperty* TagKeyStructProperty = CastField<FStructProperty>(TagsProperty->KeyProp);
		if (!TagKeyStructProperty || TagKeyStructProperty->Struct != FAvaTagId::StaticStruct())
		{
			return false;
		}

		FStructProperty* ValueStructProperty = CastField<FStructProperty>(TagsProperty->ValueProp);
		if (!ValueStructProperty || ValueStructProperty->Struct != FAvaTag::StaticStruct())
		{
			return false;
		}

		FMapProperty* AliasesProperty = FindFProperty<FMapProperty>(UAvaTagCollection::StaticClass(), UAvaTagCollection::GetAliasMapName());
		FStructProperty* AliasKeyStructProperty = AliasesProperty ? CastField<FStructProperty>(AliasesProperty->KeyProp) : nullptr;
		FStructProperty* AliasValueStructProperty = AliasesProperty ? CastField<FStructProperty>(AliasesProperty->ValueProp) : nullptr;
		if (AliasesOption && (!AliasesProperty || !AliasKeyStructProperty || AliasKeyStructProperty->Struct != FAvaTagId::StaticStruct() || !AliasValueStructProperty || AliasValueStructProperty->Struct != FAvaTagAlias::StaticStruct()))
		{
			return false;
		}

		Collection->Modify();
		int32 AddedTagCount = 0;
		if (TagsOption)
		{
			FScriptMapHelper TagsHelper(TagsProperty, TagsProperty->ContainerPtrToValuePtr<void>(Collection));
			TagsHelper.EmptyValues();

			for (const auto& Entry : *TagsOption)
			{
				const sol::object ValueObject = Entry.second;
				std::string TagValue;
				if (ValueObject.is<std::string>())
				{
					TagValue = ValueObject.as<std::string>();
				}
				else if (ValueObject.is<sol::table>())
				{
					TagValue = ValueObject.as<sol::table>().get_or<std::string>("name", "");
				}

				const FString TagString = UTF8_TO_TCHAR(TagValue.c_str());
				if (TagString.IsEmpty())
				{
					continue;
				}

				const int32 InternalIndex = TagsHelper.AddDefaultValue_Invalid_NeedsRehash();
				if (!TagsHelper.IsValidIndex(InternalIndex))
				{
					continue;
				}

				FAvaTagId TagId(ForceInit);
				FAvaTag Tag;
				Tag.TagName = FName(*TagString);

				TagsProperty->KeyProp->CopyCompleteValue(TagsHelper.GetKeyPtr(InternalIndex), &TagId);
				TagsProperty->ValueProp->CopyCompleteValue(TagsHelper.GetValuePtr(InternalIndex), &Tag);
				++AddedTagCount;
			}
			TagsHelper.Rehash();
		}

		TMap<FName, FAvaTagId> TagIdsByName = BuildTagIdByName(Collection, false);
		int32 AddedAliasCount = 0;
		if (AliasesOption && AliasesProperty)
		{
			FScriptMapHelper AliasesHelper(AliasesProperty, AliasesProperty->ContainerPtrToValuePtr<void>(Collection));
			AliasesHelper.EmptyValues();

			for (const auto& Entry : *AliasesOption)
			{
				const sol::optional<sol::table> AliasTable = Entry.second.as<sol::optional<sol::table>>();
				if (!AliasTable)
				{
					continue;
				}

				const std::string AliasValue = AliasTable->get_or<std::string>("name", "");
				const FString AliasString = UTF8_TO_TCHAR(AliasValue.c_str());
				if (AliasString.IsEmpty())
				{
					continue;
				}

				FAvaTagAlias Alias;
				Alias.AliasName = FName(*AliasString);
				if (sol::optional<sol::table> AliasTags = AliasTable->get<sol::optional<sol::table>>("tags"))
				{
					for (const auto& AliasTagEntry : *AliasTags)
					{
						const sol::optional<std::string> TagNameValue = AliasTagEntry.second.as<sol::optional<std::string>>();
						if (!TagNameValue)
						{
							continue;
						}
						const FName TagName(UTF8_TO_TCHAR(TagNameValue->c_str()));
						if (const FAvaTagId* TagId = TagIdsByName.Find(TagName))
						{
							Alias.TagIds.AddUnique(*TagId);
						}
					}
				}

				if (Alias.TagIds.IsEmpty())
				{
					continue;
				}

				const int32 InternalIndex = AliasesHelper.AddDefaultValue_Invalid_NeedsRehash();
				if (!AliasesHelper.IsValidIndex(InternalIndex))
				{
					continue;
				}

				FAvaTagId AliasId(ForceInit);
				AliasesProperty->KeyProp->CopyCompleteValue(AliasesHelper.GetKeyPtr(InternalIndex), &AliasId);
				AliasesProperty->ValueProp->CopyCompleteValue(AliasesHelper.GetValuePtr(InternalIndex), &Alias);
				++AddedAliasCount;
			}
			AliasesHelper.Rehash();
		}

		Collection->PostEditChange();
		Collection->MarkPackageDirty();
		return AddedTagCount > 0 || AddedAliasCount > 0;
	}

	sol::table BuildResolvedTagNames(sol::state_view Lua, const TArray<FAvaTag>& Tags)
	{
		sol::table Result = Lua.create_table();
		int32 LuaIndex = 0;
		for (const FAvaTag& Tag : Tags)
		{
			Result[++LuaIndex] = TCHAR_TO_UTF8(*Tag.TagName.ToString());
		}
		return Result;
	}

	sol::table BuildResolvedTagNames(sol::state_view Lua, const FAvaTagList& TagList)
	{
		sol::table Result = Lua.create_table();
		int32 LuaIndex = 0;
		for (const FAvaTag* Tag : TagList)
		{
			if (Tag)
			{
				Result[++LuaIndex] = TCHAR_TO_UTF8(*Tag->TagName.ToString());
			}
		}
		return Result;
	}

	sol::table BuildTagHandleInfo(sol::state_view Lua, const FAvaTagHandle& Handle)
	{
		sol::table Info = Lua.create_table();
		Info["valid"] = Handle.IsValid();
		Info["name"] = TCHAR_TO_UTF8(*Handle.ToName().ToString());
		Info["string"] = TCHAR_TO_UTF8(*Handle.ToString());
		Info["debug"] = TCHAR_TO_UTF8(*Handle.ToDebugString());
		const TArray<FAvaTag> ResolvedTags = UAvaTagLibrary::ResolveTagHandle(Handle);
		Info["resolved_count"] = ResolvedTags.Num();
		Info["resolved"] = BuildResolvedTagNames(Lua, ResolvedTags);

		FAvaTagSoftHandle SoftHandle(Handle);
		const FAvaTagHandle ResolvedSoftHandle = UAvaTagLibrary::ResolveTagSoftHandle(SoftHandle);
		Info["soft_valid"] = SoftHandle.IsValid();
		Info["soft_matches_exact"] = SoftHandle.MatchesExact(Handle);
		Info["soft_resolved_matches_exact"] = ResolvedSoftHandle.MatchesExact(Handle);
		return Info;
	}

	sol::table BuildTagCollectionInfo(sol::state_view Lua, UAvaTagCollection* Collection)
	{
		sol::table Info = Lua.create_table();
		Info["valid"] = Collection != nullptr;
		Info["path"] = Collection ? TCHAR_TO_UTF8(*Collection->GetPathName()) : "";
		Info["tag_count"] = Collection ? Collection->GetTagIds(false).Num() : 0;

		sol::table Tags = Lua.create_table();
		TArray<FAvaTagId> PlainTagIds;
		if (Collection)
		{
			PlainTagIds = Collection->GetTagIds(false);
			int32 LuaIndex = 0;
			for (const FAvaTagId& TagId : PlainTagIds)
			{
				const FAvaTagHandle Handle(Collection, TagId);
				sol::table Row = Lua.create_table();
				Row["id"] = TCHAR_TO_UTF8(*TagId.ToString());
				Row["name"] = TCHAR_TO_UTF8(*Collection->GetTagName(TagId).ToString());
				Row["handle"] = BuildTagHandleInfo(Lua, Handle);
				Tags[++LuaIndex] = Row;
			}
		}
		Info["tags"] = Tags;

		sol::table Aliases = Lua.create_table();
		if (Collection)
		{
			int32 LuaIndex = 0;
			for (const FAvaTagId& TagId : Collection->GetTagIds(true))
			{
				if (IsTagIdInArray(PlainTagIds, TagId))
				{
					continue;
				}

				const FAvaTagHandle Handle(Collection, TagId);
				const FAvaTagList ResolvedList = Collection->GetTags(TagId);
				sol::table Row = Lua.create_table();
				Row["id"] = TCHAR_TO_UTF8(*TagId.ToString());
				Row["name"] = TCHAR_TO_UTF8(*Collection->GetTagName(TagId).ToString());
				Row["resolved_count"] = ResolvedList.Tags.Num();
				Row["resolved"] = BuildResolvedTagNames(Lua, ResolvedList);
				Row["handle"] = BuildTagHandleInfo(Lua, Handle);
				Aliases[++LuaIndex] = Row;
			}
			Info["alias_count"] = LuaIndex;
		}
		else
		{
			Info["alias_count"] = 0;
		}
		Info["aliases"] = Aliases;

		sol::table Checks = Lua.create_table();
		if (Collection && PlainTagIds.Num() > 0)
		{
			FAvaTagHandleContainer Container;
			const FAvaTagHandle FirstHandle(Collection, PlainTagIds[0]);
			Checks["add_first"] = Container.AddTagHandle(FirstHandle);
			Checks["contains_first_exact"] = Container.ContainsTagHandle(FirstHandle);
			Checks["contains_first_resolved"] = Container.ContainsTag(FirstHandle);
			if (PlainTagIds.Num() > 1)
			{
				const FAvaTagHandle SecondHandle(Collection, PlainTagIds[1]);
				Checks["add_second"] = Container.AddTagHandle(SecondHandle);
				Checks["contains_second_exact"] = Container.ContainsTagHandle(SecondHandle);
			}
			const TArray<FAvaTag> ContainerTags = Container.ResolveTags();
			Checks["container_resolved_count"] = ContainerTags.Num();
			Checks["container_resolved"] = BuildResolvedTagNames(Lua, ContainerTags);
			Checks["container_string"] = TCHAR_TO_UTF8(*Container.ToString());

			if (Collection->GetTagIds(true).Num() > PlainTagIds.Num())
			{
				for (const FAvaTagId& TagId : Collection->GetTagIds(true))
				{
					if (!IsTagIdInArray(PlainTagIds, TagId))
					{
						const FAvaTagHandle AliasHandle(Collection, TagId);
						Checks["first_overlaps_alias"] = FirstHandle.Overlaps(AliasHandle);
						Checks["container_contains_alias_resolved"] = Container.ContainsTag(AliasHandle);
						break;
					}
				}
			}
		}
		Info["checks"] = Checks;
		return Info;
	}

		EAvaMarkRole ParseMarkRole(const std::string& Value, EAvaMarkRole DefaultValue)
		{
			const FString Role = UTF8_TO_TCHAR(Value.c_str());
			if (Role.Equals(TEXT("Stop"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkRole::Stop;
			}
			if (Role.Equals(TEXT("Pause"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkRole::Pause;
			}
			if (Role.Equals(TEXT("Jump"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkRole::Jump;
			}
			if (Role.Equals(TEXT("Reverse"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkRole::Reverse;
			}
			if (Role.Equals(TEXT("None"), ESearchCase::IgnoreCase) || Role.Equals(TEXT("Mark"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkRole::None;
			}
			return DefaultValue;
		}

		EAvaMarkDirection ParseMarkDirection(const std::string& Value, EAvaMarkDirection DefaultValue)
		{
			const FString Direction = UTF8_TO_TCHAR(Value.c_str());
			if (Direction.Equals(TEXT("Forwards"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("Forward"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkDirection::Forwards;
			}
			if (Direction.Equals(TEXT("Backwards"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("Backward"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkDirection::Backwards;
			}
			if (Direction.Equals(TEXT("Both"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkDirection::Both;
			}
			return DefaultValue;
		}

		EAvaMarkSearchDirection ParseMarkSearchDirection(const std::string& Value, EAvaMarkSearchDirection DefaultValue)
		{
			const FString Direction = UTF8_TO_TCHAR(Value.c_str());
			if (Direction.Equals(TEXT("SameDirection"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("Same"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkSearchDirection::SameDirection;
			}
			if (Direction.Equals(TEXT("OppositeDirection"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("Opposite"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkSearchDirection::OppositeDirection;
			}
			if (Direction.Equals(TEXT("AbsoluteForwards"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("AbsoluteForward"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkSearchDirection::AbsoluteForwards;
			}
			if (Direction.Equals(TEXT("AbsoluteBackwards"), ESearchCase::IgnoreCase) || Direction.Equals(TEXT("AbsoluteBackward"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkSearchDirection::AbsoluteBackwards;
			}
			if (Direction.Equals(TEXT("All"), ESearchCase::IgnoreCase))
			{
				return EAvaMarkSearchDirection::All;
			}
			return DefaultValue;
		}

		FString GetPreviewMarkLabel(UAvaSequence* Sequence)
		{
			if (!Sequence)
			{
				return FString();
			}
			if (const FStrProperty* Property = FindFProperty<FStrProperty>(UAvaSequence::StaticClass(), TEXT("PreviewMarkLabel")))
			{
				return Property->GetPropertyValue_InContainer(Sequence);
			}
			return FString();
		}

		bool SetPreviewMarkLabel(UAvaSequence* Sequence, const FString& Label)
		{
			if (!Sequence)
			{
				return false;
			}
			if (const FStrProperty* Property = FindFProperty<FStrProperty>(UAvaSequence::StaticClass(), TEXT("PreviewMarkLabel")))
			{
				Property->SetPropertyValue_InContainer(Sequence, Label);
				return true;
			}
			return false;
		}

		const TCHAR* MarkRoleToString(EAvaMarkRole Role)
		{
			switch (Role)
			{
			case EAvaMarkRole::Stop:
				return TEXT("Stop");
			case EAvaMarkRole::Pause:
				return TEXT("Pause");
			case EAvaMarkRole::Jump:
				return TEXT("Jump");
			case EAvaMarkRole::Reverse:
				return TEXT("Reverse");
			case EAvaMarkRole::None:
			default:
				return TEXT("None");
			}
		}

		const TCHAR* MarkDirectionToString(EAvaMarkDirection Direction)
		{
			switch (Direction)
			{
			case EAvaMarkDirection::Forwards:
				return TEXT("Forwards");
			case EAvaMarkDirection::Backwards:
				return TEXT("Backwards");
			case EAvaMarkDirection::Both:
			default:
				return TEXT("Both");
			}
		}

		const TCHAR* MarkSearchDirectionToString(EAvaMarkSearchDirection Direction)
		{
			switch (Direction)
			{
			case EAvaMarkSearchDirection::SameDirection:
				return TEXT("SameDirection");
			case EAvaMarkSearchDirection::OppositeDirection:
				return TEXT("OppositeDirection");
			case EAvaMarkSearchDirection::AbsoluteForwards:
				return TEXT("AbsoluteForwards");
			case EAvaMarkSearchDirection::AbsoluteBackwards:
				return TEXT("AbsoluteBackwards");
			case EAvaMarkSearchDirection::All:
			default:
				return TEXT("All");
			}
		}

		void ApplyMarkOptions(FAvaMark& Mark, const sol::table& Options)
		{
			const std::string Role = Options.get_or<std::string>("role", "");
			if (!Role.empty())
			{
				Mark.Role = ParseMarkRole(Role, Mark.Role);
			}

			const std::string Direction = Options.get_or<std::string>("direction", "");
			if (!Direction.empty())
			{
				Mark.Direction = ParseMarkDirection(Direction, Mark.Direction);
			}

			if (sol::optional<bool> bLocal = Options.get<sol::optional<bool>>("local"))
			{
				Mark.bIsLocalMark = *bLocal;
			}
			if (sol::optional<bool> bLimitPlayCount = Options.get<sol::optional<bool>>("limit_play_count_enabled"))
			{
				Mark.bLimitPlayCountEnabled = *bLimitPlayCount;
			}
			if (sol::optional<int32> LimitPlayCount = Options.get<sol::optional<int32>>("limit_play_count"))
			{
				Mark.LimitPlayCount = *LimitPlayCount;
			}
			if (sol::optional<float> PauseTime = Options.get<sol::optional<float>>("pause_time"))
			{
				Mark.PauseTime = *PauseTime;
			}

			const std::string JumpLabel = Options.get_or<std::string>("jump_label", "");
			if (!JumpLabel.empty())
			{
				Mark.JumpLabel = UTF8_TO_TCHAR(JumpLabel.c_str());
			}

			const std::string SearchDirection = Options.get_or<std::string>("search_direction", "");
			if (!SearchDirection.empty())
			{
				Mark.SearchDirection = ParseMarkSearchDirection(SearchDirection, Mark.SearchDirection);
			}
		}

		sol::table BuildMarkInfo(sol::state_view Lua, UAvaSequence* Sequence, const FAvaMark& Mark)
		{
			sol::table Info = Lua.create_table();
			const FString Label(Mark.GetLabel());
			Info["label"] = TCHAR_TO_UTF8(*Label);
			Info["role"] = TCHAR_TO_UTF8(MarkRoleToString(Mark.Role));
			Info["direction"] = TCHAR_TO_UTF8(MarkDirectionToString(Mark.Direction));
			Info["local"] = Mark.bIsLocalMark;
			Info["limit_play_count_enabled"] = Mark.bLimitPlayCountEnabled;
			Info["limit_play_count"] = Mark.LimitPlayCount;
			Info["pause_time"] = Mark.PauseTime;
			Info["jump_label"] = TCHAR_TO_UTF8(*Mark.JumpLabel);
			Info["search_direction"] = TCHAR_TO_UTF8(MarkSearchDirectionToString(Mark.SearchDirection));
			Info["is_preview"] = Sequence && Sequence->GetPreviewMark() == &Mark;

			sol::table Frames = Lua.create_table();
			int32 LuaFrameIndex = 0;
			for (int32 Frame : Mark.Frames)
			{
				Frames[++LuaFrameIndex] = Frame;
			}
			Info["frame_count"] = Mark.Frames.Num();
			Info["frames"] = Frames;
			return Info;
		}

	int32 FindSequenceIndex(IAvaSequenceProvider* Provider, const UAvaSequence* Sequence)
	{
		if (!Provider || !Sequence)
		{
			return 0;
		}

		const TArray<TObjectPtr<UAvaSequence>>& Sequences = Provider->GetSequences();
		for (int32 Index = 0; Index < Sequences.Num(); ++Index)
		{
			if (Sequences[Index].Get() == Sequence)
			{
				return Index + 1;
			}
		}
		return 0;
	}

	bool IsDescendantOf(UAvaSequence* Candidate, UAvaSequence* PossibleAncestor)
	{
		for (UAvaSequence* Parent = Candidate ? Candidate->GetParent() : nullptr; Parent; Parent = Parent->GetParent())
		{
			if (Parent == PossibleAncestor)
			{
				return true;
			}
		}
		return false;
	}

	void InitializeMovieScene(UAvaSequence* Sequence)
	{
		if (!Sequence)
		{
			return;
		}

		UMovieScene* MovieScene = Sequence->GetMovieScene();
		const UAvaSequencerSettings* Settings = GetDefault<UAvaSequencerSettings>();
		if (!MovieScene || !Settings)
		{
			return;
		}

		MovieScene->SetDisplayRate(Settings->GetDisplayRate());

		const double StartTime = Settings->GetStartTime();
		const double EndTime = Settings->GetEndTime();
		const FFrameTime StartFrame = StartTime * MovieScene->GetTickResolution();
		const FFrameTime EndFrame = EndTime * MovieScene->GetTickResolution();

		MovieScene->SetPlaybackRange(TRange<FFrameNumber>(StartFrame.FrameNumber, EndFrame.FrameNumber + 1));
		MovieScene->GetEditorData().WorkStart = StartTime;
		MovieScene->GetEditorData().WorkEnd = EndTime;
	}

	sol::table BuildSequenceInfo(sol::state_view Lua, IAvaSequenceProvider* Provider, UAvaSequence* Sequence)
	{
		sol::table Info = Lua.create_table();
		Info["index"] = FindSequenceIndex(Provider, Sequence);
		Info["name"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetName()) : "";
			Info["label"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetLabel().ToString()) : "";
			Info["display_name"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetDisplayName().ToString()) : "";
			Info["parent_index"] = Sequence ? FindSequenceIndex(Provider, Sequence->GetParent()) : 0;
			Info["start_time"] = Sequence ? Sequence->GetStartTime() : 0.0;
			Info["end_time"] = Sequence ? Sequence->GetEndTime() : 0.0;
			Info["tag_valid"] = Sequence && Sequence->GetSequenceTag().IsValid();
			Info["tag_name"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetSequenceTag().ToName().ToString()) : "";
			Info["tag_string"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetSequenceTag().ToString()) : "";
			Info["mark_count"] = Sequence ? Sequence->GetMarks().Num() : 0;
			Info["preview_mark_label"] = Sequence ? TCHAR_TO_UTF8(*GetPreviewMarkLabel(Sequence)) : "";
			Info["has_preview_mark"] = Sequence && Sequence->GetPreviewMark();

			sol::table Marks = Lua.create_table();
			if (Sequence)
			{
				int32 LuaMarkIndex = 0;
				for (const FAvaMark& Mark : Sequence->GetMarks())
				{
					Marks[++LuaMarkIndex] = BuildMarkInfo(Lua, Sequence, Mark);
				}
			}
			Info["marks"] = Marks;

		sol::table Children = Lua.create_table();
		if (Sequence)
		{
			int32 LuaChildIndex = 0;
			for (const TWeakObjectPtr<UAvaSequence>& ChildWeak : Sequence->GetChildren())
			{
				if (UAvaSequence* Child = ChildWeak.Get())
				{
					Children[++LuaChildIndex] = FindSequenceIndex(Provider, Child);
				}
			}
		}
		Info["child_count"] = Sequence ? Sequence->GetChildren().Num() : 0;
		Info["children"] = Children;
		return Info;
	}

	sol::table BuildSequenceTimeInfo(sol::state_view Lua, const FAvaSequenceTime& Time)
	{
		sol::table Info = Lua.create_table();
		Info["has_constraint"] = Time.bHasTimeConstraint;
		switch (Time.TimeType)
		{
		case EAvaSequenceTimeType::Frame:
			Info["type"] = "Frame";
			break;
		case EAvaSequenceTimeType::Seconds:
			Info["type"] = "Seconds";
			break;
		case EAvaSequenceTimeType::Mark:
			Info["type"] = "Mark";
			break;
		case EAvaSequenceTimeType::None:
		default:
			Info["type"] = "None";
			break;
		}
		Info["frame"] = Time.Frame;
		Info["sub_frame"] = Time.SubFrame;
		Info["seconds"] = Time.Seconds;
		Info["mark_label"] = TCHAR_TO_UTF8(*Time.MarkLabel);
		return Info;
	}

	sol::table BuildPlayParamsInfo(sol::state_view Lua, const FAvaSequencePlayParams& Params)
	{
		sol::table Info = Lua.create_table();
		Info["mode"] = Params.PlayMode == EAvaSequencePlayMode::Reverse ? "Reverse" : "Forward";
		Info["start"] = BuildSequenceTimeInfo(Lua, Params.Start);
		Info["end"] = BuildSequenceTimeInfo(Lua, Params.End);
		Info["loop_count"] = Params.AdvancedSettings.LoopCount;
		Info["playback_speed"] = Params.AdvancedSettings.PlaybackSpeed;
		Info["restore_state"] = Params.AdvancedSettings.bRestoreState;
		return Info;
	}

	sol::table BuildPlayerInfo(sol::state_view Lua, IAvaSequenceProvider* Provider, UAvaSequencePlayer* Player)
	{
		sol::table Info = Lua.create_table();
		UAvaSequence* Sequence = Player ? Player->GetAvaSequence() : nullptr;
		Info["valid"] = Player != nullptr;
		Info["player_class"] = Player ? TCHAR_TO_UTF8(*Player->GetClass()->GetName()) : "";
		Info["player_name"] = Player ? TCHAR_TO_UTF8(*Player->GetName()) : "";
		Info["sequence_index"] = FindSequenceIndex(Provider, Sequence);
		Info["sequence_label"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetLabel().ToString()) : "";
		Info["sequence_name"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetName()) : "";
		Info["status"] = Player ? TCHAR_TO_UTF8(PlaybackStatusToString(Player->GetPlaybackStatus())) : "";
		Info["is_playing"] = Player ? Player->IsPlaying() : false;
		Info["play_rate"] = Player ? Player->GetPlayRate() : 0.0f;
		Info["current_seconds"] = Player ? Player->GetCurrentTime().AsSeconds() : 0.0;
		Info["global_seconds"] = Player ? Player->GetGlobalTime().AsSeconds() : 0.0;
		Info["playback_object_valid"] = Player && Player->GetPlaybackObject();
		return Info;
	}

	sol::table BuildSequenceActorInfo(sol::state_view Lua, IAvaSequenceProvider* Provider, AAvaSequenceActor* Actor)
	{
		sol::table Info = Lua.create_table();
		UAvaSequence* Sequence = Actor ? Cast<UAvaSequence>(Actor->GetSequence()) : nullptr;
		UAvaSequencePlayer* Player = Actor ? Cast<UAvaSequencePlayer>(Actor->GetSequencePlayer()) : nullptr;
		const FVector Location = Actor ? Actor->GetActorLocation() : FVector::ZeroVector;

		Info["valid"] = Actor != nullptr;
		Info["actor_class"] = Actor ? TCHAR_TO_UTF8(*Actor->GetClass()->GetName()) : "";
		Info["actor_name"] = Actor ? TCHAR_TO_UTF8(*Actor->GetName()) : "";
#if WITH_EDITOR
		Info["actor_label"] = Actor ? TCHAR_TO_UTF8(*Actor->GetActorLabel()) : "";
#else
		Info["actor_label"] = "";
#endif
		Info["sequence_index"] = FindSequenceIndex(Provider, Sequence);
		Info["sequence_label"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetLabel().ToString()) : "";
		Info["sequence_name"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetName()) : "";
		Info["sequence_path"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetPathName()) : "";
		Info["player_class"] = Player ? TCHAR_TO_UTF8(*Player->GetClass()->GetName()) : "";
		Info["player_sequence_label"] = Player && Player->GetAvaSequence() ? TCHAR_TO_UTF8(*Player->GetAvaSequence()->GetLabel().ToString()) : "";
		Info["player_playback_object_valid"] = Player && Player->GetPlaybackObject();
		Info["player_status"] = Player ? TCHAR_TO_UTF8(PlaybackStatusToString(Player->GetPlaybackStatus())) : "";
		Info["x"] = Location.X;
		Info["y"] = Location.Y;
		Info["z"] = Location.Z;
		return Info;
	}

	TArray<AAvaSequenceActor*> FindSequenceActorsForLabel(UWorld* World, const FString& Label)
	{
		TArray<AAvaSequenceActor*> Result;
		if (!World)
		{
			return Result;
		}

		for (TActorIterator<AAvaSequenceActor> It(World); It; ++It)
		{
			AAvaSequenceActor* Actor = *It;
			UAvaSequence* Sequence = Actor ? Cast<UAvaSequence>(Actor->GetSequence()) : nullptr;
			if (Sequence && (Label.IsEmpty() || Sequence->GetLabel().ToString() == Label))
			{
				Result.Add(Actor);
			}
		}
		return Result;
	}

	sol::table BuildSequenceActorList(sol::state_view Lua, IAvaSequenceProvider* Provider, UWorld* World, const FString& Label)
	{
		sol::table Result = Lua.create_table();
		int32 LuaIndex = 0;
		for (AAvaSequenceActor* Actor : FindSequenceActorsForLabel(World, Label))
		{
			Result[++LuaIndex] = BuildSequenceActorInfo(Lua, Provider, Actor);
		}
		return Result;
	}

	sol::table BuildExportedSequenceInfo(sol::state_view Lua, UAvaSequence* Sequence)
	{
		sol::table Info = Lua.create_table();
		Info["valid"] = Sequence != nullptr;
		Info["class"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetClass()->GetName()) : "";
		Info["name"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetName()) : "";
		Info["object_path"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetPathName()) : "";
		Info["package_path"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetOutermost()->GetName()) : "";
		Info["label"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetLabel().ToString()) : "";
		Info["display_name"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetDisplayName().ToString()) : "";
		Info["start_time"] = Sequence ? Sequence->GetStartTime() : 0.0;
		Info["end_time"] = Sequence ? Sequence->GetEndTime() : 0.0;
		Info["parent_valid"] = Sequence && Sequence->GetParent();
		Info["child_count"] = Sequence ? Sequence->GetChildren().Num() : 0;
		Info["tag_valid"] = Sequence && Sequence->GetSequenceTag().IsValid();
		Info["tag_name"] = Sequence ? TCHAR_TO_UTF8(*Sequence->GetSequenceTag().ToName().ToString()) : "";
		Info["mark_count"] = Sequence ? Sequence->GetMarks().Num() : 0;
		Info["preview_mark_label"] = Sequence ? TCHAR_TO_UTF8(*GetPreviewMarkLabel(Sequence)) : "";
		Info["has_preview_mark"] = Sequence && Sequence->GetPreviewMark();

		sol::table Marks = Lua.create_table();
		if (Sequence)
		{
			int32 LuaMarkIndex = 0;
			for (const FAvaMark& Mark : Sequence->GetMarks())
			{
				Marks[++LuaMarkIndex] = BuildMarkInfo(Lua, Sequence, Mark);
			}
		}
		Info["marks"] = Marks;
		return Info;
	}

	const FAvaSequencePlayParams* GetScheduledPlaySettings(UObject* PlaybackObject)
	{
		if (!PlaybackObject)
		{
			return nullptr;
		}
		FStructProperty* SettingsProperty = FindFProperty<FStructProperty>(PlaybackObject->GetClass(), TEXT("ScheduledPlaySettings"));
		if (!SettingsProperty || SettingsProperty->Struct != FAvaSequencePlayParams::StaticStruct())
		{
			return nullptr;
		}
		return SettingsProperty->ContainerPtrToValuePtr<FAvaSequencePlayParams>(PlaybackObject);
	}

	bool ConfigureScheduledPlayback(UObject* PlaybackObject, const TArray<FName>& Labels, const FAvaSequencePlayParams& Params)
	{
		if (!PlaybackObject)
		{
			return false;
		}

		bool bChanged = false;
		if (FArrayProperty* NamesProperty = FindFProperty<FArrayProperty>(PlaybackObject->GetClass(), TEXT("ScheduledSequenceNames")))
		{
			if (FNameProperty* NameProperty = CastField<FNameProperty>(NamesProperty->Inner))
			{
				FScriptArrayHelper NamesHelper(NamesProperty, NamesProperty->ContainerPtrToValuePtr<void>(PlaybackObject));
				NamesHelper.EmptyValues();
				for (const FName& Label : Labels)
				{
					if (Label.IsNone())
					{
						continue;
					}
					const int32 Index = NamesHelper.AddValue();
					NameProperty->SetPropertyValue(NamesHelper.GetRawPtr(Index), Label);
					bChanged = true;
				}
			}
		}

		if (FStructProperty* SettingsProperty = FindFProperty<FStructProperty>(PlaybackObject->GetClass(), TEXT("ScheduledPlaySettings")))
		{
			if (SettingsProperty->Struct == FAvaSequencePlayParams::StaticStruct())
			{
				SettingsProperty->CopyCompleteValue(SettingsProperty->ContainerPtrToValuePtr<void>(PlaybackObject), &Params);
				bChanged = true;
			}
		}

		if (bChanged)
		{
			PlaybackObject->Modify();
		}
		return bChanged;
	}

	sol::table BuildPlaybackInfo(sol::state_view Lua, AAvaScene* Scene)
	{
		sol::table Info = Lua.create_table();
		IAvaSequenceProvider* Provider = GetProvider(Scene);
		IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
		UObject* PlaybackUObject = PlaybackObject ? PlaybackObject->ToUObject() : nullptr;
		Info["valid"] = PlaybackObject != nullptr;
		Info["object_class"] = PlaybackUObject ? TCHAR_TO_UTF8(*PlaybackUObject->GetClass()->GetName()) : "";
		Info["object_name"] = PlaybackUObject ? TCHAR_TO_UTF8(*PlaybackUObject->GetName()) : "";
		Info["object_path"] = PlaybackUObject ? TCHAR_TO_UTF8(*PlaybackUObject->GetPathName()) : "";
		Info["has_active_players"] = PlaybackObject ? PlaybackObject->HasActiveSequencePlayers() : false;

		sol::table Scheduled = Lua.create_table();
		if (PlaybackUObject)
		{
			if (FArrayProperty* NamesProperty = FindFProperty<FArrayProperty>(PlaybackUObject->GetClass(), TEXT("ScheduledSequenceNames")))
			{
				if (FNameProperty* NameProperty = CastField<FNameProperty>(NamesProperty->Inner))
				{
					FScriptArrayHelper NamesHelper(NamesProperty, NamesProperty->ContainerPtrToValuePtr<void>(PlaybackUObject));
					for (int32 Index = 0; Index < NamesHelper.Num(); ++Index)
					{
						Scheduled[Index + 1] = TCHAR_TO_UTF8(*NameProperty->GetPropertyValue(NamesHelper.GetRawPtr(Index)).ToString());
					}
				}
			}
		}
		Info["scheduled_count"] = static_cast<int32>(Scheduled.size());
		Info["scheduled_labels"] = Scheduled;
		if (const FAvaSequencePlayParams* ScheduledSettings = GetScheduledPlaySettings(PlaybackUObject))
		{
			Info["scheduled_settings"] = BuildPlayParamsInfo(Lua, *ScheduledSettings);
		}

		sol::table Players = Lua.create_table();
		if (PlaybackObject)
		{
			int32 LuaIndex = 0;
			for (UAvaSequencePlayer* Player : PlaybackObject->GetAllSequencePlayers())
			{
				if (Player)
				{
					Players[++LuaIndex] = BuildPlayerInfo(Lua, Provider, Player);
				}
			}
			Info["active_count"] = LuaIndex;
		}
		else
		{
			Info["active_count"] = 0;
		}
		Info["players"] = Players;
		return Info;
	}

	sol::table BuildInfo(sol::state_view Lua, AAvaScene* Scene)
	{
		sol::table Info = Lua.create_table();
		IAvaSequenceProvider* Provider = GetProvider(Scene);
		Info["has_scene"] = Scene != nullptr;
		Info["scene_class"] = Scene ? TCHAR_TO_UTF8(*Scene->GetClass()->GetName()) : "";
		Info["scene_name"] = Scene ? TCHAR_TO_UTF8(*Scene->GetName()) : "";
		Info["provider_debug_name"] = Provider ? TCHAR_TO_UTF8(*Provider->GetSequenceProviderDebugName().ToString()) : "";
		Info["sequence_count"] = Provider ? Provider->GetSequences().Num() : 0;
		Info["root_count"] = Provider ? Provider->GetRootSequences().Num() : 0;
		Info["default_index"] = Provider ? FindSequenceIndex(Provider, Provider->GetDefaultSequence()) : 0;

		sol::table Sequences = Lua.create_table();
		if (Provider)
		{
			int32 LuaIndex = 0;
			for (const TObjectPtr<UAvaSequence>& Sequence : Provider->GetSequences())
			{
				Sequences[++LuaIndex] = BuildSequenceInfo(Lua, Provider, Sequence.Get());
			}
		}
		Info["sequences"] = Sequences;

		sol::table Roots = Lua.create_table();
		if (Provider)
		{
			int32 LuaIndex = 0;
			for (const TWeakObjectPtr<UAvaSequence>& RootWeak : Provider->GetRootSequences())
			{
				if (UAvaSequence* Root = RootWeak.Get())
				{
					Roots[++LuaIndex] = FindSequenceIndex(Provider, Root);
				}
			}
		}
		Info["roots"] = Roots;
		return Info;
	}

	void BindAvalancheSequenceLua(sol::state& Lua, FLuaSessionData&)
	{
		Lua.set_function("ava_sequences", [](sol::this_state S) -> sol::object
		{
			sol::state_view State(S);
			sol::table Handle = State.create_table();

				Handle.set_function("ensure_scene", [](sol::this_state InnerS, sol::table) -> sol::object
			{
				AAvaScene* Scene = EnsureScene();
				if (!Scene)
				{
					return sol::lua_nil;
				}
				return sol::make_object(InnerS, BuildInfo(sol::state_view(InnerS), Scene));
			});

				Handle.set_function("info", [](sol::this_state InnerS, sol::table) -> sol::object
				{
					AAvaScene* Scene = EnsureScene();
					if (!Scene)
					{
						return sol::lua_nil;
				}
					return sol::make_object(InnerS, BuildInfo(sol::state_view(InnerS), Scene));
				});

				Handle.set_function("playback_info", [](sol::this_state InnerS, sol::table) -> sol::object
				{
					AAvaScene* Scene = EnsureScene();
					if (!Scene)
					{
						return sol::lua_nil;
					}
					return sol::make_object(InnerS, BuildPlaybackInfo(sol::state_view(InnerS), Scene));
				});

				Handle.set_function("configure_playback", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
				{
					AAvaScene* Scene = EnsureScene();
					IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
					UObject* PlaybackUObject = PlaybackObject ? PlaybackObject->ToUObject() : nullptr;
					if (!PlaybackUObject)
					{
						return sol::lua_nil;
					}

					TArray<FName> Labels;
					if (sol::optional<sol::table> ScheduledLabels = Options.get<sol::optional<sol::table>>("scheduled_labels"))
					{
						for (const auto& Entry : *ScheduledLabels)
						{
							const sol::optional<std::string> LabelValue = Entry.second.as<sol::optional<std::string>>();
							if (LabelValue && !LabelValue->empty())
							{
								Labels.Add(FName(UTF8_TO_TCHAR(LabelValue->c_str())));
							}
						}
					}

					const FAvaSequencePlayParams Params = BuildPlayParams(Options.get<sol::optional<sol::table>>("settings"));
					if (!ConfigureScheduledPlayback(PlaybackUObject, Labels, Params))
					{
						return sol::lua_nil;
					}
					return sol::make_object(InnerS, BuildPlaybackInfo(sol::state_view(InnerS), Scene));
				});

					Handle.set_function("list_presets", [](sol::this_state InnerS, sol::table) -> sol::object
					{
						sol::state_view State(InnerS);
						sol::table Result = State.create_table();

						const UAvaSequencerSettings* Settings = GetDefault<UAvaSequencerSettings>();
						if (!Settings)
						{
							return sol::make_object(InnerS, Result);
						}

						int32 LuaIndex = 0;
						auto AddPresetRow = [&State, &Result, &LuaIndex](const FAvaSequencePreset& Preset, const TCHAR* Source)
						{
							sol::table Row = State.create_table();
							Row["name"] = TCHAR_TO_UTF8(*Preset.PresetName.ToString());
							Row["label"] = TCHAR_TO_UTF8(*Preset.SequenceLabel.ToString());
							Row["source"] = TCHAR_TO_UTF8(Source);
							Row["enable_label"] = Preset.bEnableLabel;
							Row["enable_tag"] = Preset.bEnableTag;
							Row["enable_end_time"] = Preset.bEnableEndTime;
							Row["end_time"] = Preset.EndTime;
							Row["mark_count"] = Preset.Marks.Num();
							Result[++LuaIndex] = Row;
						};

						for (const FAvaSequencePreset& Preset : Settings->GetDefaultSequencePresets())
						{
							AddPresetRow(Preset, TEXT("default"));
						}
						for (const FAvaSequencePreset& Preset : Settings->GetCustomSequencePresets())
						{
							AddPresetRow(Preset, TEXT("custom"));
						}

						return sol::make_object(InnerS, Result);
					});

					Handle.set_function("add_sequences_from_preset_group", [](sol::this_state InnerS, sol::table, const std::string& GroupNameValue) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						UObject* Outer = Provider ? Provider->ToUObject() : nullptr;
						const UAvaSequencerSettings* Settings = GetDefault<UAvaSequencerSettings>();
						if (!Provider || !Outer || !Settings || GroupNameValue.empty())
						{
							return sol::lua_nil;
						}

						const FName GroupName(UTF8_TO_TCHAR(GroupNameValue.c_str()));
						TArray<const FAvaSequencePreset*> Presets = Settings->GatherPresetsFromGroup(GroupName);
						if (Presets.IsEmpty())
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "AddSequencesFromPresetGroup", "NeoStack: Add Motion Design Sequences from Preset Group"));
						Outer->Modify();

						TArray<UAvaSequence*> NewSequences;
						NewSequences.Reserve(Presets.Num());
						for (const FAvaSequencePreset* Preset : Presets)
						{
							if (!Preset)
							{
								continue;
							}

							UAvaSequence* Sequence = NewObject<UAvaSequence>(Outer, NAME_None, RF_Transactional);
							if (!Sequence)
							{
								continue;
							}
							InitializeMovieScene(Sequence);
							Preset->ApplyPreset(Sequence);
							NewSequences.Add(Sequence);
						}

						const uint32 AddedCount = Provider->AddSequences(NewSequences);
						if (AddedCount == 0)
						{
							Tx.Cancel();
							return sol::lua_nil;
						}
						Provider->RebuildSequenceTree();

						sol::state_view State(InnerS);
						sol::table Result = State.create_table();
						Result["group_name"] = GroupNameValue;
						Result["added_count"] = static_cast<int32>(AddedCount);
						sol::table Sequences = State.create_table();
						int32 LuaIndex = 0;
						for (UAvaSequence* Sequence : NewSequences)
						{
							if (Sequence)
							{
								Sequences[++LuaIndex] = BuildSequenceInfo(State, Provider, Sequence);
							}
						}
						Result["sequence_count"] = LuaIndex;
						Result["sequences"] = Sequences;
						return sol::make_object(InnerS, Result);
					});

					Handle.set_function("play_sequence", [](sol::this_state InnerS, sol::table, int32 LuaIndex, sol::optional<sol::table> Options) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
						UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
						if (!PlaybackObject || !Sequence)
						{
							return sol::lua_nil;
						}
						UAvaSequencePlayer* Player = PlaybackObject->PlaySequence(Sequence, BuildPlayParams(Options));
						if (!Player)
						{
							return sol::lua_nil;
						}
						return sol::make_object(InnerS, BuildPlayerInfo(sol::state_view(InnerS), Provider, Player));
					});

					Handle.set_function("play_by_label", [](sol::this_state InnerS, sol::table, const std::string& LabelValue, sol::optional<sol::table> Options) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
						if (!PlaybackObject || LabelValue.empty())
						{
							return sol::lua_nil;
						}
						TArray<UAvaSequencePlayer*> Players = PlaybackObject->PlaySequencesByLabel(FName(UTF8_TO_TCHAR(LabelValue.c_str())), BuildPlayParams(Options));
						sol::state_view State(InnerS);
						sol::table Result = State.create_table();
						int32 LuaPlayerIndex = 0;
						for (UAvaSequencePlayer* Player : Players)
						{
							if (Player)
							{
								Result[++LuaPlayerIndex] = BuildPlayerInfo(State, Provider, Player);
							}
						}
						return sol::make_object(InnerS, Result);
					});

					Handle.set_function("play_by_tag", [](sol::this_state InnerS, sol::table, sol::table Options) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
						const std::string CollectionPathValue = Options.get_or<std::string>("collection", "");
						const std::string TagNameValue = Options.get_or<std::string>("name", "");
						if (!PlaybackObject || CollectionPathValue.empty() || TagNameValue.empty())
						{
							return sol::lua_nil;
						}
						UAvaTagCollection* Collection = LoadTagCollection(UTF8_TO_TCHAR(CollectionPathValue.c_str()));
						const FAvaTagHandle Handle = FindTagHandle(Collection, FName(UTF8_TO_TCHAR(TagNameValue.c_str())));
						if (!Handle.IsValid())
						{
							return sol::lua_nil;
						}
						const sol::optional<bool> bExactOption = Options.get<sol::optional<bool>>("exact");
						const bool bExact = bExactOption ? *bExactOption : true;
						TArray<UAvaSequencePlayer*> Players = PlaybackObject->PlaySequencesByTag(Handle, bExact, BuildPlayParams(Options.get<sol::optional<sol::table>>("settings")));
						sol::state_view State(InnerS);
						sol::table Result = State.create_table();
						int32 LuaPlayerIndex = 0;
						for (UAvaSequencePlayer* Player : Players)
						{
							if (Player)
							{
								Result[++LuaPlayerIndex] = BuildPlayerInfo(State, Provider, Player);
							}
						}
						return sol::make_object(InnerS, Result);
					});

					Handle.set_function("play_scheduled", [](sol::this_state InnerS, sol::table) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
						if (!PlaybackObject)
						{
							return sol::lua_nil;
						}
						TArray<UAvaSequencePlayer*> Players = PlaybackObject->PlayScheduledSequences();
						sol::state_view State(InnerS);
						sol::table Result = State.create_table();
						int32 LuaPlayerIndex = 0;
						for (UAvaSequencePlayer* Player : Players)
						{
							if (Player)
							{
								Result[++LuaPlayerIndex] = BuildPlayerInfo(State, Provider, Player);
							}
						}
						return sol::make_object(InnerS, Result);
					});

					Handle.set_function("pause_sequence", [](sol::this_state InnerS, sol::table, int32 LuaIndex) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
						UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
						if (!PlaybackObject || !Sequence)
						{
							return sol::lua_nil;
						}
						PlaybackObject->PauseSequence(Sequence);
						UAvaSequencePlayer* Player = PlaybackObject->GetSequencePlayer(Sequence);
						return Player ? sol::make_object(InnerS, BuildPlayerInfo(sol::state_view(InnerS), Provider, Player)) : sol::lua_nil;
					});

					Handle.set_function("continue_sequence", [](sol::this_state InnerS, sol::table, int32 LuaIndex) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
						UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
						if (!PlaybackObject || !Sequence)
						{
							return sol::lua_nil;
						}
						UAvaSequencePlayer* Player = PlaybackObject->ContinueSequence(Sequence);
						return Player ? sol::make_object(InnerS, BuildPlayerInfo(sol::state_view(InnerS), Provider, Player)) : sol::lua_nil;
					});

					Handle.set_function("stop_sequence", [](sol::this_state InnerS, sol::table, int32 LuaIndex) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
						UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
						if (!PlaybackObject || !Sequence)
						{
							return sol::lua_nil;
						}
						PlaybackObject->StopSequence(Sequence);
						return sol::make_object(InnerS, BuildPlaybackInfo(sol::state_view(InnerS), Scene));
					});

					Handle.set_function("cleanup_playback", [](sol::this_state InnerS, sol::table) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
						if (!PlaybackObject)
						{
							return sol::lua_nil;
						}
						PlaybackObject->CleanupPlayers();
						return sol::make_object(InnerS, BuildPlaybackInfo(sol::state_view(InnerS), Scene));
					});

					Handle.set_function("spawn_sequence_player", [](sol::this_state InnerS, sol::table, int32 LuaIndex, sol::optional<sol::table> Options) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						IAvaSequencePlaybackObject* PlaybackObject = Scene ? Scene->GetPlaybackObject() : nullptr;
						UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
						UWorld* World = Scene ? Scene->GetWorld() : GetEditorWorld();
						if (!GEditor || !PlaybackObject || !Sequence || !World)
						{
							return sol::lua_nil;
						}

						UActorFactory* ActorFactory = GEditor->FindActorFactoryForActorClass(AAvaSequenceActor::StaticClass());
						if (!ActorFactory)
						{
							return sol::lua_nil;
						}

						FVector Location = FVector::ZeroVector;
						if (Options)
						{
							Location.X = Options->get_or("x", 0.0);
							Location.Y = Options->get_or("y", 0.0);
							Location.Z = Options->get_or("z", 0.0);
						}
						const FTransform Transform(Location);
						FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "SpawnSequencePlayer", "NeoStack: Spawn Motion Design Sequence Player"));
						AAvaSequenceActor* Actor = Cast<AAvaSequenceActor>(GEditor->UseActorFactory(ActorFactory, FAssetData(Sequence), &Transform));
						if (!Actor)
						{
							return sol::lua_nil;
						}
						return sol::make_object(InnerS, BuildSequenceActorInfo(sol::state_view(InnerS), Provider, Actor));
					});

					Handle.set_function("list_sequence_players", [](sol::this_state InnerS, sol::table, sol::optional<std::string> LabelValue) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						UWorld* World = Scene ? Scene->GetWorld() : GetEditorWorld();
						const FString Label = LabelValue ? FString(UTF8_TO_TCHAR(LabelValue->c_str())) : FString();
						return sol::make_object(InnerS, BuildSequenceActorList(sol::state_view(InnerS), Provider, World, Label));
					});

					Handle.set_function("delete_sequence_players", [](sol::this_state InnerS, sol::table, sol::optional<std::string> LabelValue) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						UWorld* World = Scene ? Scene->GetWorld() : GetEditorWorld();
						const FString Label = LabelValue ? FString(UTF8_TO_TCHAR(LabelValue->c_str())) : FString();
						int32 DeletedCount = 0;
						for (AAvaSequenceActor* Actor : FindSequenceActorsForLabel(World, Label))
						{
							if (Actor && World)
							{
								Actor->Modify();
								World->DestroyActor(Actor);
								++DeletedCount;
							}
						}
						return sol::make_object(InnerS, DeletedCount);
					});

					Handle.set_function("export_sequence", [](sol::this_state InnerS, sol::table, int32 LuaIndex, sol::optional<sol::table> Options) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
						if (!Sequence)
						{
							return sol::lua_nil;
						}

						UPackage* SourcePackage = Sequence->GetPackage();
						if (!SourcePackage)
						{
							return sol::lua_nil;
						}

						FString DestName = Sequence->GetName() + TEXT("_Exported");
						FString DestPath = FPackageName::GetLongPackagePath(SourcePackage->GetName());
						if (Options)
						{
							const std::string OptName = Options->get_or<std::string>("name", "");
							if (!OptName.empty())
							{
								DestName = UTF8_TO_TCHAR(OptName.c_str());
							}

							const std::string OptPath = Options->get_or<std::string>("path", "");
							if (!OptPath.empty())
							{
								DestPath = UTF8_TO_TCHAR(OptPath.c_str());
							}
						}

						if (DestName.IsEmpty() || DestPath.IsEmpty())
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "ExportSequence", "NeoStack: Export Motion Design Sequence"));
						IAssetTools& AssetTools = FAssetToolsModule::GetModule().Get();
						UObject* ExportedObject = AssetTools.DuplicateAsset(DestName, DestPath, Sequence);
						UAvaSequence* ExportedSequence = Cast<UAvaSequence>(ExportedObject);
						if (!ExportedSequence)
						{
							return sol::lua_nil;
						}
						return sol::make_object(InnerS, BuildExportedSequenceInfo(sol::state_view(InnerS), ExportedSequence));
					});

					Handle.set_function("inspect_exported_sequence", [](sol::this_state InnerS, sol::table, const std::string& PathValue) -> sol::object
					{
						const FString Path = NormalizeAssetObjectPath(UTF8_TO_TCHAR(PathValue.c_str()));
						UAvaSequence* Sequence = Cast<UAvaSequence>(StaticLoadObject(UAvaSequence::StaticClass(), nullptr, *Path));
						if (!Sequence)
						{
							return sol::lua_nil;
						}
						return sol::make_object(InnerS, BuildExportedSequenceInfo(sol::state_view(InnerS), Sequence));
					});

					Handle.set_function("configure_tag_collection", [](sol::this_state InnerS, sol::table, const std::string& CollectionPathValue, sol::table Options) -> sol::object
					{
						UAvaTagCollection* Collection = LoadTagCollection(UTF8_TO_TCHAR(CollectionPathValue.c_str()));
						if (!Collection || !ConfigureTagCollection(Collection, Options))
						{
							return sol::lua_nil;
						}
						return sol::make_object(InnerS, BuildTagCollectionInfo(sol::state_view(InnerS), Collection));
					});

					Handle.set_function("inspect_tag_collection", [](sol::this_state InnerS, sol::table, const std::string& CollectionPathValue) -> sol::object
					{
						UAvaTagCollection* Collection = LoadTagCollection(UTF8_TO_TCHAR(CollectionPathValue.c_str()));
						if (!Collection)
						{
							return sol::lua_nil;
						}
						return sol::make_object(InnerS, BuildTagCollectionInfo(sol::state_view(InnerS), Collection));
					});

				Handle.set_function("add_sequence", [](sol::this_state InnerS, sol::table, sol::optional<sol::table> Opts) -> sol::object
			{
				AAvaScene* Scene = EnsureScene();
				IAvaSequenceProvider* Provider = GetProvider(Scene);
				if (!Scene || !Provider)
				{
					return sol::lua_nil;
				}

				UObject* Outer = Provider->ToUObject();
				if (!Outer)
				{
					return sol::lua_nil;
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "AddSequence", "NeoStack: Add Motion Design Sequence"));
				Outer->Modify();

				UAvaSequence* Sequence = NewObject<UAvaSequence>(Outer, NAME_None, RF_Transactional);
				if (!Sequence)
				{
					return sol::lua_nil;
				}
				InitializeMovieScene(Sequence);

				const std::string Label = Opts ? Opts->get_or<std::string>("label", "") : "";
				if (!Label.empty())
				{
					Sequence->SetLabel(FName(UTF8_TO_TCHAR(Label.c_str())));
				}

				Provider->AddSequence(Sequence);

				const int32 ParentIndex = Opts ? Opts->get_or("parent_index", 0) : 0;
				if (UAvaSequence* Parent = GetSequence(Provider, ParentIndex))
				{
					Parent->Modify();
					Parent->AddChild(Sequence);
				}

					Provider->RebuildSequenceTree();
					MarkScenePackageDirty(Scene);
					return sol::make_object(InnerS, BuildSequenceInfo(sol::state_view(InnerS), Provider, Sequence));
				});

				Handle.set_function("add_mark", [](sol::this_state InnerS, sol::table, int32 LuaIndex, sol::table Options) -> sol::object
				{
					AAvaScene* Scene = EnsureScene();
					IAvaSequenceProvider* Provider = GetProvider(Scene);
					UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
					UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
					const std::string LabelValue = Options.get_or<std::string>("label", "");
					if (!Sequence || !MovieScene || LabelValue.empty())
					{
						return sol::lua_nil;
					}

					const FString Label = UTF8_TO_TCHAR(LabelValue.c_str());
					const int32 FrameNumber = Options.get_or("frame", Options.get_or("frame_number", 0));

					FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "AddMark", "NeoStack: Add Motion Design Sequence Mark"));
					Sequence->Modify();
					MovieScene->Modify();

					int32 MarkedFrameIndex = MovieScene->FindMarkedFrameByLabel(Label);
					if (MarkedFrameIndex == INDEX_NONE)
					{
						FMovieSceneMarkedFrame MarkedFrame;
						MarkedFrame.Label = Label;
						MarkedFrame.FrameNumber = FFrameNumber(FrameNumber);
						MarkedFrameIndex = MovieScene->AddMarkedFrame(MarkedFrame);
					}
					else
					{
						MovieScene->SetMarkedFrame(MarkedFrameIndex, FFrameNumber(FrameNumber));
						MovieScene->SortMarkedFrames();
					}

					Sequence->UpdateMarkList();
					FAvaMark& Mark = Sequence->FindOrAddMark(Label);
					ApplyMarkOptions(Mark, Options);

					if (Options.get_or("preview", false))
					{
						SetPreviewMarkLabel(Sequence, Label);
					}

					return sol::make_object(InnerS, BuildMarkInfo(sol::state_view(InnerS), Sequence, Mark));
				});

				Handle.set_function("configure_mark", [](sol::this_state InnerS, sol::table, int32 LuaIndex, sol::table Options) -> sol::object
				{
					AAvaScene* Scene = EnsureScene();
					IAvaSequenceProvider* Provider = GetProvider(Scene);
					UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
					UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
					const std::string LabelValue = Options.get_or<std::string>("label", "");
					if (!Sequence || !MovieScene || LabelValue.empty())
					{
						return sol::lua_nil;
					}

					const FString Label = UTF8_TO_TCHAR(LabelValue.c_str());
					FAvaMark ExistingMark;
					if (!Sequence->GetMark(Label, ExistingMark))
					{
						return sol::lua_nil;
					}

					FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "ConfigureMark", "NeoStack: Configure Motion Design Sequence Mark"));
					Sequence->Modify();
					MovieScene->Modify();

					if (sol::optional<int32> Frame = Options.get<sol::optional<int32>>("frame"))
					{
						if (const int32 MarkedFrameIndex = MovieScene->FindMarkedFrameByLabel(Label); MarkedFrameIndex != INDEX_NONE)
						{
							MovieScene->SetMarkedFrame(MarkedFrameIndex, FFrameNumber(*Frame));
							MovieScene->SortMarkedFrames();
						}
					}
					else if (sol::optional<int32> FrameNumberOption = Options.get<sol::optional<int32>>("frame_number"))
					{
						if (const int32 MarkedFrameIndex = MovieScene->FindMarkedFrameByLabel(Label); MarkedFrameIndex != INDEX_NONE)
						{
							MovieScene->SetMarkedFrame(MarkedFrameIndex, FFrameNumber(*FrameNumberOption));
							MovieScene->SortMarkedFrames();
						}
					}

					Sequence->UpdateMarkList();
					FAvaMark& Mark = Sequence->FindOrAddMark(Label);
					ApplyMarkOptions(Mark, Options);

					if (sol::optional<bool> bPreview = Options.get<sol::optional<bool>>("preview"))
					{
						SetPreviewMarkLabel(Sequence, *bPreview ? Label : FString());
					}

					return sol::make_object(InnerS, BuildMarkInfo(sol::state_view(InnerS), Sequence, Mark));
				});

					Handle.set_function("remove_mark", [](sol::this_state InnerS, sol::table, int32 LuaIndex, const std::string& LabelValue) -> sol::object
					{
					AAvaScene* Scene = EnsureScene();
					IAvaSequenceProvider* Provider = GetProvider(Scene);
					UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
					UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
					if (!Sequence || !MovieScene || LabelValue.empty())
					{
						return sol::make_object(InnerS, false);
					}

					const FString Label = UTF8_TO_TCHAR(LabelValue.c_str());
					const int32 MarkedFrameIndex = MovieScene->FindMarkedFrameByLabel(Label);
					if (MarkedFrameIndex == INDEX_NONE)
					{
						return sol::make_object(InnerS, false);
					}

					FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "RemoveMark", "NeoStack: Remove Motion Design Sequence Mark"));
					Sequence->Modify();
					MovieScene->Modify();
					MovieScene->DeleteMarkedFrame(MarkedFrameIndex);
					Sequence->UpdateMarkList();
					if (GetPreviewMarkLabel(Sequence) == Label)
					{
						SetPreviewMarkLabel(Sequence, FString());
					}
						return sol::make_object(InnerS, true);
					});

					Handle.set_function("apply_preset", [](sol::this_state InnerS, sol::table, int32 LuaIndex, const std::string& PresetNameValue) -> sol::object
					{
						AAvaScene* Scene = EnsureScene();
						IAvaSequenceProvider* Provider = GetProvider(Scene);
						UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
						if (!Sequence || PresetNameValue.empty())
						{
							return sol::lua_nil;
						}

						const UAvaSequencerSettings* Settings = GetDefault<UAvaSequencerSettings>();
						if (!Settings)
						{
							return sol::lua_nil;
						}

						const FName PresetName(UTF8_TO_TCHAR(PresetNameValue.c_str()));
						const FAvaSequencePreset* Preset = nullptr;
						for (const FAvaSequencePreset& DefaultPreset : Settings->GetDefaultSequencePresets())
						{
							if (DefaultPreset.PresetName == PresetName)
							{
								Preset = &DefaultPreset;
								break;
							}
						}
						if (!Preset)
						{
							for (const FAvaSequencePreset& CustomPreset : Settings->GetCustomSequencePresets())
							{
								if (CustomPreset.PresetName == PresetName)
								{
									Preset = &CustomPreset;
									break;
								}
							}
						}
						if (!Preset)
						{
							return sol::lua_nil;
						}

						FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "ApplyPreset", "NeoStack: Apply Motion Design Sequence Preset"));
						Preset->ApplyPreset(Sequence);
						if (Provider)
						{
							Provider->RebuildSequenceTree();
						}

						return sol::make_object(InnerS, BuildSequenceInfo(sol::state_view(InnerS), Provider, Sequence));
					});

				Handle.set_function("configure_sequence", [](sol::this_state InnerS, sol::table, int32 LuaIndex, sol::table Options) -> sol::object
			{
				AAvaScene* Scene = EnsureScene();
				IAvaSequenceProvider* Provider = GetProvider(Scene);
				UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
				if (!Sequence)
				{
					return sol::lua_nil;
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "ConfigureSequence", "NeoStack: Configure Motion Design Sequence"));
				Sequence->Modify();

				const std::string Label = Options.get_or<std::string>("label", "");
				if (!Label.empty())
				{
					Sequence->SetLabel(FName(UTF8_TO_TCHAR(Label.c_str())));
				}

				if (sol::optional<sol::table> TagOptions = Options.get<sol::optional<sol::table>>("tag"))
				{
					const std::string CollectionPathValue = TagOptions->get_or<std::string>("collection", "");
					const std::string NameValue = TagOptions->get_or<std::string>("name", "");
					UAvaTagCollection* Collection = LoadTagCollection(UTF8_TO_TCHAR(CollectionPathValue.c_str()));
					const FAvaTagHandle TagHandle = FindTagHandle(Collection, FName(UTF8_TO_TCHAR(NameValue.c_str())));
					if (TagHandle.IsValid())
					{
						Sequence->SetSequenceTag(TagHandle);
					}
				}
				else if (sol::optional<bool> bClearTag = Options.get<sol::optional<bool>>("clear_tag"); bClearTag && *bClearTag)
				{
					Sequence->SetSequenceTag(FAvaTagHandle());
				}

				if (sol::optional<int32> ParentIndex = Options.get<sol::optional<int32>>("parent_index"))
				{
					UAvaSequence* OldParent = Sequence->GetParent();
					if (OldParent)
					{
						OldParent->Modify();
						OldParent->RemoveChild(Sequence);
					}

					if (*ParentIndex > 0)
					{
						UAvaSequence* NewParent = GetSequence(Provider, *ParentIndex);
						if (NewParent && NewParent != Sequence && !IsDescendantOf(NewParent, Sequence))
						{
							NewParent->Modify();
							NewParent->AddChild(Sequence);
						}
					}
				}

				Provider->RebuildSequenceTree();
				return sol::make_object(InnerS, BuildSequenceInfo(sol::state_view(InnerS), Provider, Sequence));
			});

				Handle.set_function("duplicate_sequence", [](sol::this_state InnerS, sol::table, int32 LuaIndex, sol::optional<sol::table> Opts) -> sol::object
			{
				AAvaScene* Scene = EnsureScene();
				IAvaSequenceProvider* Provider = GetProvider(Scene);
				UAvaSequence* Template = GetSequence(Provider, LuaIndex);
				if (!Template || !Provider)
				{
					return sol::lua_nil;
				}

				UObject* Outer = Provider->ToUObject();
				if (!Outer)
				{
					return sol::lua_nil;
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "DuplicateSequence", "NeoStack: Duplicate Motion Design Sequence"));
				Outer->Modify();

				UAvaSequence* Duplicate = DuplicateObject<UAvaSequence>(Template, Outer);
				if (!Duplicate)
				{
					return sol::lua_nil;
				}
				Duplicate->SetParent(nullptr);
				Duplicate->RemoveAllChildren();

				const std::string Label = Opts ? Opts->get_or<std::string>("label", "") : "";
				if (!Label.empty())
				{
					Duplicate->SetLabel(FName(UTF8_TO_TCHAR(Label.c_str())));
				}

				Provider->AddSequence(Duplicate);

				const int32 ParentIndex = Opts ? Opts->get_or("parent_index", 0) : 0;
				if (UAvaSequence* Parent = GetSequence(Provider, ParentIndex))
				{
					Parent->Modify();
					Parent->AddChild(Duplicate);
				}

				Provider->RebuildSequenceTree();
				return sol::make_object(InnerS, BuildSequenceInfo(sol::state_view(InnerS), Provider, Duplicate));
			});

				Handle.set_function("set_default", [](sol::this_state InnerS, sol::table, int32 LuaIndex) -> sol::object
			{
				AAvaScene* Scene = EnsureScene();
				IAvaSequenceProvider* Provider = GetProvider(Scene);
				UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
				if (!Sequence)
				{
					return sol::make_object(InnerS, false);
				}
				Provider->SetDefaultSequence(Sequence);
				Provider->RebuildSequenceTree();
				return sol::make_object(InnerS, true);
			});

				Handle.set_function("delete_sequence", [](sol::this_state InnerS, sol::table, int32 LuaIndex) -> sol::object
			{
				AAvaScene* Scene = EnsureScene();
				IAvaSequenceProvider* Provider = GetProvider(Scene);
				UAvaSequence* Sequence = GetSequence(Provider, LuaIndex);
				if (!Sequence || !Provider)
				{
					return sol::make_object(InnerS, false);
				}

				FScopedTransaction Tx(NSLOCTEXT("NSAI_AvalancheSequence", "DeleteSequence", "NeoStack: Delete Motion Design Sequence"));
				if (UObject* Outer = Provider->ToUObject())
				{
					Outer->Modify();
				}
				Sequence->Modify();
				Provider->RemoveSequence(Sequence);
				Sequence->OnSequenceRemoved();
				Provider->RebuildSequenceTree();
				return sol::make_object(InnerS, true);
			});

			return sol::make_object(S, Handle);
		});
	}
}

class FNSAI_AvalancheSequenceModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildAvalancheSequenceDescriptor());
		Registrar.RegisterLuaBinding(
			AvalancheSequenceExtensionId,
			TEXT("AvalancheSequence"),
			{
				{ TEXT("ava_sequences()"), TEXT("Open Motion Design sequence provider handle for the editor world"), TEXT("handle") },
					{ TEXT("handle:ensure_scene()"), TEXT("Find/create the Motion Design scene provider and inspect embedded sequences"), TEXT("info table or nil") },
					{ TEXT("handle:list_presets()"), TEXT("List default Motion Design sequence presets from editor settings"), TEXT("array") },
					{ TEXT("handle:configure_tag_collection(path, opts)"), TEXT("Author tags in a Motion Design tag collection asset"), TEXT("tag collection info or nil") },
					{ TEXT("handle:inspect_tag_collection(path)"), TEXT("Inspect tags in a Motion Design tag collection asset"), TEXT("tag collection info or nil") },
					{ TEXT("handle:add_sequence(opts?)"), TEXT("Create an embedded UAvaSequence and optionally label/reparent it"), TEXT("sequence info or nil") },
					{ TEXT("handle:configure_sequence(index, opts)"), TEXT("Relabel, tag, clear tag, or reparent an embedded Motion Design sequence"), TEXT("sequence info or nil") },
						{ TEXT("handle:duplicate_sequence(index, opts?)"), TEXT("Duplicate an embedded Motion Design sequence"), TEXT("sequence info or nil") },
						{ TEXT("handle:apply_preset(index, preset_name)"), TEXT("Apply a Motion Design sequence preset through FAvaSequencePreset"), TEXT("sequence info or nil") },
						{ TEXT("handle:add_sequences_from_preset_group(group_name)"), TEXT("Create embedded Motion Design sequences from a preset group through UAvaSequencerSettings"), TEXT("result info or nil") },
					{ TEXT("handle:playback_info()"), TEXT("Inspect Motion Design playback object and active UAvaSequencePlayer state"), TEXT("playback info or nil") },
					{ TEXT("handle:configure_playback(opts)"), TEXT("Configure scheduled sequence labels and play settings on the playback object"), TEXT("playback info or nil") },
					{ TEXT("handle:play_sequence(index, opts?)"), TEXT("Play an embedded Motion Design sequence through IAvaSequencePlaybackObject"), TEXT("player info or nil") },
					{ TEXT("handle:play_by_label(label, opts?)"), TEXT("Play embedded Motion Design sequences matching a label"), TEXT("array") },
					{ TEXT("handle:play_by_tag(opts)"), TEXT("Play embedded Motion Design sequences matching a tag handle"), TEXT("array") },
					{ TEXT("handle:play_scheduled()"), TEXT("Play the playback object's scheduled Motion Design sequences"), TEXT("array") },
					{ TEXT("handle:pause_sequence(index)"), TEXT("Pause an active Motion Design sequence player"), TEXT("player info or nil") },
					{ TEXT("handle:continue_sequence(index)"), TEXT("Continue an active Motion Design sequence player"), TEXT("player info or nil") },
					{ TEXT("handle:stop_sequence(index)"), TEXT("Stop an active Motion Design sequence player"), TEXT("playback info or nil") },
					{ TEXT("handle:cleanup_playback()"), TEXT("Clean up all active/stopped Motion Design sequence players"), TEXT("playback info or nil") },
					{ TEXT("handle:spawn_sequence_player(index, opts?)"), TEXT("Spawn an AAvaSequenceActor through the Motion Design actor factory"), TEXT("actor info or nil") },
					{ TEXT("handle:list_sequence_players(label?)"), TEXT("List spawned Motion Design sequence player actors"), TEXT("array") },
					{ TEXT("handle:delete_sequence_players(label?)"), TEXT("Delete spawned Motion Design sequence player actors"), TEXT("deleted count") },
					{ TEXT("handle:export_sequence(index, opts?)"), TEXT("Export an embedded Motion Design sequence through AssetTools duplication"), TEXT("exported sequence info or nil") },
					{ TEXT("handle:inspect_exported_sequence(path)"), TEXT("Load and inspect a standalone exported Motion Design sequence asset"), TEXT("exported sequence info or nil") },
						{ TEXT("handle:add_mark(index, opts)"), TEXT("Add or update a MovieScene marked frame and Motion Design mark settings"), TEXT("mark info or nil") },
					{ TEXT("handle:configure_mark(index, opts)"), TEXT("Configure an existing Motion Design mark and optional frame/preview state"), TEXT("mark info or nil") },
					{ TEXT("handle:remove_mark(index, label)"), TEXT("Remove a MovieScene marked frame and synchronized Motion Design mark"), TEXT("boolean") },
					{ TEXT("handle:set_default(index)"), TEXT("Set the scene provider default sequence"), TEXT("boolean") },
				{ TEXT("handle:delete_sequence(index)"), TEXT("Remove and garbage an embedded Motion Design sequence"), TEXT("boolean") },
				{ TEXT("handle:info()"), TEXT("Inspect scene provider sequence/default/root/tree state"), TEXT("info table or nil") },
			},
			FLuaBindingFunc::CreateStatic(&BindAvalancheSequenceLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(AvalancheSequenceExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_AvalancheSequenceModule, NSAI_AvalancheSequence)
