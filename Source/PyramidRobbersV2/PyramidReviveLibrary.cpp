#include "PyramidReviveLibrary.h"
#include "PyramidPlayerState.h"
#include "PyramidGameState.h"
#include "PyramidPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UnrealType.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"

void UPyramidReviveLibrary::HandleSoftRespawn(ACharacter* Character, FTransform RespawnTransform, bool bHasValidCheckpoint)
{
	if (!Character || !Character->HasAuthority())
	{
		return;
	}
	if (bHasValidCheckpoint)
	{
		// Location/rotation only: the pawn's visible size comes from component scales
		// (CharacterMesh0 ~0.1775, capsule ~1.3425), so replaying a checkpoint/marker
		// FTransform scale onto the actor root would shrink the character.
		Character->SetActorLocationAndRotation(
			RespawnTransform.GetLocation(), RespawnTransform.GetRotation(),
			false, nullptr, ETeleportType::TeleportPhysics);
		Character->SetActorScale3D(FVector::OneVector);
	}
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
	{
		Move->SetMovementMode(MOVE_Walking);
		Move->StopMovementImmediately();
	}
}

void UPyramidReviveLibrary::HandleHardDeath_MarkPresentationOnly(ACharacter* Character)
{
	(void)Character;
	// Presentation (ragdoll / spectate) stays in Blueprint Multicast_PlayDeathEffects.
}

void UPyramidReviveLibrary::RestoreMovementFromDowned(ACharacter* Character)
{
	if (!Character)
	{
		return;
	}

	// The visible size is driven entirely by component scales, so the actor root must
	// stay unit scale; a stray non-unit root scale (e.g. replayed from a marker transform)
	// would compound with the mesh/capsule scales and shrink the character.
	Character->SetActorScale3D(FVector::OneVector);

	// Restore the capsule to its class-default relative scale as well, in case anything
	// touched it while downed.
	const ACharacter* DefaultChar = Character->GetClass()->GetDefaultObject<ACharacter>();
	UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	if (Capsule && DefaultChar && DefaultChar->GetCapsuleComponent())
	{
		Capsule->SetRelativeScale3D(DefaultChar->GetCapsuleComponent()->GetRelativeScale3D());
	}

	if (USkeletalMeshComponent* Mesh = Character->GetMesh())
	{
		Mesh->SetSimulatePhysics(false);
		Mesh->SetAllBodiesSimulatePhysics(false);
		Mesh->bBlendPhysics = false;

		// The ragdoll leaves the mesh component wherever physics dropped it, and anything
		// parented to a bone travels with it (the first-person camera sits on a head socket).
		// Teleporting the capsule alone therefore leaves the body - and that camera - behind,
		// so the mesh has to be put back at its class-default offset under the capsule.
		const ACharacter* DefaultCharacter = Character->GetClass()->GetDefaultObject<ACharacter>();
		const USkeletalMeshComponent* DefaultMesh = DefaultCharacter ? DefaultCharacter->GetMesh() : nullptr;

		if (Capsule)
		{
			Mesh->AttachToComponent(Capsule, FAttachmentTransformRules::KeepRelativeTransform);
		}
		if (DefaultMesh)
		{
			Mesh->SetRelativeTransform(DefaultMesh->GetRelativeTransform());
			Mesh->SetCollisionProfileName(DefaultMesh->GetCollisionProfileName());
		}
		else
		{
			Mesh->SetCollisionProfileName(TEXT("CharacterMesh"));
		}
	}
	if (Capsule)
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
	if (UCharacterMovementComponent* Move = Character->GetCharacterMovement())
	{
		Move->SetMovementMode(MOVE_Walking);
	}
	if (AController* C = Character->GetController())
	{
		if (APlayerController* PC = Cast<APlayerController>(C))
		{
			Character->EnableInput(PC);
		}
	}
}

bool UPyramidReviveLibrary::TryStartCheckpointReviveVote(AActor* CheckpointActor, ACharacter* Interactor, FTransform SpawnTransform, FText& OutPrompt)
{
	OutPrompt = FText::GetEmpty();
	if (!CheckpointActor || !Interactor || !Interactor->HasAuthority())
	{
		OutPrompt = FText::FromString(TEXT("Invalid interact"));
		return false;
	}

	UWorld* World = CheckpointActor->GetWorld();
	APyramidGameState* GS = World ? World->GetGameState<APyramidGameState>() : nullptr;
	APyramidPlayerState* PS = Interactor->GetPlayerState<APyramidPlayerState>();
	if (!GS || !PS)
	{
		OutPrompt = FText::FromString(TEXT("Missing game state"));
		return false;
	}
	if (GS->IsEscapePhase())
	{
		OutPrompt = FText::FromString(TEXT("Cannot revive during escape"));
		return false;
	}
	if (PS->IsDown())
	{
		OutPrompt = FText::FromString(TEXT("You are downed"));
		return false;
	}
	if (GS->bReviveVoteActive)
	{
		OutPrompt = FText::FromString(TEXT("Revive vote already active"));
		return false;
	}

	TArray<APyramidPlayerState*> Downed;
	GS->GetDownedCandidates(Downed);
	if (Downed.Num() == 0)
	{
		OutPrompt = FText::FromString(TEXT("No downed teammates"));
		return false;
	}

	// Stone count is Blueprint-owned on BP_Thief; caller validates before calling this.
	if (!GS->Server_StartReviveVote(SpawnTransform, PS))
	{
		OutPrompt = FText::FromString(TEXT("Could not start revive vote"));
		return false;
	}

	OutPrompt = FText::FromString(TEXT("Vote to revive"));
	return true;
}

