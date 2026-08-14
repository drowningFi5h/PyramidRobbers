#include "PyramidGameState.h"
#include "PyramidPlayerState.h"
#include "PyramidPlayerController.h"
#include "PyramidReviveLibrary.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY(LogPyramidRevive);

APyramidGameState::APyramidGameState()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	VoteTimeoutSeconds = 15.f;
}

void APyramidGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APyramidGameState, bReviveVoteActive);
	DOREPLIFETIME(APyramidGameState, VoteCheckpointTransform);
	DOREPLIFETIME(APyramidGameState, VoteInstigator);
	DOREPLIFETIME(APyramidGameState, VoteStoneSpender);
	DOREPLIFETIME(APyramidGameState, VoteCandidates);
	DOREPLIFETIME(APyramidGameState, VoteTallies);
	DOREPLIFETIME(APyramidGameState, AliveVoterCount);
	DOREPLIFETIME(APyramidGameState, SubmittedVoteCount);
	DOREPLIFETIME(APyramidGameState, VotesSubmittedMask);
}

void APyramidGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !bReviveVoteActive)
	{
		return;
	}
	VoteTimeRemaining -= DeltaSeconds;
	if (VoteTimeRemaining <= 0.f)
	{
		UE_LOG(LogPyramidRevive, Log, TEXT("Revive vote timed out after %.1fs, resolving"), VoteTimeoutSeconds);
		Server_ResolveReviveVote();
	}
}

void APyramidGameState::RefreshVoteCounts()
{
	TArray<APyramidPlayerState*> Voters;
	GetAliveVoters(Voters);

	int32 Submitted = 0;
	for (APyramidPlayerState* Voter : Voters)
	{
		if (SubmittedVoters.Contains(Voter))
		{
			++Submitted;
		}
	}

	AliveVoterCount = Voters.Num();
	SubmittedVoteCount = Submitted;
}

void APyramidGameState::GetDownedCandidates(TArray<APyramidPlayerState*>& OutCandidates) const
{
	OutCandidates.Reset();
	for (APlayerState* PS : PlayerArray)
	{
		if (APyramidPlayerState* PyramidPS = Cast<APyramidPlayerState>(PS); IsValid(PyramidPS))
		{
			if (PyramidPS->IsDown())
			{
				OutCandidates.Add(PyramidPS);
			}
		}
	}
}

void APyramidGameState::GetAliveVoters(TArray<APyramidPlayerState*>& OutVoters) const
{
	OutVoters.Reset();
	for (APlayerState* PS : PlayerArray)
	{
		if (APyramidPlayerState* PyramidPS = Cast<APyramidPlayerState>(PS); IsValid(PyramidPS))
		{
			if (!PyramidPS->IsDown())
			{
				OutVoters.Add(PyramidPS);
			}
		}
	}
}

bool APyramidGameState::Server_StartReviveVote(FTransform CheckpointTransform, APyramidPlayerState* InstigatorPS)
{
	if (!HasAuthority() || !IsValid(InstigatorPS))
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("StartReviveVote rejected: no authority or invalid instigator"));
		return false;
	}
	if (bReviveVoteActive)
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("StartReviveVote rejected: a vote is already active"));
		return false;
	}
	if (IsEscapePhase())
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("StartReviveVote rejected: escape phase"));
		return false;
	}
	if (InstigatorPS->IsDown())
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("StartReviveVote rejected: instigator %s is downed"), *InstigatorPS->GetPlayerName());
		return false;
	}

	TArray<APyramidPlayerState*> Candidates;
	GetDownedCandidates(Candidates);
	if (Candidates.Num() == 0)
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("StartReviveVote rejected: nobody is downed"));
		return false;
	}

	TArray<APyramidPlayerState*> Voters;
	GetAliveVoters(Voters);
	if (Voters.Num() == 0)
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("StartReviveVote rejected: nobody is alive to vote"));
		return false;
	}

	ClearReviveVoteSession();

	// Client_OpenReviveVoteUI only resets the client-side copies, so the server's own
	// controller objects keep last vote's submission flag without this.
	for (APlayerState* PS : PlayerArray)
	{
		if (APyramidPlayerState* PyramidPS = Cast<APyramidPlayerState>(PS); IsValid(PyramidPS))
		{
			if (APyramidPlayerController* PC = Cast<APyramidPlayerController>(PyramidPS->GetPlayerController()); IsValid(PC))
			{
				PC->ResetReviveVoteSubmission();
			}
		}
	}

	bReviveVoteActive = true;
	VoteCheckpointTransform = CheckpointTransform;
	VoteInstigator = InstigatorPS;
	VoteStoneSpender = InstigatorPS;
	VoteCandidates = Candidates;
	VoteTallies.Init(0, Candidates.Num());
	AliveVoterCount = Voters.Num();
	SubmittedVoteCount = 0;
	VotesSubmittedMask = 0;
	VoteTimeRemaining = VoteTimeoutSeconds;
	SubmittedVoters.Reset();

	UE_LOG(LogPyramidRevive, Log, TEXT("Revive vote opened by %s: %d downed candidate(s), %d alive voter(s), %.1fs timeout"),
		*InstigatorPS->GetPlayerName(), Candidates.Num(), Voters.Num(), VoteTimeoutSeconds);

	ForceNetUpdate();
	NotifyOpenVoteUI();
	BP_OnReviveVoteOpened(Candidates);
	return true;
}

