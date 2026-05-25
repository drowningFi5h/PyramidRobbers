#pragma once

#include "CoreMinimal.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Character.h"
#include "MicrophoneSpeakComponent.h"
#include "PlayerVoiceChatActor.generated.h"

enum EVoiceChat_Mode
{
	VC_GLOBAL_NORADIO,
	VC_GLOBAL_RADIO,
	VC_PROXIMITY_NORADIO,
	VC_PROXIMITY_RADIO
};


// frames allowed 400 200 100 50 25
UENUM(BlueprintType)
enum class EOpusFramePerSec : uint8 {
	OPUS_FPS_400       UMETA(DisplayName = "OPUS_FPS_400"),
	OPUS_FPS_200       UMETA(DisplayName = "OPUS_FPS_200"),
	OPUS_FPS_100       UMETA(DisplayName = "OPUS_FPS_100"),
	OPUS_FPS_50        UMETA(DisplayName = "OPUS_FPS_50"),
	OPUS_FPS_25		 UMETA(DisplayName = "OPUS_FPS_25")
};


// global delegates for monitoring Voice Chat actors creation / deletion
DECLARE_DYNAMIC_DELEGATE_OneParam(FDelegateNewVoiceChatActor, const APlayerVoiceChatActor*, VoiceChatActor);
DECLARE_DYNAMIC_DELEGATE_OneParam(FDelegateDeleteVoiceChatActor, const APlayerVoiceChatActor*, VoiceChatActor);
// global delegates for monitoring local plalyer Voice Chat Actor creation
DECLARE_DYNAMIC_DELEGATE_OneParam(FDelegateMyVoiceChatActorReady, const APlayerVoiceChatActor*, VoiceChatActor);


UCLASS()
class VOICECHAT_API APlayerVoiceChatActor : public AActor
{
	GENERATED_BODY()

		DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerNameReceived, FString, name);
		DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlayerMicrophoneOnReceived, bool, IsMicrophoneOn);

