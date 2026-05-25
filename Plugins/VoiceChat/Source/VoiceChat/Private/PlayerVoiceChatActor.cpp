#include "PlayerVoiceChatActor.h"
#include "IMediaCaptureSupport.h"
#include "IMediaModule.h"
#include "Misc/MediaBlueprintFunctionLibrary.h"
#include "MediaCaptureSupport.h"
#include "VoiceChat.h"

/* singleton local player */
APlayerVoiceChatActor *APlayerVoiceChatActor::myPlayerVoiceActor = NULL;
/* selected audio device */
FString APlayerVoiceChatActor::selectedAudioInputDevice = "";
/* microphone gain */
float APlayerVoiceChatActor::rawMicrophoneGain = 1.0f;
/* mute all player */
float APlayerVoiceChatActor::voiceChatGlobalVolume = 1.0f;
/* project sound class used for voice playback */
USoundClass* APlayerVoiceChatActor::voiceChatSoundClass = nullptr;
/* mute all player */
bool APlayerVoiceChatActor::muteAll = false;
/* update actor location and rotation tickrate */
float APlayerVoiceChatActor::tickRateUpdateActor = 0.2f;

float APlayerVoiceChatActor::thresholdSendData = 0.0f;

FDelegateNewVoiceChatActor UUniversalVoiceChat::delegateStaticNewVoiceChatActor;
FDelegateDeleteVoiceChatActor UUniversalVoiceChat::delegateStaticDeleteVoiceChatActor;
FDelegateMyVoiceChatActorReady UUniversalVoiceChat::delegateStaticMyVoiceChatActorReady;

APlayerVoiceChatActor *UUniversalVoiceChat::GetMyPlayerVoiceActor() {
	return APlayerVoiceChatActor::myPlayerVoiceActor;
}

APlayerVoiceChatActor::APlayerVoiceChatActor() : Super() {
	bReplicates = true;
	SetNetCullDistanceSquared(50000000000.0f);
	bNetLoadOnClient = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.bAllowTickOnDedicatedServer = true;
	SetActorTickInterval(0.2f);
	//root component
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	RootComponent = RootSceneComponent;

}

// server add mic comp on start
void APlayerVoiceChatActor::BeginPlay() {
	Super::BeginPlay();

	if (APlayerVoiceChatActor::tickRateUpdateActor > 0.0f) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor BeginPlay set tickrate %f"), APlayerVoiceChatActor::tickRateUpdateActor);
		SetActorTickInterval(APlayerVoiceChatActor::tickRateUpdateActor);
	}

	UUniversalVoiceChat::delegateStaticNewVoiceChatActor.ExecuteIfBound(this);

	// spawn micro comp par server
	if (GetWorld()->GetNetMode() == ENetMode::NM_ListenServer || GetWorld()->GetNetMode() == ENetMode::NM_DedicatedServer || GetWorld()->GetNetMode() == ENetMode::NM_Standalone) {
		microphoneSpeakComponent = NewObject<UMicrophoneSpeakComponent>(this, UMicrophoneSpeakComponent::StaticClass());		
		microphoneSpeakComponent->RegisterComponent();		
		microphoneSpeakComponent->SetIsReplicated(true);
		if (GetWorld()->GetNetMode() == NM_Standalone) {
			SetOwner(UGameplayStatics::GetPlayerController(this, 0));
			OnRep_Owner(); // not called in standalone, so call it manually
		}
		
		if (GetWorld()->GetNetMode() == NM_ListenServer) {
			bool OwnedLocally = IsOwnedBy(UGameplayStatics::GetPlayerController(this, 0));
			if (OwnedLocally) {				
				OnRep_Owner();
			}			
		}

		
		if (IsMicrophoneComponentValid()) {
			UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor BeginPlay try set attenuation"));
			if (!pathToAttenuationAsset.IsEmpty()) {
				this->microphoneSpeakComponent->setAttenuationAssetPath(true, pathToAttenuationAsset);
			}
			else {
				this->microphoneSpeakComponent->setAttenuationAssetPath(false, pathToAttenuationAsset);
			}

			if (!pathToSourceChainEffectAsset.IsEmpty()) {
				this->microphoneSpeakComponent->setSourceChainEffectAssetPath(true, pathToSourceChainEffectAsset);
			}
			else {
				this->microphoneSpeakComponent->setSourceChainEffectAssetPath(false, pathToSourceChainEffectAsset);
			}

		}
		else {
			UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor BeginPlay try set attenuation and source effect his->microphoneSpeakComponent null"));
		}

		FScriptDelegate Delegate;
		Delegate.BindUFunction(this, "DelegateEndPlayOwner");
		GetOwner()->OnEndPlay.AddUnique(Delegate);

		
		if (GetOwner()->IsA(APlayerController::StaticClass())) {
			APlayerController* controllerOwner = ((APlayerController*)GetOwner());
			ownerPlayerState = controllerOwner->GetPlayerState<APlayerState>();
		}
	}	
}

