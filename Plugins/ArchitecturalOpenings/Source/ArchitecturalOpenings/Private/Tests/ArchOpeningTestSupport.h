// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ArchOpeningComponent.h"
#include "UObject/Object.h"

#include "ArchOpeningTestSupport.generated.h"

/**
 * Counts opening events so the automation tests can assert that each one fires exactly once per
 * real transition.
 *
 * Dynamic multicast delegates can only bind to a UFUNCTION on a UObject, so this has to be a real
 * reflected class rather than a lambda. It is not Blueprintable, is never spawned by the plugin,
 * and exists purely as a test target.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, meta = (DisplayName = "Architectural Opening Event Counter"))
class UArchOpeningEventCounter : public UObject
{
	GENERATED_BODY()

public:
	int32 OpeningStarted = 0;
	int32 FullyOpened = 0;
	int32 ClosingStarted = 0;
	int32 FullyClosed = 0;
	int32 MotionStopped = 0;
	int32 InteractionAccepted = 0;
	int32 ObstructionDetected = 0;

	void BindTo(UArchOpeningComponent* Opening)
	{
		Opening->OnOpeningStarted.AddDynamic(this, &UArchOpeningEventCounter::HandleOpeningStarted);
		Opening->OnFullyOpened.AddDynamic(this, &UArchOpeningEventCounter::HandleFullyOpened);
		Opening->OnClosingStarted.AddDynamic(this, &UArchOpeningEventCounter::HandleClosingStarted);
		Opening->OnFullyClosed.AddDynamic(this, &UArchOpeningEventCounter::HandleFullyClosed);
		Opening->OnMotionStopped.AddDynamic(this, &UArchOpeningEventCounter::HandleMotionStopped);
		Opening->OnInteractionAccepted.AddDynamic(this, &UArchOpeningEventCounter::HandleInteractionAccepted);
		Opening->OnObstructionDetected.AddDynamic(this, &UArchOpeningEventCounter::HandleObstructionDetected);
	}

	void Reset()
	{
		OpeningStarted = FullyOpened = ClosingStarted = FullyClosed = 0;
		MotionStopped = InteractionAccepted = ObstructionDetected = 0;
	}

private:
	UFUNCTION() void HandleOpeningStarted(UArchOpeningComponent*) { ++OpeningStarted; }
	UFUNCTION() void HandleFullyOpened(UArchOpeningComponent*) { ++FullyOpened; }
	UFUNCTION() void HandleClosingStarted(UArchOpeningComponent*) { ++ClosingStarted; }
	UFUNCTION() void HandleFullyClosed(UArchOpeningComponent*) { ++FullyClosed; }
	UFUNCTION() void HandleMotionStopped(UArchOpeningComponent*) { ++MotionStopped; }
	UFUNCTION() void HandleInteractionAccepted(UArchOpeningComponent*, AActor*) { ++InteractionAccepted; }
	UFUNCTION() void HandleObstructionDetected(UArchOpeningComponent*, AActor*) { ++ObstructionDetected; }
};
