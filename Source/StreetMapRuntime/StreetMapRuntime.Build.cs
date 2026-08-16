using UnrealBuildTool;

public class StreetMapRuntime : ModuleRules
{
    public StreetMapRuntime(ReadOnlyTargetRules Target)
        : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new[]
            {
                "Core",
                "CoreUObject",
                "Engine"
            });

        PrivateDependencyModuleNames.AddRange(
            new[]
            {
                "NavigationSystem",
                "PhysicsCore",
                "RenderCore",
                "RHI",
                "Landscape"
            });
    }
}