// clean static if owned locally on destroy
void APlayerVoiceChatActor::EndPlay(const EEndPlayReason::Type EndPlayReason){

	Super::EndPlay(EndPlayReason);

	UUniversalVoiceChat::delegateStaticDeleteVoiceChatActor.ExecuteIfBound(this);

	//UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor OnDestroy"));
	bool OwnedLocally = myPlayerVoiceActor == this;
	if (OwnedLocally) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor EndPlay OwnedLocally server %d"), GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_DedicatedServer);
		// make sure to cut audio
		UUniversalVoiceChat::VoiceChatStopSpeak();
		// NULL the singleton
		myPlayerVoiceActor = NULL;
	}
	else {
		UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor EndPlay not OwnedLocally server %d"), GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_DedicatedServer);
	}


	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerVoiceChatActor::StaticClass(), FoundActors);
	UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor EndPlay total actors left %d"), FoundActors.Num());
}

// replication UE4 interne
void APlayerVoiceChatActor::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APlayerVoiceChatActor, microphoneSpeakComponent);
	DOREPLIFETIME(APlayerVoiceChatActor, idVoiceChat);
	DOREPLIFETIME(APlayerVoiceChatActor, playerName);
	DOREPLIFETIME(APlayerVoiceChatActor, ownerPlayerState);
	DOREPLIFETIME(APlayerVoiceChatActor, radioChannelSubscribed);
	DOREPLIFETIME(APlayerVoiceChatActor, pathToAttenuationAsset);
	DOREPLIFETIME(APlayerVoiceChatActor, pathToSourceChainEffectAsset);
	DOREPLIFETIME(APlayerVoiceChatActor, voiceVolume);
	DOREPLIFETIME(APlayerVoiceChatActor, isMicrophoneOn);
	
	//UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor GetLifetimeReplicatedProps"));
}

void APlayerVoiceChatActor::RepNotifyMicComp() {

	UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor::RepNotifyMicComp"));
	if (IsMicrophoneComponentValid()) {
		if (!pathToAttenuationAsset.IsEmpty()) {
			this->microphoneSpeakComponent->setAttenuationAssetPath(true, pathToAttenuationAsset);
		}
		else {
			this->microphoneSpeakComponent->setAttenuationAssetPath(false, pathToAttenuationAsset);
		}

		if (!pathToSourceChainEffectAsset.IsEmpty()) {
			this->microphoneSpeakComponent->setSourceChainEffectAssetPath(true, pathToSourceChainEffectAsset);
		}
		else {
			this->microphoneSpeakComponent->setSourceChainEffectAssetPath(false, pathToSourceChainEffectAsset);
		}

		// delegateStaticMyVoiceChatActorReady is executed only on clients or listen server host
		bool OwnedLocally = IsOwnedBy(UGameplayStatics::GetPlayerController(this, 0)) && GetNetMode() != NM_DedicatedServer;
		if (OwnedLocally) {
			UUniversalVoiceChat::delegateStaticMyVoiceChatActorReady.ExecuteIfBound(this);
		}
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor RepNotifyMicComp his->microphoneSpeakComponent null"));
	}
}

void APlayerVoiceChatActor::RepNotifyAttenuationAsset() {
	
	UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor::RepNotifyAttenuationAsset"));
	//if (!pathToAttenuationAsset.IsEmpty()) {
	//	UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor RepNotifyAttenuationAsset server ? %d %s"), GetWorld()->IsServer(), *pathToAttenuationAsset);
	//}
	//if (GEngine)
	//	GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, FString::Printf(TEXT("RepNotifyAttenuationAsset %s empty? %d"), *GetName(), pathToAttenuationAsset.IsEmpty()));

	if (IsMicrophoneComponentValid()) {
		if (!pathToAttenuationAsset.IsEmpty()) {
			this->microphoneSpeakComponent->setAttenuationAssetPath(true, pathToAttenuationAsset);
		}
		else {
			this->microphoneSpeakComponent->setAttenuationAssetPath(false, pathToAttenuationAsset);
		}
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor RepNotifyAttenuationAsset his->microphoneSpeakComponent null"));
	}	
}


void APlayerVoiceChatActor::RepNotifySourceEffectAsset() {
	UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor::RepNotifySourceEffectAsset"));
	
	//if (GEngine)
	//	GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow, FString::Printf(TEXT("RepNotifySourceEffectAsset %s empty? %d"), *GetName(), pathToAttenuationAsset.IsEmpty()));

	if (IsMicrophoneComponentValid()) {
		if (!pathToSourceChainEffectAsset.IsEmpty()) {
			this->microphoneSpeakComponent->setSourceChainEffectAssetPath(true, pathToSourceChainEffectAsset);
		}
		else {
			this->microphoneSpeakComponent->setSourceChainEffectAssetPath(false, pathToSourceChainEffectAsset);
		}
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor RepNotifySourceEffectAsset this->microphoneSpeakComponent null"));
	}
}


