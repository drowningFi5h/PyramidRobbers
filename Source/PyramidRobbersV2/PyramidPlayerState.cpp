#include "PyramidPlayerState.h"
#include "PyramidGameState.h"
#include "PyramidReviveLibrary.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"
#include "EngineUtils.h"

APyramidPlayerState::APyramidPlayerState()
{
	bReplicates = true;
	LivesRemaining = 2;
	bIsDown = false;
	bHasCheckpoint = false;
}

void APyramidPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APyramidPlayerState, LivesRemaining);
	DOREPLIFETIME(APyramidPlayerState, bIsDown);
	DOREPLIFETIME(APyramidPlayerState, bHasCheckpoint);
	DOREPLIFETIME(APyramidPlayerState, LastCheckpointTransform);
}

void APyramidPlayerState::StampCheckpoint(FTransform Transform)
{
	if (!HasAuthority())
	{
		return;
	}
	// Store spawn placement only; scale must never come from a checkpoint/marker or it
	// would be replayed onto the pawn root at respawn and shrink the character.
	Transform.SetScale3D(FVector::OneVector);
	LastCheckpointTransform = Transform;
	bHasCheckpoint = true;
	ForceNetUpdate();
}

void APyramidPlayerState::TryConsumeLife(bool& bRevived, bool& bOutHasValidCheckpoint, FTransform& RespawnTransform)
{
	bRevived = false;
	bOutHasValidCheckpoint = false;
	RespawnTransform = FTransform::Identity;

	if (!HasAuthority() || bIsDown || LivesRemaining <= 0)
	{
		return;
	}

	LivesRemaining -= 1;
	bRevived = true;

	if (bHasCheckpoint)
	{
		bOutHasValidCheckpoint = true;
		RespawnTransform = LastCheckpointTransform;
	}
	else if (UWorld* World = GetWorld())
	{
		// Fallback: first PlayerStart in the world (GameMode default / staging).
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			RespawnTransform = It->GetActorTransform();
			bOutHasValidCheckpoint = true;
			break;
		}
	}

	ForceNetUpdate();
}

void APyramidPlayerState::EnterHardDown()
{
	if (!HasAuthority() || bIsDown)
	{
		return;
	}
	bIsDown = true;
	ForceNetUpdate();
	if (UWorld* World = GetWorld())
	{
		if (APyramidGameState* GS = World->GetGameState<APyramidGameState>())
		{
			GS->RegisterDeath();
		}
	}
	BP_OnMarkedDown();
}

void APyramidPlayerState::MarkDown()
{
	EnterHardDown();
}

void APyramidPlayerState::ApplyStoneRevive(FTransform CheckpointTransform)
{
	if (!HasAuthority())
	{
		return;
	}
	bIsDown = false;
	LivesRemaining = 1;
	CheckpointTransform.SetScale3D(FVector::OneVector);
	LastCheckpointTransform = CheckpointTransform;
	bHasCheckpoint = true;
	ForceNetUpdate();
	BP_OnStoneRevived();
}

void APyramidPlayerState::ReviveAtCheckpoint(FTransform CheckpointTransform)
{
	ApplyStoneRevive(CheckpointTransform);
}

void APyramidPlayerState::OnRep_bIsDown()
{
	// Going down is presented by the existing Blueprint death multicast, so only the
	// revive is mirrored here: the server applies it to its own copy of the pawn, which
	// leaves remote clients ragdolled, input-disabled and stuck in spectate.
	if (bIsDown)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	APawn* OwnedPawn = GetPawn();
	if (!OwnedPawn)
	{
		if (APlayerController* OwningPC = GetPlayerController())
		{
			OwnedPawn = OwningPC->GetPawn();
		}
	}
	if (IsValid(OwnedPawn))
	{
		// Mirror the server-side unit-root correction so the owning client's pawn keeps
		// its correct visible size after the revive replicates down.
		OwnedPawn->SetActorScale3D(FVector::OneVector);
		if (ACharacter* OwnedChar = Cast<ACharacter>(OwnedPawn))
		{
			UPyramidReviveLibrary::RestoreMovementFromDowned(OwnedChar);
		}
		UPyramidReviveLibrary::CallFunctionByName(OwnedPawn, TEXT("RestoreFromDowned"));
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = Cast<APlayerController>(It->Get());
		if (PC && PC->IsLocalController() && PC->PlayerState == this)
		{
			UPyramidReviveLibrary::CallFunctionByName(PC, TEXT("Client_ExitSpectate"));
			UE_LOG(LogPyramidRevive, Log, TEXT("Mirrored revive locally for %s (exited spectate)"), *GetPlayerName());
		}
	}
}
