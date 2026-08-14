#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "PyramidPlayerController.generated.h"

class APyramidPlayerState;

/**
 * Spectate + revive vote Client/Server RPCs.
 * Widget open/close is handled in C++; BP hooks remain optional.
 */
UCLASS(Blueprintable)
class PYRAMIDROBBERSV2_API APyramidPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadWrite, Category = "Spectate")
	bool bIsSpectating = false;

	UPROPERTY(BlueprintReadWrite, Category = "ReviveVote")
	bool bHasSubmittedReviveVote = false;

	/** Optional vote UI class (e.g. /Game/UI/WG_ReviveVote). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "ReviveVote")
	TSubclassOf<UUserWidget> ReviveVoteWidgetClass;

	UPROPERTY(BlueprintReadOnly, Category = "ReviveVote")
	TObjectPtr<UUserWidget> ReviveVoteWidgetInstance;

	/** Cached candidates for 1..N keyboard submit while vote UI is open. */
	UPROPERTY(BlueprintReadOnly, Category = "ReviveVote")
	TArray<TObjectPtr<APyramidPlayerState>> CachedVoteCandidates;

	UPROPERTY(BlueprintReadOnly, Category = "ReviveVote")
	bool bCanSubmitReviveVote = false;

	UFUNCTION(Client, Reliable, BlueprintCallable, Category = "ReviveVote")
	void Client_OpenReviveVoteUI(const TArray<APyramidPlayerState*>& Candidates, bool bCanVote);

	UFUNCTION(Client, Reliable, BlueprintCallable, Category = "ReviveVote")
	void Client_UpdateReviveVoteTallies(const TArray<int32>& Tallies);

	UFUNCTION(Client, Reliable, BlueprintCallable, Category = "ReviveVote")
	void Client_CloseReviveVoteUI();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "ReviveVote")
	void Server_SubmitReviveVote(APyramidPlayerState* CandidatePS);

	/** Lets the owning client vote again when the server refused the submission. */
	UFUNCTION(Client, Reliable, Category = "ReviveVote")
	void Client_ReviveVoteSubmitResult(bool bAccepted);

	/** Server-side clear at vote start; the Client_ RPCs only reach the client's copy. */
	void ResetReviveVoteSubmission();

	UFUNCTION(Client, Reliable, BlueprintCallable, Category = "Lives")
	void Client_NotifyLifeLost(int32 NewLivesRemaining);

	UFUNCTION(BlueprintImplementableEvent, Category = "ReviveVote")
	void BP_OpenReviveVoteUI(const TArray<APyramidPlayerState*>& Candidates, bool bCanVote);

	UFUNCTION(BlueprintImplementableEvent, Category = "ReviveVote")
	void BP_UpdateReviveVoteTallies(const TArray<int32>& Tallies);

	UFUNCTION(BlueprintImplementableEvent, Category = "ReviveVote")
	void BP_CloseReviveVoteUI();

	UFUNCTION(BlueprintImplementableEvent, Category = "Lives")
	void BP_NotifyLifeLost(int32 NewLivesRemaining);

	/** Localized status line for the revive vote widget ("press 1-4", "waiting", "vote cast"). */
	UFUNCTION(BlueprintPure, Category = "ReviveVote")
	FText GetReviveVoteStatusText() const;

	/**
	 * Multi-line candidate list for the revive vote widget, one line per downed teammate:
	 * "[n] Name .... X votes". Tallies is indexed to match CachedVoteCandidates; a shorter
	 * or empty array is treated as zero votes.
	 */
	UFUNCTION(BlueprintPure, Category = "ReviveVote")
	FText GetReviveVoteListText(const TArray<int32>& Tallies) const;

	/** Same as GetReviveVoteListText, but reads the live tallies from the GameState itself. */
	UFUNCTION(BlueprintPure, Category = "ReviveVote")
	FText GetReviveVoteListLive() const;

	virtual void SetupInputComponent() override;

protected:
	void HandleReviveVoteKey1();
	void HandleReviveVoteKey2();
	void HandleReviveVoteKey3();
	void HandleReviveVoteKey4();
	void TrySubmitVoteByIndex(int32 Index);
	void HandleSpectateCycle();
	float GetVoteTimeoutHintSeconds() const;
	void RefreshReviveVoteWidget();
};