void APlayerVoiceChatActor::RepNotifyVoiceVolume() {
	UE_LOG(LogVoiceChatPro, Display, TEXT("RepNotifyVoiceVolume"));
	if (IsMicrophoneComponentValid()) {
		this->microphoneSpeakComponent->SetVoiceVolume(voiceVolume);
	}
}

void APlayerVoiceChatActor::RepNotifyPlayerName() {
	UE_LOG(LogVoiceChatPro, Display, TEXT("RepNotifyPlayerName %s"), *playerName);
	OnPlayerNameReceived.Broadcast(playerName);
}

void APlayerVoiceChatActor::RepNotifyIsMicrophoneOn() {
	UE_LOG(LogVoiceChatPro, Display, TEXT("RepNotifyIsMicrophoneOn %d"), isMicrophoneOn);
	OnIsMicrophoneOnReceived.Broadcast(isMicrophoneOn);
}

// automatically remove voice chat if owner is destroyed
void APlayerVoiceChatActor::DelegateEndPlayOwner(AActor* act, EEndPlayReason::Type EndPlayReason) {
	UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor DelegateEndPlayOwner"));
	if (IsValid(this)) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor DelegateEndPlayOwner start destroy this"));
		Destroy();
	}
}

// event called when server has set owner
void APlayerVoiceChatActor::OnRep_Owner() {
	Super::OnRep_Owner();
	//UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor OnRep_Owner"));
	
	bool OwnedLocally = IsOwnedBy(UGameplayStatics::GetPlayerController(this, 0)) && GetNetMode() != NM_DedicatedServer;
	if (OwnedLocally) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor OnRep_Owner OwnedLocally server %d"), GetWorld()->GetNetMode() == ENetMode::NM_ListenServer || GetWorld()->GetNetMode() == ENetMode::NM_DedicatedServer || GetWorld()->GetNetMode() == ENetMode::NM_Standalone);
		myPlayerVoiceActor = this;
	}
	else {
		UE_LOG(LogVoiceChatPro, Display, TEXT("APlayerVoiceChatActor OnRep_Owner not OwnedLocally server %d"), GetWorld()->GetNetMode() == ENetMode::NM_ListenServer || GetWorld()->GetNetMode() == ENetMode::NM_DedicatedServer || GetWorld()->GetNetMode() == ENetMode::NM_Standalone);
	}
}


void APlayerVoiceChatActor::Tick(float DeltaTime) {
	Super::Tick(DeltaTime);
	//UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor server ? %d ownerPlayerState NULL ? %d "), GetWorld()->IsServer(), ownerPlayerState == NULL);


	if (GetWorld() != NULL && (GetWorld()->GetNetMode() == ENetMode::NM_ListenServer || GetWorld()->GetNetMode() == ENetMode::NM_DedicatedServer || GetWorld()->GetNetMode() == ENetMode::NM_Standalone)) {
		APlayerController *ownerPC = (APlayerController *)GetOwner();
		if (ownerPC != NULL) {
			if (ownerPC->GetPawn() != NULL) {
				SetActorLocation(ownerPC->GetPawn()->GetActorLocation());
				SetActorRotation(ownerPC->GetPawn()->GetActorRotation());
				RPCServerUpdatePosAudioComp(GetActorLocation(), GetActorRotation());
				//UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor  GetActorLocation %s"), *(ownerPC->GetCharacter()->GetActorLocation().ToString()) );
			}
		}
	}
	
}


/* server set, pathToAttenuationAsset is automatically replicated */
void APlayerVoiceChatActor::ServerSetAttenuation(bool enableAttenuation, FString _pathToAttenuationAsset) {
	if (enableAttenuation) {
		pathToAttenuationAsset = _pathToAttenuationAsset;
	}
	else {
		pathToAttenuationAsset = "";
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_Standalone) {
		RepNotifyAttenuationAsset();
	}
}

/* server set, pathToAttenuationAsset is automatically replicated */
void APlayerVoiceChatActor::ServerSetSourceChainEffect(bool enableSourceChainEffect, FString _pathToSourceChainEffect) {
	if (enableSourceChainEffect) {
		pathToSourceChainEffectAsset = _pathToSourceChainEffect;
	}
	else {
		pathToSourceChainEffectAsset = "";
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_Standalone) {
		RepNotifySourceEffectAsset();
	}
}

void APlayerVoiceChatActor::ServerSetAllowUseProximity(bool _allowUseRange) {
	AntiCheatAllowUseProximity = _allowUseRange;
}



void APlayerVoiceChatActor::ServerSetAllowUseGlobal(bool _allowUseGlobal) {
	AntiCheatAllowUseGlobal = _allowUseGlobal;
}



void  APlayerVoiceChatActor::ServerSetMaxProximityRange(float _maxProximityRange) {
	AntiCheatMaxProximityRange = _maxProximityRange;
}



/* mute audio locally, no server involved in this */
void APlayerVoiceChatActor::muteAudio(bool isMute) {
	if (IsMicrophoneComponentValid()) {
		microphoneSpeakComponent->muteAudio(isMute);
	}	
}