public:
	

	static APlayerVoiceChatActor* myPlayerVoiceActor; // local player reference
	static FString selectedAudioInputDevice; // selected microphone hardware on Windows
	static float rawMicrophoneGain; // volume gain
	static float voiceChatGlobalVolume; // global voice chat volume
	static USoundClass* voiceChatSoundClass; // project sound class used for voice playback
	static bool muteAll; // mute all players
	static float tickRateUpdateActor; // mute all players
	static float thresholdSendData;
	

	// constructor
	APlayerVoiceChatActor();

	/* the root scene component*/
	UPROPERTY()
		USceneComponent *RootSceneComponent;
		
	/* the component used to speak and receive voice*/
	UPROPERTY(Transient, BlueprintReadOnly, ReplicatedUsing = RepNotifyMicComp, Category = "VoiceChatUniversal")
		UMicrophoneSpeakComponent* microphoneSpeakComponent;

	/* delegate to clean up this actor*/
	UFUNCTION(Category = "VoiceChatUniversal")
		void DelegateEndPlayOwner(AActor* act, EEndPlayReason::Type EndPlayReason);
	
	/* the owner of this actor, used for muting a player for example */
	UPROPERTY(Transient, replicated, BlueprintReadOnly, Category = "VoiceChatUniversal")
		APlayerState *ownerPlayerState;
	
	UPROPERTY(Transient, replicated, EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = "true"), Category = "VoiceChatUniversal")
		int32 idVoiceChat;

	UPROPERTY(BlueprintReadOnly, Transient, ReplicatedUsing = RepNotifyPlayerName, Category = "VoiceChatUniversal")
		FString playerName = "Player";

	UPROPERTY(Transient, ReplicatedUsing = RepNotifyIsMicrophoneOn, EditAnywhere, BlueprintReadWrite, Category = "VoiceChatUniversal")
		bool isMicrophoneOn = false;

	UPROPERTY(BlueprintReadOnly, Transient, ReplicatedUsing = RepNotifyVoiceVolume, Category = "VoiceChatUniversal")
		float voiceVolume = 5.0f;

	UPROPERTY(BlueprintReadWrite, Transient, replicated, Category = "VoiceChatUniversal")
		TArray<int32> radioChannelSubscribed;

	UPROPERTY(Transient, ReplicatedUsing = RepNotifyAttenuationAsset, EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = "true"), Category = "VoiceChatUniversal")
		FString pathToAttenuationAsset = "";

	UPROPERTY(Transient, ReplicatedUsing = RepNotifySourceEffectAsset, EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = "true"), Category = "VoiceChatUniversal")
		FString pathToSourceChainEffectAsset = "";

	UPROPERTY(Transient, BlueprintReadWrite, Meta = (ExposeOnSpawn = "true"), Category = "VoiceChatUniversal")
		bool ServerPerformAntiCheat = false;
	
	UPROPERTY(Transient, BlueprintReadWrite, Meta = (ExposeOnSpawn = "true"), Category = "VoiceChatUniversal")
		bool AntiCheatAllowUseProximity = true;

	UPROPERTY(Transient, BlueprintReadWrite, Meta = (ExposeOnSpawn = "true"), Category = "VoiceChatUniversal")
		bool AntiCheatAllowUseGlobal = true;

	UPROPERTY(Transient, BlueprintReadWrite, Meta = (ExposeOnSpawn = "true"), Category = "VoiceChatUniversal")
		float AntiCheatMaxProximityRange = 1000;

	UPROPERTY(BlueprintAssignable, Category = "VoiceChatUniversal")
		FPlayerNameReceived OnPlayerNameReceived;

	UPROPERTY(BlueprintAssignable, Category = "VoiceChatUniversal")
		FPlayerMicrophoneOnReceived OnIsMicrophoneOnReceived;

	UFUNCTION(Category = "VoiceChatUniversal")
		void RepNotifyMicComp();

	UFUNCTION(Category = "VoiceChatUniversal")
		void RepNotifyAttenuationAsset();

	UFUNCTION(Category = "VoiceChatUniversal")
		void RepNotifySourceEffectAsset();
		
	UFUNCTION(Category = "VoiceChatUniversal")
		void RepNotifyVoiceVolume();

	UFUNCTION(Category = "VoiceChatUniversal")
		void RepNotifyPlayerName();
	
	UFUNCTION(Category = "VoiceChatUniversal")
		void RepNotifyIsMicrophoneOn();

	UFUNCTION(Category = "VoiceChatUniversal")
		void muteAudio(bool isMute);

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void ServerSetAllowUseGlobal(bool _allowUseGlobal);

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void ServerAddChannel(int32 channelToAdd);

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void ServerRemoveChannel(int32 channelToRemove);

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void ServerSetAttenuation(bool enableAttenuation, FString _pathToAttenuationAsset);

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void ServerSetSourceChainEffect(bool enableSourceChainEffect, FString _pathToSourceChainEffect);
		
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void ServerSetAllowUseProximity(bool _allowUseRange);

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void ServerSetMaxProximityRange(float _maxProximityRange);

	// client set name
	UFUNCTION(Server, Reliable, Category = "VoiceChatUniversal")
		void RPCClientSetPlayerName(const FString& name);

	// client replicate its microphone status ( on / off )
	UFUNCTION(Server, Reliable, Category = "VoiceChatUniversal")
		void RPCClientSetIsMicrophoneOn(bool _isMicrophoneOn);

	// client replicate its microphone volume
	UFUNCTION(Server, Reliable, Category = "VoiceChatUniversal")
		void RPCClientSetMicrophoneVolume(float volume);

	// server update audio pos
	UFUNCTION(NetMulticast, Unreliable, Category = "VoiceChatUniversal")
		void RPCServerUpdatePosAudioComp(FVector worldPos, FRotator worldRotation);
	   
	// client ask radio/team channel add
	UFUNCTION(Server, Reliable, Category = "VoiceChatUniversal")
		void RPCClientAskAddChannel(int32 channelToAdd);

	// client ask radio/team channel remove
	UFUNCTION(Server, Reliable, Category = "VoiceChatUniversal")
		void RPCClientAskRemoveChannel(int32 channelToRemove);

	// client attenuation path
	UFUNCTION(Server, Reliable, Category = "VoiceChatUniversal")
		void RPCClientSetAttenuationPath(const FString& _attenuationPath);

	// client source chain effect path
	UFUNCTION(Server, Reliable, Category = "VoiceChatUniversal")
		void RPCClientSetSourceChainEffectPath(const FString& _pathToSourceChainEffectAsset);

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void setOverrideLocallyAttenuationPath(bool enableAttenuation, bool overrideLocally, FString _pathToAttenuationAsset);

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void setOverrideLocallySourceEffectPath(bool enableSourceEffect, bool overrideLocally, FString _pathToSourceEffectAsset);

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void setLocallyMultiplierVolume(float multiplierVolume);

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		float getLocallyMultiplierVolume();

	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		bool IsMicrophoneComponentValid();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason);
	virtual void Tick(float DeltaTime) override;
	virtual void OnRep_Owner() override;
};


