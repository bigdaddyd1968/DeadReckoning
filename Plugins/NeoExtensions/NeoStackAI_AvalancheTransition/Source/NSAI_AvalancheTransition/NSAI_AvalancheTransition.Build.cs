using UnrealBuildTool;

public class NSAI_AvalancheTransition : ModuleRules
{
    public NSAI_AvalancheTransition(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDefinitions.Add("LUA_BUILD_AS_DLL=1");

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "NeoStackAI",
            "UnrealEd",
            "StateTreeModule",
            "StateTreeEditorModule",
            "AvalancheTag",
            "AvalancheTransition"
        });
    }
}