bool UPyramidReviveLibrary::CallFunctionByName(UObject* Target, FName FunctionName)
{
	if (!IsValid(Target))
	{
		return false;
	}

	UFunction* Function = Target->FindFunction(FunctionName);
	if (!Function)
	{
		return false;
	}

	if (Function->ParmsSize == 0)
	{
		Target->ProcessEvent(Function, nullptr);
		return true;
	}

	void* Frame = FMemory::Malloc(Function->ParmsSize, Function->GetMinAlignment());
	FMemory::Memzero(Frame, Function->ParmsSize);
	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->InitializeValue_InContainer(Frame);
	}

	Target->ProcessEvent(Function, Frame);

	for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
	{
		It->DestroyValue_InContainer(Frame);
	}
	FMemory::Free(Frame);
	return true;
}

void UPyramidReviveLibrary::SetUIInputMode(APlayerController* PC, bool bUIOnly, bool bShowCursor)
{
	if (!IsValid(PC) || !PC->IsLocalController())
	{
		return;
	}

	if (bUIOnly)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(InputMode);
	}
	else
	{
		PC->SetInputMode(FInputModeGameOnly());
	}

	PC->bShowMouseCursor = bShowCursor;
}

APawn* UPyramidReviveLibrary::CycleSpectateForward(APlayerController* PC)
{
	if (!IsValid(PC) || !PC->IsLocalController())
	{
		return nullptr;
	}

	APyramidPlayerController* PyramidPC = Cast<APyramidPlayerController>(PC);
	APyramidPlayerState* SelfPS = PC->GetPlayerState<APyramidPlayerState>();
	const bool bDown = SelfPS && SelfPS->IsDown();
	if (PyramidPC)
	{
		if (!PyramidPC->bIsSpectating && !bDown)
		{
			return nullptr;
		}
		PyramidPC->bIsSpectating = true;
	}
	else if (!bDown)
	{
		return nullptr;
	}

	// Same-frame debounce so Enhanced Input + legacy LMB bind cannot double-step.
	static uint64 LastCycleFrame = 0;
	if (GFrameCounter == LastCycleFrame)
	{
		return Cast<APawn>(PC->GetViewTarget());
	}
	LastCycleFrame = GFrameCounter;

	UWorld* World = PC->GetWorld();
	AGameStateBase* GS = World ? World->GetGameState() : nullptr;
	if (!GS)
	{
		return nullptr;
	}

	TArray<APawn*> Alive;
	for (APlayerState* PS : GS->PlayerArray)
	{
		const APyramidPlayerState* PPS = Cast<APyramidPlayerState>(PS);
		if (!IsValid(PPS) || PPS->IsDown())
		{
			continue;
		}
		APawn* Pawn = PPS->GetPawn();
		if (!IsValid(Pawn) || Pawn == PC->GetPawn())
		{
			continue;
		}
		Alive.Add(Pawn);
	}

	if (Alive.Num() == 0)
	{
		return nullptr;
	}

	APawn* Current = Cast<APawn>(PC->GetViewTarget());
	int32 Index = Alive.IndexOfByKey(Current);
	const int32 NextIndex = (Index == INDEX_NONE) ? 0 : (Index + 1) % Alive.Num();
	APawn* NewTarget = Alive[NextIndex];

	PC->SetViewTargetWithBlend(NewTarget, 0.2f);

	if (FObjectProperty* TargetProp = FindFProperty<FObjectProperty>(PC->GetClass(), TEXT("CurrentSpectateTarget")))
	{
		TargetProp->SetObjectPropertyValue_InContainer(PC, NewTarget);
	}

	const FString PlayerName = NewTarget->GetPlayerState() ? NewTarget->GetPlayerState()->GetPlayerName() : NewTarget->GetName();

	auto FillSpectateParams = [&](UFunction* Fn, void* Frame)
	{
		for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			It->InitializeValue_InContainer(Frame);
			const FString PropName = It->GetName();
			if (const FStrProperty* StrProp = CastField<FStrProperty>(*It))
			{
				if (PropName.Equals(TEXT("PlayerName")))
				{
					StrProp->SetPropertyValue_InContainer(Frame, PlayerName);
				}
			}
			else if (const FIntProperty* IntProp = CastField<FIntProperty>(*It))
			{
				if (PropName.Equals(TEXT("AliveIndex")))
				{
					IntProp->SetPropertyValue_InContainer(Frame, NextIndex);
				}
				else if (PropName.Equals(TEXT("AliveCount")))
				{
					IntProp->SetPropertyValue_InContainer(Frame, Alive.Num());
				}
			}
		}
	};

	auto InvokeWithParams = [&](UObject* Target, FName FunctionName)
	{
		UFunction* Fn = Target ? Target->FindFunction(FunctionName) : nullptr;
		if (!Fn)
		{
			return;
		}
		void* Frame = nullptr;
		if (Fn->ParmsSize > 0)
		{
			Frame = FMemory::Malloc(Fn->ParmsSize, Fn->GetMinAlignment());
			FMemory::Memzero(Frame, Fn->ParmsSize);
			FillSpectateParams(Fn, Frame);
		}
		Target->ProcessEvent(Fn, Frame);
		if (Frame)
		{
			for (TFieldIterator<FProperty> It(Fn); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
			{
				It->DestroyValue_InContainer(Frame);
			}
			FMemory::Free(Frame);
		}
	};

	InvokeWithParams(PC, FName(TEXT("Client_OnSpectateChanged")));
	if (FObjectProperty* WidgetProp = FindFProperty<FObjectProperty>(PC->GetClass(), TEXT("SpectatorWidgetRef")))
	{
		if (UUserWidget* Widget = Cast<UUserWidget>(WidgetProp->GetObjectPropertyValue_InContainer(PC)))
		{
			InvokeWithParams(Widget, FName(TEXT("SetSpectateInfo")));
		}
	}

	return NewTarget;
}
