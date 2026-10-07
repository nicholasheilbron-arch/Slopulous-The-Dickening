using UnrealBuildTool;

// Adapter between SHAMAN's terrain abstraction and Voxel Plugin Free (Plugins/VoxelFree).
// This is the ONLY module that may include Voxel Plugin headers. Gameplay code lives in "Shaman" and never
// depends on this module; it is selected at runtime through UShamanTerrainSubsystem::InitializeTerrain(backend class).
public class ShamanVoxel : ModuleRules
{
	public ShamanVoxel(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Shaman" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Voxel" });
	}
}
