#include "PyramidPlayerController.h"
#include "PyramidPlayerState.h"
#include "PyramidGameState.h"
#include "PyramidReviveLibrary.h"
#include "Engine/Engine.h"
#include "Components/InputComponent.h"
#include "Components/TextBlock.h"

void APyramidPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (!InputComponent)
	{
		return;
	}
	InputComponent->BindKey(EKeys::One, IE_Pressed, this, &APyramidPlayerController::HandleReviveVoteKey1);
	InputComponent->BindKey(EKeys::Two, IE_Pressed, this, &APyramidPlayerController::HandleReviveVoteKey2);
	InputComponent->BindKey(EKeys::Three, IE_Pressed, this, &APyramidPlayerController::HandleReviveVoteKey3);
	InputComponent->BindKey(EKeys::Four, IE_Pressed, this, &APyramidPlayerController::HandleReviveVoteKey4);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &APyramidPlayerController::HandleSpectateCycle);
}

void APyramidPlayerController::TrySubmitVoteByIndex(int32 Index)
{
	if (!bCanSubmitReviveVote || bHasSubmittedReviveVote)
	{
		return;
	}
	if (!CachedVoteCandidates.IsValidIndex(Index) || !CachedVoteCandidates[Index])
	{
		return;
	}
	// Set before the call: on a listen server the RPC runs inline and a refusal
	// clears this flag, which a set afterwards would overwrite.
	bHasSubmittedReviveVote = true;
	Server_SubmitReviveVote(CachedVoteCandidates[Index]);
	RefreshReviveVoteWidget();
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1, 4.f, FColor::Green,
			FString::Printf(TEXT("Voted for candidate %d"), Index + 1));
	}
}

void APyramidPlayerController::HandleReviveVoteKey1() { TrySubmitVoteByIndex(0); }
void APyramidPlayerController::HandleReviveVoteKey2() { TrySubmitVoteByIndex(1); }
void APyramidPlayerController::HandleReviveVoteKey3() { TrySubmitVoteByIndex(2); }
void APyramidPlayerController::HandleReviveVoteKey4() { TrySubmitVoteByIndex(3); }

void APyramidPlayerController::HandleSpectateCycle()
{
	UPyramidReviveLibrary::CycleSpectateForward(this);
}

void APyramidPlayerController::Client_OpenReviveVoteUI_Implementation(const TArray<APyramidPlayerState*>& Candidates, bool bCanVote)
{
	bHasSubmittedReviveVote = false;
	bCanSubmitReviveVote = bCanVote;
	CachedVoteCandidates = Candidates;

	if (ReviveVoteWidgetClass)
	{
		if (!ReviveVoteWidgetInstance)
		{
			ReviveVoteWidgetInstance = CreateWidget<UUserWidget>(this, ReviveVoteWidgetClass);
			if (ReviveVoteWidgetInstance)
			{
				ReviveVoteWidgetInstance->AddToViewport(50);
			}
		}
		RefreshReviveVoteWidget();
	}

	if (GEngine)
	{
		FString Msg = bCanVote
			? TEXT("REVIVE VOTE: press 1-4 to choose a downed teammate")
			: TEXT("REVIVE VOTE: teammates are voting...");
		for (int32 i = 0; i < Candidates.Num(); ++i)
		{
			if (Candidates[i])
			{
				Msg += FString::Printf(TEXT("\n[%d] %s"), i + 1, *Candidates[i]->GetPlayerName());
			}
		}
		GEngine->AddOnScreenDebugMessage(9911, GetVoteTimeoutHintSeconds(), FColor::Yellow, Msg);
	}

	BP_OpenReviveVoteUI(Candidates, bCanVote);
}

float APyramidPlayerController::GetVoteTimeoutHintSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APyramidGameState* GS = World->GetGameState<APyramidGameState>())
		{
			return FMath::Max(5.f, GS->VoteTimeoutSeconds);
		}
	}
	return 15.f;
}

void APyramidPlayerController::Client_UpdateReviveVoteTallies_Implementation(const TArray<int32>& Tallies)
{
	if (GEngine)
	{
		FString Msg = TEXT("Vote tallies:");
		for (int32 i = 0; i < Tallies.Num(); ++i)
		{
			Msg += FString::Printf(TEXT(" [%d]=%d"), i + 1, Tallies[i]);
		}
		GEngine->AddOnScreenDebugMessage(9912, 8.f, FColor::Cyan, Msg);
	}
	RefreshReviveVoteWidget();
	BP_UpdateReviveVoteTallies(Tallies);
}

