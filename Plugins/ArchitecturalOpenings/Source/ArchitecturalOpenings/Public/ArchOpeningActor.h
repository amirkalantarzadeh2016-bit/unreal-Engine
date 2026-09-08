// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "ArchOpeningActor.generated.h"

class UArchOpeningComponent;

/**
 * Convenience actor holding a single UArchOpeningComponent as its root.
 *
 * Nothing requires this actor: the component can be added to any existing actor. It exists so an
 * artist can drop one thing in the level, place it at the opening, and start assigning meshes.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Architectural Opening"))
class ARCHITECTURALOPENINGS_API AArchOpeningActor : public AActor
{
	GENERATED_BODY()

public:
	AArchOpeningActor();

	UFUNCTION(BlueprintPure, Category = "Opening")
	UArchOpeningComponent* GetOpening() const { return Opening; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Opening", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UArchOpeningComponent> Opening;
};
