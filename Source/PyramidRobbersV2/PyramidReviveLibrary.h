#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PyramidReviveLibrary.generated.h"

class ACharacter;
class APyramidPlayerState;
class AActor;
class APlayerController;
class APawn;

/**
 * Thin Blueprint helpers for soft/hard death presentation and checkpoint vote start.
 */
UCLASS()
class PYRAMIDROBBERSV2_API UPyramidReviveLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Soft respawn: teleport if transform valid, never applies ragdoll. */
	UFUNCTION(BlueprintCallable, Category = "Revive", meta = (WorldContext = "WorldContextObject"))
	static void HandleSoftRespawn(ACharacter* Character, FTransform RespawnTransform, bool bHasValidCheckpoint);

	/** Hard death presentation entry; Blueprints should call MarkDown before this. */
	UFUNCTION(BlueprintCallable, Category = "Revive")
	static void HandleHardDeath_MarkPresentationOnly(ACharacter* Character);

	/** Restore collision/movement after stone revive; Blueprints fill heal/exit-spectate. */
	UFUNCTION(BlueprintCallable, Category = "Revive")
	static void RestoreMovementFromDowned(ACharacter* Character);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Revive", meta = (WorldContext = "WorldContextObject"))
	static bool TryStartCheckpointReviveVote(AActor* CheckpointActor, ACharacter* Interactor, FTransform SpawnTransform, FText& OutPrompt);

	/**
	 * Puts a player controller into a menu/results state: UI-only input (movement and
	 * look disabled), mouse cursor shown. Pass bUIOnly=false to restore game-and-UI input
	 * and hide the cursor. Safe to call on any client for its own local controller.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revive|UI")
	static void SetUIInputMode(APlayerController* PC, bool bUIOnly, bool bShowCursor);

	/**
	 * Local camera cycle: next alive pawn, wrapping. No-op if the caller is not
	 * spectating / downed or there are no alive teammates. Returns the new view target.
	 */
	UFUNCTION(BlueprintCallable, Category = "Revive|Spectate")
	static APawn* CycleSpectateForward(APlayerController* PC);

	/**
	 * Calls a UFUNCTION by name with a real parameter frame.
	 * ProcessEvent memcpy's ParmsSize bytes out of the frame pointer, so passing
	 * nullptr to a function that declares any parameter reads from address 0.
	 * Returns false when the target is invalid or has no such function.
	 */
	static bool CallFunctionByName(UObject* Target, FName FunctionName);
};