/* server set, radioChannelSubscribed is automatically replicated */
void APlayerVoiceChatActor::ServerAddChannel(int32 channelToAdd) {
	if (!radioChannelSubscribed.Contains(channelToAdd)) {
		radioChannelSubscribed.Add(channelToAdd);
	}
	//UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor::AddChannel total %d"), radioChannelSubscribed.Num());
}

/* server set, radioChannelSubscribed is automatically replicated */
void APlayerVoiceChatActor::ServerRemoveChannel(int32 channelToRemove) {
	radioChannelSubscribed.Remove(channelToRemove);
	//UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor::RemoveChannel total %d"), radioChannelSubscribed.Num());
}

/* RPC Server => Client */
void APlayerVoiceChatActor::RPCServerUpdatePosAudioComp_Implementation(FVector worldPos, FRotator worldRotation) {
	SetActorLocation(worldPos);
	SetActorRotation(worldRotation);
}

/* RPC Client => Server */
void APlayerVoiceChatActor::RPCClientAskAddChannel_Implementation(int32 channelToAdd) {
	ServerAddChannel(channelToAdd);
}

/* RPC Client => Server */
void APlayerVoiceChatActor::RPCClientAskRemoveChannel_Implementation(int32 channelToRemove) {
	ServerRemoveChannel(channelToRemove);
}

void APlayerVoiceChatActor::RPCClientSetMicrophoneVolume_Implementation(float volume) {
	voiceVolume = volume;
	if (IsMicrophoneComponentValid()) {
		microphoneSpeakComponent->SetVoiceVolume(volume);
	}
	if (GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_Standalone) {
		RepNotifyVoiceVolume();
	}
}

void APlayerVoiceChatActor::RPCClientSetPlayerName_Implementation(const FString& name) {
	playerName = name;
	if (GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_Standalone) {
		RepNotifyPlayerName();
	}
}

void APlayerVoiceChatActor::RPCClientSetIsMicrophoneOn_Implementation(bool _isMicrophoneOn) {
	isMicrophoneOn = _isMicrophoneOn;
	if (GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_Standalone) {
		RepNotifyIsMicrophoneOn();
	}
}

void APlayerVoiceChatActor::RPCClientSetAttenuationPath_Implementation(const FString& _pathToAttenuationAsset) {
	pathToAttenuationAsset = _pathToAttenuationAsset;
	if (GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_Standalone) {
		RepNotifyAttenuationAsset();
	}
}

void APlayerVoiceChatActor::RPCClientSetSourceChainEffectPath_Implementation(const FString& _pathToSourceChainEffectAsset) {
	pathToSourceChainEffectAsset = _pathToSourceChainEffectAsset;
	if (GetWorld()->GetNetMode() == NM_ListenServer || GetWorld()->GetNetMode() == NM_Standalone) {
		RepNotifySourceEffectAsset();
	}
}

