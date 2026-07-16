using UnrealBuildTool;
using System.IO;

public class NSAI_IKRig : ModuleRules
{
    public NSAI_IKRig(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Lua and sol2 are exposed by the NeoStack AI core module as public includes.
        PrivateDefinitions.Add("LUA_BUILD_AS_DLL=1");
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "NeoStackAI",
            "UnrealEd",
            "IKRig",
            "IKRigEditor",
            "PBIK",
            "AnimGraphRuntime",
            "Slate",
            "SlateCore",
        });
    }
}
