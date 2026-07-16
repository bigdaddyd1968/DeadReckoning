using UnrealBuildTool;

public class NSAI_AvalancheSceneState : ModuleRules
{
    public NSAI_AvalancheSceneState(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDefinitions.Add("LUA_BUILD_AS_DLL=1");

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "NeoStackAI",
            "UnrealEd",
            "Avalanche",
            "AvalancheSceneState",
            "AvalancheSceneStateBlueprint",
            "BlueprintGraph",
            "PropertyBindingUtils",
            "SceneState",
            "SceneStateBinding",
            "SceneStateBlueprint",
            "SceneStateBlueprintEditor",
            "SceneStateEvent",
            "SceneStateGameplay",
            "SceneStateMachineGraph",
            "SceneStateTransitionGraph",
            "SceneStateTasks"
        });
    }
}