void APlayerVoiceChatActor::setOverrideLocallyAttenuationPath(bool enableAttenuation, bool overrideLocally, FString _pathToAttenuationAsset) {
	if (IsMicrophoneComponentValid()) {
		microphoneSpeakComponent->setOverrideLocallyAttenuationPath(enableAttenuation, overrideLocally, _pathToAttenuationAsset);
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor::setOverrideLocallyAttenuationPath error microphoneSpeakComponent != nullptr && IsValid(microphoneSpeakComponent"));
	}
}

void APlayerVoiceChatActor::setOverrideLocallySourceEffectPath(bool enableSourceEffect, bool overrideLocally, FString _pathToSourceEffectAsset) {
	if (IsMicrophoneComponentValid()) {
		microphoneSpeakComponent->setOverrideLocallySourceEffectPath(enableSourceEffect, overrideLocally, _pathToSourceEffectAsset);
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor::setOverrideLocallySourceEffectPath error microphoneSpeakComponent != nullptr && IsValid(microphoneSpeakComponent"));
	}
}


void APlayerVoiceChatActor::setLocallyMultiplierVolume(float multiplierVolume) {
	if (IsMicrophoneComponentValid()) {
		microphoneSpeakComponent->setLocallyMultiplierVolume(multiplierVolume);
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor::setLocallyMultiplierVolume error microphoneSpeakComponent != nullptr && IsValid(microphoneSpeakComponent"));
	}
}

float APlayerVoiceChatActor::getLocallyMultiplierVolume() {
	if (IsMicrophoneComponentValid()) {
		return microphoneSpeakComponent->getLocallyMultiplierVolume();
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor::getLocallyMultiplierVolume error microphoneSpeakComponent != nullptr && IsValid(microphoneSpeakComponent"));
		return 1.0f;
	}
}

bool APlayerVoiceChatActor::IsMicrophoneComponentValid() {
	bool res = microphoneSpeakComponent != nullptr && IsValid(microphoneSpeakComponent);
	if (!res) {
		if (microphoneSpeakComponent == nullptr) {
			UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor::IsMicrophoneComponentValid microphoneSpeakComponent == nullptr, maybe you should use delegateStaticMyVoiceChatActorReady"));
		}
		else {
			if (!IsValid(microphoneSpeakComponent)) {
				UE_LOG(LogVoiceChatPro, Warning, TEXT("APlayerVoiceChatActor::IsMicrophoneComponentValid !IsValid(microphoneSpeakComponent), maybe you should use delegateStaticMyVoiceChatActorReady"));
			}
		}
	}
	return res;
}


/************** BLUEPRINT LIBRARY ***************************************/


// get your local player voice chat, you need this if you want to receive raw microphone data
APlayerVoiceChatActor* UUniversalVoiceChat::VoiceChatGetMyLocalPlayerVoiceChat() {
	if (APlayerVoiceChatActor::myPlayerVoiceActor == NULL) {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("UUniversalVoiceChat::GetMyLocalPlayerVoiceChat is NULL"));
	}
	return APlayerVoiceChatActor::myPlayerVoiceActor;
}

bool UUniversalVoiceChat::VoiceChatInitAudioVoiceChatQuality(int32 _sampleRate, int32 _numChannels, EOpusFramePerSec opusFramePerSec) {
	if (APlayerVoiceChatActor::myPlayerVoiceActor == NULL || APlayerVoiceChatActor::myPlayerVoiceActor->microphoneSpeakComponent == NULL) {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("UUniversalVoiceChat::initVoiceChatQuality myPlayerVoiceActor is NULL"));
		return false;
	}
	int32 opusfps = 200;
	switch (opusFramePerSec) {
		case EOpusFramePerSec::OPUS_FPS_400:
			opusfps = 400;
			break;
		case EOpusFramePerSec::OPUS_FPS_200:
			opusfps = 200;
			break;
		case EOpusFramePerSec::OPUS_FPS_100:
			opusfps = 100;
			break;
		case EOpusFramePerSec::OPUS_FPS_50:
			opusfps = 50;
			break;
		case EOpusFramePerSec::OPUS_FPS_25:
			opusfps = 25;
			break;
		default:
			break;
	};

	return APlayerVoiceChatActor::myPlayerVoiceActor->microphoneSpeakComponent->initAudioResources(_sampleRate, _numChannels, opusfps);
}

bool UUniversalVoiceChat::VoiceChatWasInitAudioVoiceChatQuality() {
	if (APlayerVoiceChatActor::myPlayerVoiceActor == NULL || APlayerVoiceChatActor::myPlayerVoiceActor->microphoneSpeakComponent == NULL) {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("UUniversalVoiceChat::wasInitAudioVoiceChatQuality is NULL"));
		return false;
	}
	return APlayerVoiceChatActor::myPlayerVoiceActor->microphoneSpeakComponent->getWasInitAudioResources();
}

// use this to start speaking (local owned client side only)
bool UUniversalVoiceChat::VoiceChatStartSpeak(bool _shouldHearMyOwnVoice, bool isGlobal, int32 radioChannel, bool useRange, float maxRange) {	
	UE_LOG(LogVoiceChatPro, Warning, TEXT("[VoiceChatPlugin] BP Request: VoiceChatStartSpeak."));
	TArray<int32> radioChannelArray;
	radioChannelArray.Add(radioChannel);
	return UUniversalVoiceChat::VoiceChatStartSpeakTeamArray(_shouldHearMyOwnVoice, isGlobal, radioChannelArray, useRange, maxRange);
}

// use this to start speaking to several team radio channel (local owned client side only)
bool UUniversalVoiceChat::VoiceChatStartSpeakTeamArray(bool _shouldHearMyOwnVoice, bool isGlobal, TArray<int32> radioChannel, bool useRange, float maxRange) {

	bool res = false;

	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor) && APlayerVoiceChatActor::myPlayerVoiceActor->IsMicrophoneComponentValid()) {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("[VoiceChatPlugin] VoiceChatStartSpeakTeamArray OK! Calling Component..."));
		res = APlayerVoiceChatActor::myPlayerVoiceActor->microphoneSpeakComponent->startSpeaking(_shouldHearMyOwnVoice, isGlobal, radioChannel, useRange, maxRange);
		if(res) APlayerVoiceChatActor::myPlayerVoiceActor->RPCClientSetIsMicrophoneOn(true);
	}
	else {
		UE_LOG(LogVoiceChatPro, Error, TEXT("[VoiceChatPlugin] CRITICAL ERROR: VoiceChatStartSpeak failed! myPlayerVoiceActor is NULL or invalid. DID YOU SPAWN IT ON THE SERVER?"));
		res = false;
	}
	return res;
}

// use this to stop speaking (local owned client side only)
bool UUniversalVoiceChat::VoiceChatStopSpeak() {

	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor) && APlayerVoiceChatActor::myPlayerVoiceActor->IsMicrophoneComponentValid()) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("VoiceChatStopSpeak ok"));
		APlayerVoiceChatActor::myPlayerVoiceActor->microphoneSpeakComponent->endSpeaking();
		APlayerVoiceChatActor::myPlayerVoiceActor->RPCClientSetIsMicrophoneOn(false);
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatStopSpeak error"));
		return false;
	}
	return true;
}

