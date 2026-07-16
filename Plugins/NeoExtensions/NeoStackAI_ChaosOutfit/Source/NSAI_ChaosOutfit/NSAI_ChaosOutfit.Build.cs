using UnrealBuildTool;
using System;
using System.IO;

public class NSAI_ChaosOutfit : ModuleRules
{
    public NSAI_ChaosOutfit(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        // Lua and sol2 are exposed by the NeoStack AI core module as public includes.
        PrivateDefinitions.Add("LUA_BUILD_AS_DLL=1");
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new string[] {
            "NeoStackAI",
            "UnrealEd",
            "ClothingSystemRuntimeInterface",
            "ClothingSystemRuntimeCommon",
            "ClothingSystemEditor",
        });

        // ChaosOutfitAsset moved out of Experimental in UE 5.8. Probe both
        // locations and use source/module descriptors rather than generated
        // Intermediate headers, which may not exist before a source engine build.
        string ChaosOutfitPluginDir = FindChaosOutfitPluginDir(Target);
        bool bHasChaosOutfit =
            !String.IsNullOrEmpty(ChaosOutfitPluginDir) &&
            HasPluginModuleSource(ChaosOutfitPluginDir, "ChaosOutfitAssetEngine") &&
            HasPluginModuleSource(Path.Combine(EngineDirectory, "Plugins", "ChaosClothAsset"), "ChaosClothAssetEngine");
        if (bHasChaosOutfit)
        {
            PrivateDependencyModuleNames.Add("ChaosOutfitAssetEngine");
            PrivateDependencyModuleNames.Add("ChaosClothAssetEngine");
        }
        PrivateDefinitions.Add("WITH_CHAOS_OUTFIT_ASSET=" + (bHasChaosOutfit ? "1" : "0"));
    }

    private string FindChaosOutfitPluginDir(ReadOnlyTargetRules Target)
    {
        string UE58PluginDir = Path.Combine(EngineDirectory, "Plugins", "ChaosOutfitAsset");
        string UE57PluginDir = Path.Combine(EngineDirectory, "Plugins", "Experimental", "ChaosOutfitAsset");
        string[] CandidateDirs = IsUE58OrLater(Target)
            ? new string[] { UE58PluginDir, UE57PluginDir }
            : new string[] { UE57PluginDir, UE58PluginDir };

        foreach (string CandidateDir in CandidateDirs)
        {
            if (File.Exists(Path.Combine(CandidateDir, "ChaosOutfitAsset.uplugin")))
            {
                return CandidateDir;
            }
        }

        return null;
    }

    private static bool IsUE58OrLater(ReadOnlyTargetRules Target)
    {
        return Target.Version.MajorVersion > 5 ||
            (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8);
    }

    private static bool HasPluginModuleSource(string PluginDir, string ModuleName)
    {
        string ModuleBuildFile = Path.Combine(PluginDir, "Source", ModuleName, ModuleName + ".Build.cs");
        return File.Exists(ModuleBuildFile);
    }
}
