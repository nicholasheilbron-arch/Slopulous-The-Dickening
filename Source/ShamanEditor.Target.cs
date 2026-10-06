using UnrealBuildTool;
public class ShamanEditorTarget : TargetRules
{
	public ShamanEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V2;
		ExtraModuleNames.Add("Shaman");
	}
}
