#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "PyramidGameState.generated.h"

class APyramidPlayerState;
class APyramidPlayerController;

PYRAMIDROBBERSV2_API DECLARE_LOG_CATEGORY_EXTERN(LogPyramidRevive, Log, All);

/**
 * Server-authoritative revive vote session + heist GameState base.
 * Blueprints reparent here and keep heist score/escape logic in BP.
 *
 * Heist hooks RegisterDeath / RegisterRevive / IsEscapePhase are owned by
 * Blueprint (same names). C++ calls them via ProcessEvent so NativeEvent
 * overrides are not required.
 */
UCLASS(Blueprintable)
class PYRAMIDROBBERSV2_API APyramidGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	APyramidGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ReviveVote")
	bool bReviveVoteActive = false;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ReviveVote")
	FTransform VoteCheckpointTransform;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "ReviveVote")
	float VoteTimeoutSeconds = 15.f;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ReviveVote")
	TObjectPtr<APyramidPlayerState> VoteInstigator;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ReviveVote")
	TObjectPtr<APyramidPlayerState> VoteStoneSpender;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ReviveVote")
	TArray<TObjectPtr<APyramidPlayerState>> VoteCandidates;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ReviveVote")
	TArray<int32> VoteTallies;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ReviveVote")
	int32 AliveVoterCount = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ReviveVote")
	int32 SubmittedVoteCount = 0;

	/** Bitmask of alive voter PlayerArray indices that have submitted. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "ReviveVote")
	int32 VotesSubmittedMask = 0;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "ReviveVote")
	bool Server_StartReviveVote(FTransform CheckpointTransform, APyramidPlayerState* InstigatorPS);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "ReviveVote")
	bool Server_SubmitReviveVote(APyramidPlayerState* VoterPS, APyramidPlayerState* CandidatePS);

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "ReviveVote")
	void Server_ResolveReviveVote();

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "ReviveVote")
	void ClearReviveVoteSession();

	UFUNCTION(BlueprintPure, Category = "ReviveVote")
	void GetDownedCandidates(TArray<APyramidPlayerState*>& OutCandidates) const;

	UFUNCTION(BlueprintPure, Category = "ReviveVote")
	void GetAliveVoters(TArray<APyramidPlayerState*>& OutVoters) const;

	UFUNCTION(BlueprintImplementableEvent, Category = "ReviveVote")
	void BP_OnReviveVoteOpened(const TArray<APyramidPlayerState*>& Candidates);

	UFUNCTION(BlueprintImplementableEvent, Category = "ReviveVote")
	void BP_OnReviveVoteTalliesUpdated(const TArray<int32>& Tallies);

	UFUNCTION(BlueprintImplementableEvent, Category = "ReviveVote")
	void BP_OnReviveVoteClosed();

	/** Invokes Blueprint RegisterDeath if present (PlayersDead++ / TryResolveHeist). */
	void InvokeBP_RegisterDeath();

	/** Invokes Blueprint RegisterRevive if present (PlayersDead--). */
	void InvokeBP_RegisterRevive();

	/** Invokes Blueprint IsEscapePhase if present; otherwise false. */
	bool InvokeBP_IsEscapePhase() const;

	/** Compatibility alias used by PlayerState / vote logic. */
	void RegisterDeath() { InvokeBP_RegisterDeath(); }
	void RegisterRevive() { InvokeBP_RegisterRevive(); }
	bool IsEscapePhase() const { return InvokeBP_IsEscapePhase(); }

protected:
	float VoteTimeRemaining = 0.f;
	TSet<TWeakObjectPtr<APyramidPlayerState>> SubmittedVoters;

	/** Set while resolving, so revive side effects re-entering Blueprint cannot resolve twice. */
	bool bResolvingReviveVote = false;

	/** Recomputes AliveVoterCount / SubmittedVoteCount from the players alive right now. */
	void RefreshVoteCounts();

	void NotifyOpenVoteUI();
	void NotifyUpdateTallies();
	void NotifyCloseVoteUI();
	APyramidPlayerState* PickWinner() const;
};
