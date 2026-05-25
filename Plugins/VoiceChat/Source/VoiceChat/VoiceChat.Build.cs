using System;
using System.IO;
using UnrealBuildTool;
using System.Collections.Generic;

public class VoiceChat : ModuleRules
{
	public VoiceChat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {

            }
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
                "AudioMixer",
                    "AudioCaptureCore",
                    "AudioCapture",
                    "Media",
                    "MediaUtils",
                    "MediaAssets",
				"Voice",
				// ... add other public dependencies that you statically link with here ...
			}
			);
			
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
                   "AudioMixer",
                    "Voice",
                    "AudioCapture",
                     "AudioCaptureCore",
                     "Media",
                    "MediaUtils",
                    "MediaAssets",
				// ... add private dependencies that you statically link with here ...	
			}
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);

        if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows))
        {
            AddEngineThirdPartyPrivateStaticDependencies(Target, "DirectSound");
        }
        else if (Target.Platform == UnrealTargetPlatform.Mac)
        {
            PublicFrameworks.AddRange(new string[] { "CoreAudio", "AudioUnit", "AudioToolbox" });
        }
        else if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
        {
            AddEngineThirdPartyPrivateStaticDependencies(Target, "SDL2");
        }

        if (Target.Platform.IsInGroup(UnrealPlatformGroup.Windows) ||
               Target.Platform == UnrealTargetPlatform.Mac)
        {
            PrivateDependencyModuleNames.Add("AudioCaptureRtAudio");
        }

        AddEngineThirdPartyPrivateStaticDependencies(Target, "libOpus");
    }
}