void APyramidPlayerController::Client_CloseReviveVoteUI_Implementation()
{
	bHasSubmittedReviveVote = false;
	bCanSubmitReviveVote = false;
	CachedVoteCandidates.Reset();

	if (ReviveVoteWidgetInstance)
	{
		ReviveVoteWidgetInstance->RemoveFromParent();
		ReviveVoteWidgetInstance = nullptr;
	}

	BP_CloseReviveVoteUI();
}

void APyramidPlayerController::Server_SubmitReviveVote_Implementation(APyramidPlayerState* CandidatePS)
{
	// The GameState owns duplicate rejection; gating on this controller's own flag
	// would silently drop every vote after the first, because the reset that
	// Client_CloseReviveVoteUI does never reaches the server's copy.
	APyramidPlayerState* VoterPS = GetPlayerState<APyramidPlayerState>();
	APyramidGameState* GS = GetWorld() ? GetWorld()->GetGameState<APyramidGameState>() : nullptr;
	if (!VoterPS || !GS)
	{
		Client_ReviveVoteSubmitResult(false);
		return;
	}

	const bool bAccepted = GS->Server_SubmitReviveVote(VoterPS, CandidatePS);
	bHasSubmittedReviveVote = bAccepted;
	if (!bAccepted)
	{
		Client_ReviveVoteSubmitResult(false);
	}
}

void APyramidPlayerController::Client_ReviveVoteSubmitResult_Implementation(bool bAccepted)
{
	if (!bAccepted)
	{
		bHasSubmittedReviveVote = false;
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(
				9913, 4.f, FColor::Red, TEXT("Vote not accepted - press 1-4 to try again"));
		}
	}
	RefreshReviveVoteWidget();
}

void APyramidPlayerController::ResetReviveVoteSubmission()
{
	bHasSubmittedReviveVote = false;
}

FText APyramidPlayerController::GetReviveVoteStatusText() const
{
	FString Progress;
	if (const UWorld* World = GetWorld())
	{
		if (const APyramidGameState* GS = World->GetGameState<APyramidGameState>())
		{
			Progress = FString::Printf(TEXT("   (%d / %d voted)"),
				GS->SubmittedVoteCount, GS->AliveVoterCount);
		}
	}

	if (bHasSubmittedReviveVote)
	{
		return FText::FromString(TEXT("Vote cast - waiting for teammates...") + Progress);
	}
	if (bCanSubmitReviveVote)
	{
		return FText::FromString(TEXT("Press 1-4 to vote for who to revive") + Progress);
	}
	return FText::FromString(TEXT("Your teammates are voting...") + Progress);
}

FText APyramidPlayerController::GetReviveVoteListLive() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const APyramidGameState* GS = World->GetGameState<APyramidGameState>())
		{
			return GetReviveVoteListText(GS->VoteTallies);
		}
	}
	return GetReviveVoteListText(TArray<int32>());
}

FText APyramidPlayerController::GetReviveVoteListText(const TArray<int32>& Tallies) const
{
	FString List;
	for (int32 i = 0; i < CachedVoteCandidates.Num(); ++i)
	{
		const APyramidPlayerState* PS = CachedVoteCandidates[i];
		if (!IsValid(PS))
		{
			continue;
		}
		const int32 Votes = Tallies.IsValidIndex(i) ? Tallies[i] : 0;
		if (!List.IsEmpty())
		{
			List += TEXT("\n");
		}
		List += FString::Printf(TEXT("[%d]  %s     %d vote%s"),
			i + 1, *PS->GetPlayerName(), Votes, (Votes == 1 ? TEXT("") : TEXT("s")));
	}
	if (List.IsEmpty())
	{
		List = TEXT("No downed teammates");
	}
	return FText::FromString(List);
}

void APyramidPlayerController::RefreshReviveVoteWidget()
{
	if (!IsValid(ReviveVoteWidgetInstance))
	{
		return;
	}

	if (UTextBlock* Status = Cast<UTextBlock>(ReviveVoteWidgetInstance->GetWidgetFromName(TEXT("StatusText"))))
	{
		Status->SetText(GetReviveVoteStatusText());
	}
	if (UTextBlock* List = Cast<UTextBlock>(ReviveVoteWidgetInstance->GetWidgetFromName(TEXT("ListText"))))
	{
		List->SetText(GetReviveVoteListLive());
	}
}

void APyramidPlayerController::Client_NotifyLifeLost_Implementation(int32 NewLivesRemaining)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			9910, 5.f, FColor::Orange,
			FString::Printf(TEXT("Chances remaining: %d"), NewLivesRemaining));
	}
	BP_NotifyLifeLost(NewLivesRemaining);
}
