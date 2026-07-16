#include "Modules/ModuleManager.h"
#include "NeoStackAIModule.h"
#include "Extensions/NeoStackExtensionRegistrar.h"
#include "GameplayAbilitiesExtensionBinding.h"
#include "Engine/Blueprint.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilityBlueprint.h"
#include "GameplayEffect.h"

namespace
{
	const TCHAR* GameplayAbilitiesExtensionId = TEXT("neostack.gameplayabilities");

	FNeoStackExtensionDescriptor BuildGameplayAbilitiesDescriptor()
	{
		FNeoStackExtensionDescriptor Descriptor;
		Descriptor.ExtensionId = GameplayAbilitiesExtensionId;
		Descriptor.DisplayName = TEXT("Gameplay Abilities");
		Descriptor.ModuleName = TEXT("NSAI_GameplayAbilities");
		Descriptor.Vendor = TEXT("Betide Studio");
		Descriptor.Version = TEXT("0.1.0");
		Descriptor.Category = TEXT("integration");
		Descriptor.Description = TEXT("Author Gameplay Abilities and Effects, manage gameplay tags, and wire up cooldowns and costs.");
		Descriptor.bIsBuiltIn = false;
		Descriptor.Tags = { TEXT("lua"), TEXT("abilities"), TEXT("gameplay") };
		Descriptor.State = ENeoStackExtensionState::Active;
		return Descriptor;
	}
}

class FNSAI_GameplayAbilitiesModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
	{
		FNeoStackExtensionRegistrar& Registrar = FNeoStackExtensionRegistrar::Get();
		Registrar.RegisterExtension(BuildGameplayAbilitiesDescriptor());
		Registrar.RegisterLuaBinding(
			GameplayAbilitiesExtensionId,
			TEXT("GameplayAbility"),
			TArray<FLuaFunctionDoc>(NSAIGameplayAbilitiesExtension::GetGameplayAbilityLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIGameplayAbilitiesExtension::BindGameplayAbilityLua));
		Registrar.RegisterLuaBinding(
			GameplayAbilitiesExtensionId,
			TEXT("GameplayEffect"),
			TArray<FLuaFunctionDoc>(NSAIGameplayAbilitiesExtension::GetGameplayEffectLuaDocs()),
			FLuaBindingFunc::CreateStatic(&NSAIGameplayAbilitiesExtension::BindGameplayEffectLua));

		// GameplayAbility Blueprints use a dedicated UBlueprint subclass.
		Registrar.RegisterAssetCapability(
			GameplayAbilitiesExtensionId,
			TEXT("GameplayAbility"),
			TEXT("_enrich_gameplay_ability"),
			[](UObject* Asset) -> bool
			{
				return Asset && Asset->IsA<UGameplayAbilityBlueprint>();
			});

		// GameplayEffect Blueprints are plain UBlueprints whose GeneratedClass derives
		// from UGameplayEffect.
		Registrar.RegisterAssetCapability(
			GameplayAbilitiesExtensionId,
			TEXT("GameplayEffect"),
			TEXT("_enrich_gameplay_effect"),
			[](UObject* Asset) -> bool
			{
				UBlueprint* BP = Cast<UBlueprint>(Asset);
				return BP && BP->GeneratedClass
					&& BP->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass());
			});
	}

    virtual void ShutdownModule() override
	{
		FNeoStackExtensionRegistrar::Get().UnregisterAllForExtension(GameplayAbilitiesExtensionId);
	}
};
IMPLEMENT_MODULE(FNSAI_GameplayAbilitiesModule, NSAI_GameplayAbilities)
