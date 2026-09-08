// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ArchOpeningTypes.h"

#include "ArchOpeningFunctionLibrary.generated.h"

class AActor;
class UArchOpeningComponent;
class USceneComponent;
class UObject;

UCLASS()
class ARCHITECTURALOPENINGS_API UArchOpeningFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Resolves a hit component to the opening that owns it, via the world registry.
	 * Falls back to the component's own actor when the registry has no entry, which covers openings
	 * whose parts all belong to the same actor as the opening component.
	 */
	UFUNCTION(BlueprintCallable, Category = "Architectural Openings",
		meta = (WorldContext = "WorldContextObject"))
	static UArchOpeningComponent* ResolveOpeningFromComponent(const UObject* WorldContextObject, USceneComponent* Component);

	/** Resolves a hit result to an opening, applying the opening's clickability filters. */
	UFUNCTION(BlueprintCallable, Category = "Architectural Openings",
		meta = (WorldContext = "WorldContextObject"))
	static UArchOpeningComponent* ResolveOpeningFromHit(const UObject* WorldContextObject, const FHitResult& Hit);

	/** Every opening component on an actor. Convenience for Blueprint graphs. */
	UFUNCTION(BlueprintCallable, Category = "Architectural Openings")
	static TArray<UArchOpeningComponent*> GetOpeningsOnActor(AActor* Actor);

	/** Human readable single-line summary of a validation report, for UI and logs. */
	UFUNCTION(BlueprintPure, Category = "Architectural Openings")
	static FText SummarizeValidation(const FArchOpeningValidationReport& Report);
};
