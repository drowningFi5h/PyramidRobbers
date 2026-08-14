#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "PyramidPlayerState.generated.h"

/**
 * Canonical life / checkpoint / downed state. Blueprints should reparent to this
 * and keep heist-only presentation hooks in BP_OnMarkedDown / BP_OnStoneRevived.
 */
UCLASS(Blueprintable)
class PYRAMIDROBBERSV2_API APyramidPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	APyramidPlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_bIsDown, BlueprintReadWrite, Category = "Lives")
	bool bIsDown = false;

	UPROPERTY(Replicated, BlueprintReadWrite, Category = "Lives")
	int32 LivesRemaining = 2;

	UPROPERTY(Replicated, BlueprintReadWrite, Category = "Checkpoint")
	bool bHasCheckpoint = false;

	UPROPERTY(Replicated, BlueprintReadWrite, Category = "Checkpoint")
	FTransform LastCheckpointTransform;

	UFUNCTION(BlueprintCallable, Category = "Checkpoint")
	void StampCheckpoint(FTransform Transform);

	/** Soft death: if LivesRemaining > 0, decrement and return checkpoint/fallback info. */
	UFUNCTION(BlueprintCallable, Category = "Lives")
	void TryConsumeLife(bool& bRevived, bool& bOutHasValidCheckpoint, FTransform& RespawnTransform);

	UFUNCTION(BlueprintCallable, Category = "Lives")
	void EnterHardDown();

	/** Alias for existing Blueprints that call MarkDown. */
	UFUNCTION(BlueprintCallable, Category = "Lives")
	void MarkDown();

	/** Stone revive: clear downed, LivesRemaining = 1, stamp checkpoint to revive location. */
	UFUNCTION(BlueprintCallable, Category = "Lives")
	void ApplyStoneRevive(FTransform CheckpointTransform);

	/** Alias used by existing Blueprints / checkpoints. */
	UFUNCTION(BlueprintCallable, Category = "Lives")
	void ReviveAtCheckpoint(FTransform CheckpointTransform);

	UFUNCTION(BlueprintPure, Category = "Lives")
	bool IsDown() const { return bIsDown; }

	UFUNCTION(BlueprintPure, Category = "Lives")
	int32 GetLivesRemaining() const { return LivesRemaining; }

	UFUNCTION(BlueprintImplementableEvent, Category = "Lives")
	void BP_OnMarkedDown();

	UFUNCTION(BlueprintImplementableEvent, Category = "Lives")
	void BP_OnStoneRevived();

	UFUNCTION()
	void OnRep_bIsDown();
};
