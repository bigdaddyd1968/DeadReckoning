// Copyright 2025-2026 Betide Studio. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "PoseSearchExtensionBinding.h"

namespace
{
	const TCHAR* PoseSearchExtensionId = TEXT("neostack.posesearch");

	FNeoStackExtensionDescriptor BuildPoseSearchDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = PoseSearchExtensionId;
		Descriptor.DisplayName = TEXT("Pose Search");
		Descriptor.ModuleName = TEXT("NSAI_PoseSearch");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Set up motion matching — pose databases, schemas, and search configuration for animation selection.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("posesearch"), TEXT("animation") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_PoseSearchModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildPoseSearchDescriptor());
		Registrar.RegisterAssetCapability(
			PoseSearchExtensionId,
			TEXT("PoseSearchSchema"),
			TEXT("_enrich_pose_search_schema"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->GetClass()->GetName() == TEXT("PoseSearchSchema");
			});
		Registrar.RegisterAssetCapability(
			PoseSearchExtensionId,
			TEXT("PoseSearchDatabase"),
			TEXT("_enrich_pose_search_database"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->GetClass()->GetName() == TEXT("PoseSearchDatabase");
			});
		Registrar.RegisterAssetCapability(
			PoseSearchExtensionId,
			TEXT("PoseSearchNormalizationSet"),
			TEXT("_enrich_pose_search_normalization_set"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->GetClass()->GetName() == TEXT("PoseSearchNormalizationSet");
			});
		Registrar.RegisterLuaBinding(
			PoseSearchExtensionId,
			TEXT("PoseSearch"),
			TArray<FLuaFunctionDoc>(NSAIPoseSearchExtension::GetPoseSearchLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIPoseSearchExtension::BindPoseSearchLua));
	}

	virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(PoseSearchExtensionId);
	}
};

IMPLEMENT_MODULE(FNSAI_PoseSearchModule, NSAI_PoseSearch)