bool APyramidGameState::Server_SubmitReviveVote(APyramidPlayerState* VoterPS, APyramidPlayerState* CandidatePS)
{
	if (!HasAuthority())
	{
		return false;
	}
	if (!bReviveVoteActive)
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("SubmitReviveVote rejected: no vote is active"));
		return false;
	}
	if (!IsValid(VoterPS) || !IsValid(CandidatePS))
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("SubmitReviveVote rejected: invalid voter or candidate"));
		return false;
	}
	if (VoterPS->IsDown())
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("SubmitReviveVote rejected: voter %s is downed"), *VoterPS->GetPlayerName());
		return false;
	}
	if (SubmittedVoters.Contains(VoterPS))
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("SubmitReviveVote rejected: %s already voted"), *VoterPS->GetPlayerName());
		return false;
	}

	const int32 CandidateIndex = VoteCandidates.IndexOfByKey(CandidatePS);
	if (!VoteTallies.IsValidIndex(CandidateIndex))
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("SubmitReviveVote rejected: %s is not a candidate in this vote"), *CandidatePS->GetPlayerName());
		return false;
	}

	SubmittedVoters.Add(VoterPS);
	VoteTallies[CandidateIndex] += 1;

	// Approximate mask by PlayerArray index for replication/debug.
	const int32 VoterArrayIndex = PlayerArray.IndexOfByKey(VoterPS);
	if (VoterArrayIndex != INDEX_NONE && VoterArrayIndex < 31)
	{
		VotesSubmittedMask |= (1 << VoterArrayIndex);
	}

	// Counted against who is alive now, not at vote start: a voter downed mid-vote
	// would otherwise stall the session until the timeout.
	RefreshVoteCounts();

	UE_LOG(LogPyramidRevive, Log, TEXT("%s voted for %s (%d/%d submitted)"),
		*VoterPS->GetPlayerName(), *CandidatePS->GetPlayerName(), SubmittedVoteCount, AliveVoterCount);

	ForceNetUpdate();
	NotifyUpdateTallies();
	BP_OnReviveVoteTalliesUpdated(VoteTallies);

	if (SubmittedVoteCount >= AliveVoterCount)
	{
		UE_LOG(LogPyramidRevive, Log, TEXT("All alive voters submitted, resolving immediately"));
		Server_ResolveReviveVote();
	}
	return true;
}

APyramidPlayerState* APyramidGameState::PickWinner() const
{
	if (VoteCandidates.Num() == 0)
	{
		return nullptr;
	}

	int32 BestTally = -1;
	TArray<int32> TiedIndices;
	for (int32 i = 0; i < VoteTallies.Num(); ++i)
	{
		const int32 Tally = VoteTallies.IsValidIndex(i) ? VoteTallies[i] : 0;
		if (Tally > BestTally)
		{
			BestTally = Tally;
			TiedIndices.Reset();
			TiedIndices.Add(i);
		}
		else if (Tally == BestTally)
		{
			TiedIndices.Add(i);
		}
	}

	// Zero votes across the board: random among all candidates.
	if (BestTally <= 0)
	{
		TiedIndices.Reset();
		for (int32 i = 0; i < VoteCandidates.Num(); ++i)
		{
			TiedIndices.Add(i);
		}
	}

	const int32 Pick = TiedIndices[FMath::RandRange(0, TiedIndices.Num() - 1)];
	return VoteCandidates.IsValidIndex(Pick) ? VoteCandidates[Pick].Get() : nullptr;
}

