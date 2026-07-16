using UnrealBuildTool;

public class NSAI_Avalanche : ModuleRules
{
    public NSAI_Avalanche(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDefinitions.Add("LUA_BUILD_AS_DLL=1");

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "NeoStackAI",
            "AssetRegistry",
            "UnrealEd",
            "Avalanche",
            "AvalancheAttribute",
            "AvalancheCamera",
            "AvalancheComponentVisualizers",
            "AvalancheMask",
            "AvalancheMedia",
            "AvalancheOutliner",
            "AvalancheRemoteControl",
            "AvalancheSceneTree",
            "AvalancheSceneRig",
            "AvalancheSceneRigEditor",
            "AvalancheSequence",
            "AvalancheSequencer",
            "AvalancheViewport",
            "AvalancheShapes",
            "AvalancheText",
            "AvalancheTag",
            "AvalancheTransition",
            "ActorModifierCore",
            "DynamicMesh",
            "GeometryCore",
            "GeometryMask",
            "GeometryFramework",
            "InputCore",
            "LevelSequence",
            "MediaIOCore",
            "MovieScene",
            "RemoteControl",
            "RemoteControlLogic",
            "Slate",
            "StructUtils",
            "Text3D"
        });
    }
}
