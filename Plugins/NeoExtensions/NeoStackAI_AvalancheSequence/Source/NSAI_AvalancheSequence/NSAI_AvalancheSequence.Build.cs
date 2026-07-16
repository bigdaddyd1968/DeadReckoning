using UnrealBuildTool;

public class NSAI_AvalancheSequence : ModuleRules
{
    public NSAI_AvalancheSequence(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDefinitions.Add("LUA_BUILD_AS_DLL=1");

        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "NeoStackAI",
            "UnrealEd",
            "AssetTools",
            "Avalanche",
            "AvalancheSequence",
            "AvalancheSequencer",
            "AvalancheTag",
            "LevelSequence",
            "MovieScene"
        });
    }
}
