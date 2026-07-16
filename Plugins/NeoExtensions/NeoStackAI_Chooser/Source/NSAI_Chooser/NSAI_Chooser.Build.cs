using UnrealBuildTool;
using System.IO;

public class NSAI_Chooser : ModuleRules
{
    public NSAI_Chooser(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Lua and sol2 are exposed by the NeoStack AI core module as public includes.
        PrivateDefinitions.Add("LUA_BUILD_AS_DLL=1");
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "NeoStackAI",
            "UnrealEd",
            "Chooser",
            "ProxyTable",
            "ProxyTableEditor",
            "GameplayTags",
        });
    }
}