/* bp library */
UCLASS()
class VOICECHAT_API UUniversalVoiceChat : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

	
public:

	// reference to local actor client side
	static APlayerVoiceChatActor *GetMyPlayerVoiceActor();
	// delegate creation / end any voice chat actor
	static FDelegateNewVoiceChatActor delegateStaticNewVoiceChatActor;
	static FDelegateDeleteVoiceChatActor delegateStaticDeleteVoiceChatActor;
	// delegate my local voice chat actor ready
	static FDelegateMyVoiceChatActorReady delegateStaticMyVoiceChatActorReady;

	// Your client local voice chat actor
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static APlayerVoiceChatActor* VoiceChatGetMyLocalPlayerVoiceChat();

	// Local Voice Chat Actor : Check if your actor is ready to speak
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool IsMyPlayerVoiceChatActorReady();

	// Local Voice Chat Actor : Customize audio settings
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatInitAudioVoiceChatQuality(int32 _sampleRate = 48000, int32 _numChannels = 1, EOpusFramePerSec _opusFramePerSec = EOpusFramePerSec::OPUS_FPS_200);
			
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatWasInitAudioVoiceChatQuality();
		
	// Local Voice Chat Actor : Start speak
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatStartSpeak(bool _shouldHearMyOwnVoice = true, bool isGlobal = true, int32 radioChannel = 0, bool useProximity = false, float maxProximityRange = 0);

	// Local Voice Chat Actor : Start speak to several radio channel
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatStartSpeakTeamArray(bool _shouldHearMyOwnVoice, bool isGlobal, TArray<int32> radioChannel, bool useProximity, float maxProximityRange);

	// Local Voice Chat Actor : Stop speak
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatStopSpeak();
	
	// Local Voice Chat Actor : Multiply your microphone volume
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatSetMicrophoneVolume(float volume);
	
	// Helpers function : Mute someone given its APlayerState
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal", meta = (WorldContext = "WorldContextObject"))
		static void VoiceChatLocalMuteSomeone(const UObject* WorldContextObject, APlayerState *playerToMute, bool shouldMute);

	// Helpers function : Check if someone is muted given its APlayerState
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal", meta = (WorldContext = "WorldContextObject"))
		static bool VoiceChatLocalIsMutedSomeone(const UObject* WorldContextObject, APlayerState *playerToCheckMute);
		
	// Local Voice Chat Actor : Register to a radio channel
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatAddChannel(int32 channelToAdd);
	
	// Local Voice Chat Actor : Check if registered to radio channel
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool  VoiceChatCheckRegisteredToChannel(int32 channelToCheck);
	
	// Local Voice Chat Actor : Unregister to a radio channel
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatRemoveChannel(int32 channelToRemove);
	
	// Desktop microphone permission hook. No-op on Steam desktop targets.
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void  VoiceChatAskMicrophonePermission();

	// Desktop microphone permission hook. Returns true on Steam desktop targets.
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool  VoiceChatHasMicrophonePermission();

	// Local Voice Chat Actor : Multiply your microphone volume from PCM data
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void VoiceChatSetRawMicrophoneGain(float gain);

	// Sound settings set global Voice Chat volume
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void VoiceChatSetGlobalVolume(float globalVolume);

	// Sound settings set project Sound Class for Voice Chat audio output
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal", meta = (WorldContext = "WorldContextObject"))
		static void VoiceChatSetSoundClass(const UObject* WorldContextObject, USoundClass* soundClass);

	// Local Voice Chat Actor : Is Speaking
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatIsSpeaking();

	// Helpers function : Get microphones connected to your PC
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void VoiceChatGetAudioDevicesList(TArray<FString>& outDevices);

	// Helpers function : Set microphone to use
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void VoiceChatSetHardwareAudioInput(FString audioInputDeviceName);

	// Helpers function : Get a voice chat actor from a APlayerState
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal", meta = (WorldContext = "WorldContextObject"))
		static APlayerVoiceChatActor* VoiceChatGetActorFromPlayerState(const UObject* WorldContextObject, APlayerState* fromPlayerState);

	// Helpers function : Get how loud is a player from a APlayerState
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal", meta = (WorldContext = "WorldContextObject"))
		static float VoiceChatGetMicrophoneRuntimeVolumeFromPlayerState(const UObject* WorldContextObject, APlayerState* fromPlayerState);

	// Helpers function : Register callback when a new Voice Chat actor is created
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void RegisterCallbackNewVoiceChatActor(const FDelegateNewVoiceChatActor& Delegate);

	// Helpers function : Register callback when a new Voice Chat actor is destroyed
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void RegisterCallbackDeleteVoiceChatActor(const FDelegateDeleteVoiceChatActor& Delegate);

	// Helpers function : Register callback when a local Voice Chat Actor is ready
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void RegisterCallbackMyVoiceChatActorReady(const FDelegateMyVoiceChatActorReady& Delegate);
		
	// Local Voice Chat Actor : Set player name
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatSetPlayerName(FString name);

	// Helpers function : Is all voice chat muted
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatGetMuteAllPlayers();

	// Helpers function : Set voice chat muted
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void VoiceChatSetMuteAllPlayers(bool _muteAll);

	// Helpers function : Set voice chat location tick rate 
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void VoiceChatSetDefaultTickRateUpdateLocation(float tickRate);

	// Helpers function : If volume is superior to threshold, then send data, otherwise skip data
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static void VoiceChatSetThresholdSendData(float thresholdSendData);

	// Local Voice Chat Actor : Enable/disable attenuation and set attenuation path to use
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatSetAttenuationPath(bool enableAttenuation, FString _attenuationPath);

	// Local Voice Chat Actor : Enable/disable source effect and set source effect path to use
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatSetSourceChainEffectPath(bool enableSourceChainEffect, FString _pathToSourceChainEffectAsset);

	// Local Voice Chat Actor : Enable/disable hear my own voice when speaking
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		static bool VoiceChatEnableShouldHearMyOwnVoice(bool enable);
		
	// Helpers function : If you want your user to be able bypass server replicated attenuation, use this function on any voice chat actor by using APlayerState
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void VoiceChatSetSomeoneOverrideLocallyAttenuationPath(const UObject* WorldContextObject, bool enableAttenuation, bool overrideLocally, FString _pathToAttenuationAsset, APlayerState* playerToOverride);

	// Helpers function : If you want your user to be able bypass server replicated source effect , use this function on any voice chat actor by using APlayerState
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void VoiceChatSetSomeoneOverrideLocallySourceEffectPath(const UObject* WorldContextObject, bool enableSourceEffect, bool overrideLocally, FString _pathToSourceEffectAsset, APlayerState* playerToOverride);

	// Helpers function : If you want your user to be able to bypass server replicated volume, use this function on any voice chat actor by using APlayerState
	// <This is Discord style volume adjustment>
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		void VoiceChatSetSomeoneLocallyMultiplierVolume(const UObject* WorldContextObject, float multiplierVolume, APlayerState* playerToOverride);

	// Helpers function : getter function to get bypassed volume value
	// <This is Discord style volume adjustment>
	UFUNCTION(BlueprintCallable, Category = "VoiceChatUniversal")
		float VoiceChatGetSomeoneLocallyMultiplierVolume(const UObject* WorldContextObject, APlayerState* playerToOverride);

};