void APyramidGameState::Server_ResolveReviveVote()
{
	if (!HasAuthority() || !bReviveVoteActive || bResolvingReviveVote)
	{
		return;
	}

	TGuardValue<bool> ResolveGuard(bResolvingReviveVote, true);

	APyramidPlayerState* Winner = PickWinner();
	const FTransform SpawnTransform = VoteCheckpointTransform;
	APyramidPlayerState* Spender = VoteStoneSpender.Get();

	// Close the session before applying the revive: the effects below re-enter Blueprint
	// (RegisterRevive / TryResolveHeist / spectate exit) and must not observe a live vote,
	// and Tick must not resolve again if any of them takes a frame.
	ClearReviveVoteSession();
	NotifyCloseVoteUI();
	BP_OnReviveVoteClosed();

	if (!IsValid(Winner))
	{
		UE_LOG(LogPyramidRevive, Warning, TEXT("Revive vote closed with no valid winner"));
		return;
	}

	UE_LOG(LogPyramidRevive, Log, TEXT("Revive vote resolved: reviving %s"), *Winner->GetPlayerName());

	Winner->ApplyStoneRevive(SpawnTransform);
	InvokeBP_RegisterRevive();

	if (APlayerController* WinnerController = Winner->GetPlayerController(); IsValid(WinnerController))
	{
		if (ACharacter* WinnerCharacter = Cast<ACharacter>(WinnerController->GetPawn()); IsValid(WinnerCharacter))
		{
			UPyramidReviveLibrary::RestoreMovementFromDowned(WinnerCharacter);
			// Location/rotation only, then force unit root scale: applying the checkpoint
			// transform's scale would compound with the pawn's component scales and shrink it.
			WinnerCharacter->SetActorLocationAndRotation(
				SpawnTransform.GetLocation(), SpawnTransform.GetRotation(),
				false, nullptr, ETeleportType::TeleportPhysics);
			WinnerCharacter->SetActorScale3D(FVector::OneVector);
			UPyramidReviveLibrary::CallFunctionByName(WinnerCharacter, TEXT("RestoreFromDowned"));
			WinnerController->SetViewTargetWithBlend(WinnerCharacter);
		}
		else
		{
			UE_LOG(LogPyramidRevive, Warning, TEXT("%s was revived but their controller has no pawn to restore"), *Winner->GetPlayerName());
		}

		// Exit spectate via the existing Blueprint Client_ExitSpectate event.
		UPyramidReviveLibrary::CallFunctionByName(WinnerController, TEXT("Client_ExitSpectate"));
	}

	if (IsValid(Spender))
	{
		if (AController* SpenderController = Spender->GetPlayerController(); IsValid(SpenderController))
		{
			if (APawn* SpenderPawn = SpenderController->GetPawn(); IsValid(SpenderPawn))
			{
				UPyramidReviveLibrary::CallFunctionByName(SpenderPawn, TEXT("ConsumeReviveStone"));
				UE_LOG(LogPyramidRevive, Log, TEXT("Consumed a revive stone from %s"), *Spender->GetPlayerName());
			}
		}
	}
}

void APyramidGameState::ClearReviveVoteSession()
{
	bReviveVoteActive = false;
	VoteInstigator = nullptr;
	VoteStoneSpender = nullptr;
	VoteCandidates.Reset();
	VoteTallies.Reset();
	AliveVoterCount = 0;
	SubmittedVoteCount = 0;
	VotesSubmittedMask = 0;
	VoteTimeRemaining = 0.f;
	SubmittedVoters.Reset();
	ForceNetUpdate();
}

void APyramidGameState::NotifyOpenVoteUI()
{
	TArray<APyramidPlayerState*> Candidates;
	for (const TObjectPtr<APyramidPlayerState>& C : VoteCandidates)
	{
		if (IsValid(C))
		{
			Candidates.Add(C.Get());
		}
	}

	for (APlayerState* PS : PlayerArray)
	{
		if (APyramidPlayerState* PyramidPS = Cast<APyramidPlayerState>(PS); IsValid(PyramidPS))
		{
			if (APyramidPlayerController* PC = Cast<APyramidPlayerController>(PyramidPS->GetPlayerController()); IsValid(PC))
			{
				const bool bCanVote = !PyramidPS->IsDown();
				PC->Client_OpenReviveVoteUI(Candidates, bCanVote);
			}
		}
	}
}

void APyramidGameState::NotifyUpdateTallies()
{
	for (APlayerState* PS : PlayerArray)
	{
		if (APyramidPlayerState* PyramidPS = Cast<APyramidPlayerState>(PS); IsValid(PyramidPS))
		{
			if (APyramidPlayerController* PC = Cast<APyramidPlayerController>(PyramidPS->GetPlayerController()); IsValid(PC))
			{
				PC->Client_UpdateReviveVoteTallies(VoteTallies);
			}
		}
	}
}

void APyramidGameState::NotifyCloseVoteUI()
{
	for (APlayerState* PS : PlayerArray)
	{
		if (APyramidPlayerState* PyramidPS = Cast<APyramidPlayerState>(PS); IsValid(PyramidPS))
		{
			if (APyramidPlayerController* PC = Cast<APyramidPlayerController>(PyramidPS->GetPlayerController()); IsValid(PC))
			{
				PC->Client_CloseReviveVoteUI();
			}
		}
	}
}

void APyramidGameState::InvokeBP_RegisterDeath()
{
	if (UFunction* Fn = FindFunction(TEXT("RegisterDeath")))
	{
		ProcessEvent(Fn, nullptr);
	}
}

void APyramidGameState::InvokeBP_RegisterRevive()
{
	if (UFunction* Fn = FindFunction(TEXT("RegisterRevive")))
	{
		ProcessEvent(Fn, nullptr);
	}
}

bool APyramidGameState::InvokeBP_IsEscapePhase() const
{
	if (UFunction* Fn = FindFunction(TEXT("IsEscapePhase")))
	{
		struct FIsEscapePhaseParms
		{
			bool ReturnValue = false;
		};
		FIsEscapePhaseParms Parms;
		const_cast<APyramidGameState*>(this)->ProcessEvent(Fn, &Parms);
		return Parms.ReturnValue;
	}
	return false;
}
