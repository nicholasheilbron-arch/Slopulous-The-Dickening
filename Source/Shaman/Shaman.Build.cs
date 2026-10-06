using UnrealBuildTool;
public class Shaman : ModuleRules
{
	public Shaman(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore",
			"AIModule", "NavigationSystem", "GameplayTags", "ProceduralMeshComponent"
		});
	}
}