bool UUniversalVoiceChat::VoiceChatSetMicrophoneVolume(float volume) {

	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor)) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("VoiceChatSetMicrophoneVolume ok"));
		APlayerVoiceChatActor::myPlayerVoiceActor->RPCClientSetMicrophoneVolume(volume);

		if (APlayerVoiceChatActor::myPlayerVoiceActor->IsMicrophoneComponentValid()) {
			APlayerVoiceChatActor::myPlayerVoiceActor->microphoneSpeakComponent->SetVoiceVolume(volume);
		}
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatSetMicrophoneVolume error"));
		return false;
	}
	return true;
}



// use this to mute someone (local owned client side only)
void UUniversalVoiceChat::VoiceChatLocalMuteSomeone(const UObject* WorldContextObject, APlayerState *playerToMute, bool shouldMute) {
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull), APlayerVoiceChatActor::StaticClass(), FoundActors);
	UE_LOG(LogVoiceChatPro, Display, TEXT(" VoiceChatMuteSomeone total actor found %d"), FoundActors.Num());
	for (int i = 0; i < FoundActors.Num(); i++) {
		if (((APlayerVoiceChatActor*)FoundActors[i])->ownerPlayerState == playerToMute) {
			((APlayerVoiceChatActor*)FoundActors[i])->muteAudio(shouldMute);
			break;
		}
	}
}

// check if we muted someone locally (local owned client side only)
bool UUniversalVoiceChat::VoiceChatLocalIsMutedSomeone(const UObject* WorldContextObject, APlayerState *playerToCheckMute) {
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull), APlayerVoiceChatActor::StaticClass(), FoundActors);
	UE_LOG(LogVoiceChatPro, Display, TEXT(" VoiceChatLocalIsMutedSomeone total actor found %d"), FoundActors.Num());
	for (int i = 0; i < FoundActors.Num(); i++) {
		if (((APlayerVoiceChatActor*)FoundActors[i])->ownerPlayerState == playerToCheckMute && ((APlayerVoiceChatActor*)FoundActors[i])->IsMicrophoneComponentValid()) {
			return ((APlayerVoiceChatActor*)FoundActors[i])->microphoneSpeakComponent->isMutedLocalSetting;
		}
	}
	return false;
}


bool  UUniversalVoiceChat::VoiceChatCheckRegisteredToChannel(int32 channelToCheck) {
	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor)) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("VoiceChatCheckRegisteredToChannel ok"));
		return APlayerVoiceChatActor::myPlayerVoiceActor->radioChannelSubscribed.Contains(channelToCheck);
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatCheckRegisteredToChannel error"));
		return false;
	}
}


bool UUniversalVoiceChat::VoiceChatAddChannel(int32 channelToAdd) {
	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor)) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("VoiceChatAddChannel ok"));
		APlayerVoiceChatActor::myPlayerVoiceActor->RPCClientAskAddChannel(channelToAdd);
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatAddChannel error"));
		return false;
	}
	return true;
}

bool UUniversalVoiceChat::VoiceChatRemoveChannel(int32 channelToRemove) {
	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor)) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("VoiceChatRemoveChannel ok"));
		APlayerVoiceChatActor::myPlayerVoiceActor->RPCClientAskRemoveChannel(channelToRemove);
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatRemoveChannel error"));
		return false;
	}
	return true;
}

bool  UUniversalVoiceChat::VoiceChatHasMicrophonePermission() {
	return true;
}

void  UUniversalVoiceChat::VoiceChatAskMicrophonePermission() {
}

void UUniversalVoiceChat::VoiceChatSetRawMicrophoneGain(float gain) {
	APlayerVoiceChatActor::rawMicrophoneGain = gain;
}

void UUniversalVoiceChat::VoiceChatSetGlobalVolume(float globalVolume) {
	APlayerVoiceChatActor::voiceChatGlobalVolume = globalVolume;
}

void UUniversalVoiceChat::VoiceChatSetSoundClass(const UObject* WorldContextObject, USoundClass* soundClass) {
	APlayerVoiceChatActor::voiceChatSoundClass = soundClass;

	UWorld* world = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!world) {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatSetSoundClass error world is null"));
		return;
	}

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(world, APlayerVoiceChatActor::StaticClass(), FoundActors);
	for (AActor* FoundActor : FoundActors) {
		APlayerVoiceChatActor* VoiceActor = Cast<APlayerVoiceChatActor>(FoundActor);
		if (VoiceActor && VoiceActor->IsMicrophoneComponentValid()) {
			VoiceActor->microphoneSpeakComponent->SetVoiceSoundClass(soundClass);
		}
	}
}

bool UUniversalVoiceChat::VoiceChatIsSpeaking() {
	bool res = false;

	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor) && APlayerVoiceChatActor::myPlayerVoiceActor->IsMicrophoneComponentValid()) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("VoiceChatIsSpeaking ok"));
		res = APlayerVoiceChatActor::myPlayerVoiceActor->microphoneSpeakComponent->isSpeakingLocalSetting;
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatIsSpeaking error"));
	}

	return res;
}

