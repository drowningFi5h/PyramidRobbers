#include "VoiceChat.h"

#define LOCTEXT_NAMESPACE "FVoiceChat"

DEFINE_LOG_CATEGORY(LogVoiceChatPro);

void FVoiceChatModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
}

void FVoiceChatModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FVoiceChatModule, VoiceChat)
