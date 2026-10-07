using UnrealBuildTool;
public class ShamanTarget : TargetRules
{
	public ShamanTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V2;
		ExtraModuleNames.AddRange(new string[] { "Shaman", "ShamanVoxel" });
	}
}