void UUniversalVoiceChat::VoiceChatGetAudioDevicesList(TArray<FString>& outDevices) {

#if PLATFORM_WINDOWS || PLATFORM_MAC || PLATFORM_LINUX
	TArray<FMediaCaptureDeviceInfo> DeviceInfos;
	MediaCaptureSupport::EnumerateAudioCaptureDevices(DeviceInfos);
	for (const FMediaCaptureDeviceInfo& DeviceInfo : DeviceInfos)
	{
		outDevices.Add(DeviceInfo.DisplayName.ToString());
	}
#endif

}

void UUniversalVoiceChat::VoiceChatSetHardwareAudioInput(FString audioInputDeviceName) {
	APlayerVoiceChatActor::selectedAudioInputDevice = audioInputDeviceName;
}

APlayerVoiceChatActor* UUniversalVoiceChat::VoiceChatGetActorFromPlayerState(const UObject* WorldContextObject, APlayerState* fromPlayerState) {
	APlayerVoiceChatActor* res = nullptr;

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull), APlayerVoiceChatActor::StaticClass(), FoundActors);

	for (int i = 0; i < FoundActors.Num(); i++) {
		if (((APlayerVoiceChatActor*)FoundActors[i])->ownerPlayerState == fromPlayerState) {
			res = (APlayerVoiceChatActor*)FoundActors[i];
			break;
		}
	}

	return res;
}

float UUniversalVoiceChat::VoiceChatGetMicrophoneRuntimeVolumeFromPlayerState(const UObject* WorldContextObject, APlayerState* fromPlayerState) {
	float res = 0.0f;

	TArray<AActor*> FoundVolumeActors;

	UWorld* worldco = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	
	if (worldco) {
		UGameplayStatics::GetAllActorsOfClass(worldco, APlayerVoiceChatActor::StaticClass(), FoundVolumeActors);
	
		for (int i = 0; i < FoundVolumeActors.Num(); i++) {
			if (((APlayerVoiceChatActor*)FoundVolumeActors[i])->ownerPlayerState == fromPlayerState) {
				APlayerVoiceChatActor* actorFound = (APlayerVoiceChatActor*)FoundVolumeActors[i];
				res = (actorFound != nullptr && actorFound->IsMicrophoneComponentValid()) ? actorFound->microphoneSpeakComponent->latestVolume : 0.0f;
				break;
			}
		}
	}

	return res;
}

bool UUniversalVoiceChat::VoiceChatSetPlayerName(FString name) {

	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor)) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("VoiceChatSetPlayerName ok"));
		APlayerVoiceChatActor::myPlayerVoiceActor->RPCClientSetPlayerName(name);
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatSetPlayerName error"));
		return false;
	}
	return true;
}

bool UUniversalVoiceChat::VoiceChatGetMuteAllPlayers() {
	return APlayerVoiceChatActor::muteAll;
}

void UUniversalVoiceChat::VoiceChatSetMuteAllPlayers(bool _muteAll) {
	APlayerVoiceChatActor::muteAll = _muteAll;
}

void UUniversalVoiceChat::VoiceChatSetDefaultTickRateUpdateLocation(float tickRate) {
	APlayerVoiceChatActor::tickRateUpdateActor = tickRate;
}

void UUniversalVoiceChat::RegisterCallbackNewVoiceChatActor(const FDelegateNewVoiceChatActor& Delegate) {
	delegateStaticNewVoiceChatActor = Delegate;
}

void UUniversalVoiceChat::RegisterCallbackDeleteVoiceChatActor(const FDelegateDeleteVoiceChatActor& Delegate) {
	delegateStaticDeleteVoiceChatActor = Delegate;
}

void UUniversalVoiceChat::RegisterCallbackMyVoiceChatActorReady(const FDelegateMyVoiceChatActorReady& Delegate) {
	delegateStaticMyVoiceChatActorReady = Delegate;
}


void UUniversalVoiceChat::VoiceChatSetThresholdSendData(float thresholdSendData) {
	APlayerVoiceChatActor::thresholdSendData = thresholdSendData;
}


bool UUniversalVoiceChat::VoiceChatSetAttenuationPath(bool enableAttenuation, FString _attenuationPath) {
	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor)) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("VoiceChatSetAttenuationPath ok"));
		APlayerVoiceChatActor::myPlayerVoiceActor->RPCClientSetAttenuationPath(enableAttenuation ? _attenuationPath : "");
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatSetAttenuationPath error"));
		return false;
	}
	return true;
}

bool UUniversalVoiceChat::VoiceChatSetSourceChainEffectPath(bool enableSourceChainEffect, FString _pathToSourceChainEffectAsset){
	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor)) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("VoiceChatSetSourceChainEffectPath ok"));
		APlayerVoiceChatActor::myPlayerVoiceActor->RPCClientSetSourceChainEffectPath(enableSourceChainEffect ? _pathToSourceChainEffectAsset : "");
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatSetSourceChainEffectPath error"));
		return false;
	}
	return true;
}

