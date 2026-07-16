using UnrealBuildTool;

public class NSAI_Text3D : ModuleRules
{
    public NSAI_Text3D(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDefinitions.Add("LUA_BUILD_AS_DLL=1");

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "NeoStackAI",
            "Text3D",
        });
    }
}
