// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "ArchOpeningSubsystem.generated.h"

class UArchOpeningComponent;
class USceneComponent;

/**
 * Per-world registry mapping every clickable component of every playing opening back to its
 * controller.
 *
 * This is what keeps click interaction off the "search the scene every frame" path: an interactor
 * traces once, then resolves the hit component through a hash lookup. Openings register in
 * BeginPlay and unregister in EndPlay, so entries never outlive their component.
 */
UCLASS()
class ARCHITECTURALOPENINGS_API UArchOpeningSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	void RegisterOpening(UArchOpeningComponent* Opening);
	void UnregisterOpening(UArchOpeningComponent* Opening);

	/** Resolves a hit component (leaf, handle, or proxy) to the opening that owns it. */
	UFUNCTION(BlueprintCallable, Category = "Opening|Interaction")
	UArchOpeningComponent* FindOpeningForComponent(const USceneComponent* Component) const;

	//~ Begin USubsystem Interface
	virtual void Deinitialize() override;
	//~ End USubsystem Interface

private:
	/** Component -> opening. Weak on both sides so a destroyed actor cannot leave a dangling entry. */
	TMap<TWeakObjectPtr<const USceneComponent>, TWeakObjectPtr<UArchOpeningComponent>> ComponentToOpening;
};