bool UUniversalVoiceChat::VoiceChatEnableShouldHearMyOwnVoice(bool enable){
	if (APlayerVoiceChatActor::myPlayerVoiceActor != NULL && IsValid(APlayerVoiceChatActor::myPlayerVoiceActor) 
		&& APlayerVoiceChatActor::myPlayerVoiceActor->IsMicrophoneComponentValid()) {
		UE_LOG(LogVoiceChatPro, Display, TEXT("VoiceChatEnableShouldHearMyOwnVoice ok"));
		APlayerVoiceChatActor::myPlayerVoiceActor->microphoneSpeakComponent->shouldHearMyOwnVoiceLocalSetting = enable;
	}
	else {
		UE_LOG(LogVoiceChatPro, Warning, TEXT("VoiceChatEnableShouldHearMyOwnVoice error"));
		return false;
	}
	return true;
}

bool UUniversalVoiceChat::IsMyPlayerVoiceChatActorReady() {
	return APlayerVoiceChatActor::myPlayerVoiceActor != nullptr 
		&& IsValid(APlayerVoiceChatActor::myPlayerVoiceActor)
		&& APlayerVoiceChatActor::myPlayerVoiceActor->IsMicrophoneComponentValid();
}

void UUniversalVoiceChat::VoiceChatSetSomeoneOverrideLocallyAttenuationPath(const UObject* WorldContextObject, bool enableAttenuation, bool overrideLocally, FString _pathToAttenuationAsset, APlayerState* playerToOverride) {
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull), APlayerVoiceChatActor::StaticClass(), FoundActors);
	UE_LOG(LogVoiceChatPro, Display, TEXT(" VoiceChatSetSomeoneOverrideLocallyAttenuationPath total actor found %d"), FoundActors.Num());
	for (int i = 0; i < FoundActors.Num(); i++) {
		if (IsValid(((APlayerVoiceChatActor*)FoundActors[i])) && ((APlayerVoiceChatActor*)FoundActors[i])->ownerPlayerState == playerToOverride) {
			((APlayerVoiceChatActor*)FoundActors[i])->setOverrideLocallyAttenuationPath(enableAttenuation, overrideLocally, _pathToAttenuationAsset);
		}
	}
}

void UUniversalVoiceChat::VoiceChatSetSomeoneOverrideLocallySourceEffectPath(const UObject* WorldContextObject, bool enableSourceEffect, bool overrideLocally, FString _pathToSourceEffectAsset, APlayerState* playerToOverride) {
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull), APlayerVoiceChatActor::StaticClass(), FoundActors);
	UE_LOG(LogVoiceChatPro, Display, TEXT(" VoiceChatSetSomeoneOverrideLocallySourceEffectPath total actor found %d"), FoundActors.Num());
	for (int i = 0; i < FoundActors.Num(); i++) {
		if (IsValid(((APlayerVoiceChatActor*)FoundActors[i])) && ((APlayerVoiceChatActor*)FoundActors[i])->ownerPlayerState == playerToOverride) {
			((APlayerVoiceChatActor*)FoundActors[i])->setOverrideLocallySourceEffectPath(enableSourceEffect, overrideLocally, _pathToSourceEffectAsset);
		}
	}
}

void UUniversalVoiceChat::VoiceChatSetSomeoneLocallyMultiplierVolume(const UObject* WorldContextObject, float multiplierVolume, APlayerState* playerToOverride) {
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull), APlayerVoiceChatActor::StaticClass(), FoundActors);
	UE_LOG(LogVoiceChatPro, Display, TEXT(" VoiceChatSetSomeoneLocallyMultiplierVolume total actor found %d"), FoundActors.Num());
	for (int i = 0; i < FoundActors.Num(); i++) {
		if (IsValid(((APlayerVoiceChatActor*)FoundActors[i])) && ((APlayerVoiceChatActor*)FoundActors[i])->ownerPlayerState == playerToOverride) {
			((APlayerVoiceChatActor*)FoundActors[i])->setLocallyMultiplierVolume(multiplierVolume);
		}
	}
}



float UUniversalVoiceChat::VoiceChatGetSomeoneLocallyMultiplierVolume(const UObject* WorldContextObject, APlayerState* playerToOverride) {
	float result = 1.0f;
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull), APlayerVoiceChatActor::StaticClass(), FoundActors);
	UE_LOG(LogVoiceChatPro, Display, TEXT(" VoiceChatGetSomeoneLocallyMultiplierVolume total actor found %d"), FoundActors.Num());
	for (int i = 0; i < FoundActors.Num(); i++) {
		if (IsValid(((APlayerVoiceChatActor*)FoundActors[i])) && ((APlayerVoiceChatActor*)FoundActors[i])->ownerPlayerState == playerToOverride) {
			result = ((APlayerVoiceChatActor*)FoundActors[i])->getLocallyMultiplierVolume();
			break;
		}
	}

	return result;
}
